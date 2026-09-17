using System;
using Microsoft.Win32;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Startup;

public sealed class StartupService
{
    private const string RegKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "KVMBridgeWindows";
    private readonly AppLogger _log;

    public StartupService(AppLogger log)
    {
        _log = log;
    }

    public bool IsEnabled()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RegKey, writable: false);
            return key?.GetValue(ValueName) != null;
        }
        catch (Exception ex)
        {
            _log.Error("Failed to read startup registry key.", ex);
            return false;
        }
    }

    public void SetEnabled(bool enable)
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RegKey, writable: true);
            if (key == null)
            {
                _log.Error("Cannot open HKCU Run registry key for writing.");
                return;
            }

            if (enable)
            {
                var exePath = System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName
                    ?? System.Environment.ProcessPath
                    ?? throw new InvalidOperationException("Cannot determine EXE path");
                key.SetValue(ValueName, $"\"{exePath}\"");
                _log.Info($"Startup registry entry added: \"{exePath}\"");
            }
            else
            {
                key.DeleteValue(ValueName, throwOnMissingValue: false);
                _log.Info("Startup registry entry removed.");
            }
        }
        catch (Exception ex)
        {
            _log.Error("Failed to modify startup registry key.", ex);
        }
    }
}
