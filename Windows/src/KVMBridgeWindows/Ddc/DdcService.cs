using System;
using System.Threading;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Ddc;

public interface IDdcService
{
    DdcResult WriteInput(
        string displayId,
        byte vcpCode,
        uint inputValue,
        bool verifyReadback,
        TimeSpan timeout);
}

public sealed class DdcService : IDdcService, IDisposable
{
    private readonly IMonitorDiscoveryService _discovery;
    private readonly AppLogger _log;
    private readonly SemaphoreSlim _serial = new(1, 1);
    private bool _disposed;

    public DdcService(IMonitorDiscoveryService discovery, AppLogger log)
    {
        _discovery = discovery;
        _log = log;
    }

    public DdcResult WriteInput(string displayId, byte vcpCode, uint inputValue, bool verifyReadback, TimeSpan timeout)
    {
        if (!_serial.Wait(timeout))
            return DdcResult.Fail("ddc_operation_timed_out");

        try
        {
            var mon = _discovery.FindById(displayId);
            if (mon == null)
            {
                _log.Warning($"Display not found for ID: {displayId}");
                return DdcResult.Fail("display_not_found");
            }

            return ExecuteWithHandle(mon, vcpCode, inputValue, verifyReadback);
        }
        catch (Exception ex)
        {
            _log.Error("DDC write threw exception.", ex);
            return DdcResult.Fail("ddc_write_failed");
        }
        finally
        {
            _serial.Release();
        }
    }

    private DdcResult ExecuteWithHandle(MonitorInfo mon, byte vcpCode, uint inputValue, bool verifyReadback)
    {
        if (!NativeMethods.GetNumberOfPhysicalMonitorsFromHMONITOR(mon.HMonitor, out uint count) || count == 0)
        {
            _log.Warning("GetNumberOfPhysicalMonitors returned 0.");
            return DdcResult.Fail("ddc_transport_unavailable");
        }

        var physArr = new PHYSICAL_MONITOR[count];
        if (!NativeMethods.GetPhysicalMonitorsFromHMONITOR(mon.HMonitor, count, physArr))
        {
            _log.Warning("GetPhysicalMonitors failed.");
            return DdcResult.Fail("ddc_transport_unavailable");
        }

        try
        {
            if (mon.PhysicalIndex >= count)
            {
                _log.Warning($"Physical index {mon.PhysicalIndex} out of range {count}.");
                return DdcResult.Fail("display_not_found");
            }

            var handle = physArr[mon.PhysicalIndex].hPhysicalMonitor;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            bool ok = NativeMethods.SetVCPFeature(handle, vcpCode, inputValue);
            sw.Stop();
            int errCode = ok ? 0 : System.Runtime.InteropServices.Marshal.GetLastWin32Error();
            _log.Info($"SetVCPFeature(0x{vcpCode:X2}, {inputValue}) => {ok} in {sw.ElapsedMilliseconds}ms" + (ok ? "" : $" Win32Err={errCode}"));

            if (!ok) return DdcResult.Fail("ddc_write_failed");

            if (verifyReadback)
            {
                bool readOk = NativeMethods.GetVCPFeatureAndVCPFeatureReply(handle, vcpCode, out _, out uint current, out _);
                if (readOk && current == inputValue)
                    return DdcResult.SuccessConfirmed((int)inputValue, (int)current);
                _log.Warning($"Read-back mismatch or failed: readOk={readOk} current={current} expected={inputValue}");
                return DdcResult.Fail("invalid_ddc_response");
            }

            return DdcResult.SuccessUnconfirmed((int)inputValue);
        }
        finally
        {
            NativeMethods.DestroyPhysicalMonitors(count, physArr);
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _serial.Dispose();
    }
}

public sealed class DdcResult
{
    public bool WriteSucceeded { get; init; }
    public bool Confirmed { get; init; }
    public int RequestedInput { get; init; }
    public int? ReadBackInput { get; init; }
    public string? ErrorCode { get; init; }

    public static DdcResult Fail(string code) => new() { WriteSucceeded = false, Confirmed = false, ErrorCode = code };
    public static DdcResult SuccessUnconfirmed(int input) => new() { WriteSucceeded = true, Confirmed = false, RequestedInput = input, ErrorCode = "input_written_but_unconfirmed" };
    public static DdcResult SuccessConfirmed(int input, int readBack) => new() { WriteSucceeded = true, Confirmed = true, RequestedInput = input, ReadBackInput = readBack };
}
