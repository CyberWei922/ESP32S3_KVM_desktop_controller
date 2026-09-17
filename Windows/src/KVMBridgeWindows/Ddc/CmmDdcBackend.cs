using System;
using System.Diagnostics;
using System.IO;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Ddc;

/// <summary>
/// Optional diagnostic back-end using ControlMyMonitor.exe.
/// Only used when ddc_backend = "control_my_monitor" in config.
/// NOT the default. Never included in release builds as a dependency.
/// </summary>
public sealed class CmmDdcBackend
{
    private readonly string _exePath;
    private readonly string _monitorId;
    private readonly AppLogger _log;

    public CmmDdcBackend(string exePath, string monitorId, AppLogger log)
    {
        _exePath = exePath;
        _monitorId = monitorId;
        _log = log;
    }

    public DdcResult WriteInput(uint inputValue, TimeSpan timeout)
    {
        if (!File.Exists(_exePath))
        {
            _log.Warning($"ControlMyMonitor not found at: {_exePath}");
            return DdcResult.Fail("ddc_transport_unavailable");
        }

        try
        {
            // Avoid shell injection: pass args directly, no cmd.exe
            var psi = new ProcessStartInfo(_exePath)
            {
                Arguments = $"/SetValue \"{_monitorId}\" 60 {inputValue}",
                UseShellExecute = false,
                RedirectStandardOutput = false,
                CreateNoWindow = true
            };

            using var proc = Process.Start(psi);
            if (proc == null) return DdcResult.Fail("ddc_transport_unavailable");

            bool exited = proc.WaitForExit((int)timeout.TotalMilliseconds);
            if (!exited)
            {
                try { proc.Kill(); } catch { }
                _log.Warning("ControlMyMonitor timed out.");
                return DdcResult.Fail("ddc_operation_timed_out");
            }

            if (proc.ExitCode != 0)
            {
                _log.Warning($"ControlMyMonitor exit code {proc.ExitCode}");
                return DdcResult.Fail("ddc_write_failed");
            }

            return DdcResult.SuccessUnconfirmed((int)inputValue);
        }
        catch (Exception ex)
        {
            _log.Error("ControlMyMonitor execution failed.", ex);
            return DdcResult.Fail("ddc_write_failed");
        }
    }
}
