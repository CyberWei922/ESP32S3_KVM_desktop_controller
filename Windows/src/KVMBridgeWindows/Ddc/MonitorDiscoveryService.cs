using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Ddc;

public interface IMonitorDiscoveryService
{
    IReadOnlyList<MonitorInfo> Monitors { get; }
    void Scan();
    MonitorInfo? FindById(string stableId);
}

public sealed class MonitorDiscoveryService : IMonitorDiscoveryService
{
    private readonly AppLogger _log;
    private List<MonitorInfo> _monitors = new();

    public IReadOnlyList<MonitorInfo> Monitors => _monitors;

    public MonitorDiscoveryService(AppLogger log) { _log = log; }

    public void Scan()
    {
        var results = new List<MonitorInfo>();
        var configTargets = QueryDisplayConfigTargets();

        try
        {
            NativeMethods.EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr hMon, IntPtr hdc, ref RECT rect, IntPtr data) =>
            {
                var info = new MONITORINFOEX { cbSize = Marshal.SizeOf<MONITORINFOEX>() };
                if (!NativeMethods.GetMonitorInfo(hMon, ref info))
                {
                    _log.Warning($"GetMonitorInfo failed for HMONITOR {hMon}, err={Marshal.GetLastWin32Error()}");
                    return true;
                }
                string deviceName = info.szDevice ?? "";

                if (!NativeMethods.GetNumberOfPhysicalMonitorsFromHMONITOR(hMon, out uint count) || count == 0)
                {
                    _log.Info($"No physical monitors for {deviceName} (HMONITOR {hMon})");
                    return true;
                }

                var physArr = new PHYSICAL_MONITOR[count];
                if (!NativeMethods.GetPhysicalMonitorsFromHMONITOR(hMon, count, physArr))
                {
                    _log.Warning($"GetPhysicalMonitors failed for {deviceName}");
                    return true;
                }

                try
                {
                    configTargets.TryGetValue(deviceName, out var targetInfo);

                    for (uint i = 0; i < count; i++)
                    {
                        var desc = physArr[i].szPhysicalMonitorDescription ?? "";
                        string friendly = !string.IsNullOrWhiteSpace(targetInfo.FriendlyName)
                            ? targetInfo.FriendlyName
                            : (!string.IsNullOrWhiteSpace(desc) ? desc : deviceName);

                        var stableId = BuildStableId(targetInfo.DevicePath, deviceName, desc, i);
                        bool ddcOk = ProbeVcp(physArr[i].hPhysicalMonitor);

                        results.Add(new MonitorInfo
                        {
                            StableId = stableId,
                            FriendlyName = friendly,
                            DeviceName = deviceName,
                            Description = desc,
                            HMonitor = hMon,
                            PhysicalIndex = i,
                            DdcAvailable = ddcOk
                        });
                    }
                }
                finally
                {
                    NativeMethods.DestroyPhysicalMonitors(count, physArr);
                }
                return true;
            }, IntPtr.Zero);
        }
        catch (Exception ex)
        {
            _log.Error("Monitor scan failed.", ex);
        }

        _monitors = results;
        _log.Info($"Monitor scan complete: {results.Count} physical monitor(s) found.");
        foreach (var m in results)
            _log.Info($"  [{m.IdSuffix}] '{m.FriendlyName}' (desc='{m.Description}') DDC={m.DdcAvailable} Device={m.DeviceName} StableId={m.StableId}");
    }

    public MonitorInfo? FindById(string stableId)
    {
        if (string.IsNullOrWhiteSpace(stableId)) return null;
        var matches = _monitors.FindAll(m => m.StableId == stableId);
        if (matches.Count == 1) return matches[0];
        if (matches.Count > 1)
        {
            _log.Warning($"Ambiguous monitor ID '{stableId}': {matches.Count} matches. Returning null.");
            return null;
        }
        return null;
    }

    private Dictionary<string, (string FriendlyName, string DevicePath)> QueryDisplayConfigTargets()
    {
        var map = new Dictionary<string, (string, string)>(StringComparer.OrdinalIgnoreCase);
        try
        {
            const uint QDC_ONLY_ACTIVE_PATHS = 2;
            int err = NativeMethods.GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, out uint pathCount, out uint modeCount);
            if (err != 0 || pathCount == 0) return map;

            var paths = new DISPLAYCONFIG_PATH_INFO[pathCount];
            var modes = new DISPLAYCONFIG_MODE_INFO[modeCount];
            err = NativeMethods.QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, ref pathCount, paths, ref modeCount, modes, IntPtr.Zero);
            if (err != 0) return map;

            for (int i = 0; i < pathCount; i++)
            {
                var sourceName = new DISPLAYCONFIG_SOURCE_DEVICE_NAME
                {
                    header = new DISPLAYCONFIG_DEVICE_INFO_HEADER
                    {
                        type = DISPLAYCONFIG_DEVICE_INFO_TYPE.DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                        size = (uint)Marshal.SizeOf<DISPLAYCONFIG_SOURCE_DEVICE_NAME>(),
                        adapterId = paths[i].sourceInfo.adapterId,
                        id = paths[i].sourceInfo.id
                    }
                };
                if (NativeMethods.DisplayConfigGetDeviceInfo(ref sourceName) != 0) continue;

                var targetName = new DISPLAYCONFIG_TARGET_DEVICE_NAME
                {
                    header = new DISPLAYCONFIG_DEVICE_INFO_HEADER
                    {
                        type = DISPLAYCONFIG_DEVICE_INFO_TYPE.DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,
                        size = (uint)Marshal.SizeOf<DISPLAYCONFIG_TARGET_DEVICE_NAME>(),
                        adapterId = paths[i].targetInfo.adapterId,
                        id = paths[i].targetInfo.id
                    }
                };
                if (NativeMethods.DisplayConfigGetDeviceInfo(ref targetName) != 0) continue;

                var gdi = sourceName.viewGdiDeviceName;
                var friendly = targetName.monitorFriendlyDeviceName;
                var devPath = targetName.monitorDevicePath;

                if (!string.IsNullOrWhiteSpace(gdi))
                {
                    map[gdi] = (friendly ?? "", devPath ?? "");
                }
            }
        }
        catch (Exception ex)
        {
            _log.Warning($"QueryDisplayConfig failed: {ex.Message}");
        }
        return map;
    }

    private static string BuildStableId(string? devicePath, string deviceName, string description, uint index)
    {
        var raw = !string.IsNullOrWhiteSpace(devicePath)
            ? devicePath
            : $"{deviceName}|{description}|{index}";
        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(raw));
        return "mon-" + Convert.ToHexString(hash)[..16].ToLowerInvariant();
    }

    private static bool ProbeVcp(IntPtr hPhys)
    {
        try { return NativeMethods.GetVCPFeatureAndVCPFeatureReply(hPhys, 0x60, out _, out _, out _); }
        catch { return false; }
    }
}
