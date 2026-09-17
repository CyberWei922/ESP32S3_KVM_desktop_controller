using System;
using System.IO;
using System.Text.Json;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Startup;

public sealed class DeviceIdentityService
{
    private readonly string _statePath;
    private readonly AppLogger _log;
    private string _deviceId = string.Empty;

    public string DeviceId => _deviceId;

    public DeviceIdentityService(string appDataDir, AppLogger log)
    {
        _statePath = Path.Combine(appDataDir, "state.json");
        _log = log;
    }

    public void Load()
    {
        if (File.Exists(_statePath))
        {
            try
            {
                var json = File.ReadAllText(_statePath, System.Text.Encoding.UTF8);
                using var doc = JsonDocument.Parse(json);
                if (doc.RootElement.TryGetProperty("device_id", out var prop))
                {
                    var id = prop.GetString();
                    if (!string.IsNullOrWhiteSpace(id) && id.StartsWith("win-") && id.Length <= 128)
                    {
                        _deviceId = id;
                        _log.Info($"Device ID loaded: {_deviceId}");
                        return;
                    }
                }
                _log.Warning("state.json had invalid device_id; regenerating.");
            }
            catch (Exception ex)
            {
                _log.Error("Failed to read state.json; regenerating device ID.", ex);
            }
        }

        _deviceId = $"win-{Guid.NewGuid():D}";
        _log.Info($"Generated new device ID: {_deviceId}");
        AtomicSave();
    }

    private void AtomicSave()
    {
        var dir = Path.GetDirectoryName(_statePath)!;
        Directory.CreateDirectory(dir);
        var tmp = _statePath + ".tmp";
        var json = JsonSerializer.Serialize(new { device_id = _deviceId }, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(tmp, json, System.Text.Encoding.UTF8);
        File.Move(tmp, _statePath, overwrite: true);
    }
}
