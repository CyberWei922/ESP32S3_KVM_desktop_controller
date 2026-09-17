using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Configuration;

public sealed class ConfigurationService
{
    private readonly string _configPath;
    private readonly AppLogger _log;
    private AppConfig _config = new();

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
        ReadCommentHandling = JsonCommentHandling.Skip
    };

    public AppConfig Current => _config;

    public ConfigurationService(string appDataDir, AppLogger log)
    {
        _configPath = Path.Combine(appDataDir, "config.json");
        _log = log;
    }

    public void Load()
    {
        if (!File.Exists(_configPath))
        {
            _log.Info("No config.json found; creating default (KVM disabled, connection disabled).");
            _config = SafeDefault();
            AtomicSave(_config);
            return;
        }

        try
        {
            var json = File.ReadAllText(_configPath, System.Text.Encoding.UTF8);
            var loaded = JsonSerializer.Deserialize<AppConfig>(json, JsonOpts);
            if (loaded == null) throw new InvalidDataException("Deserialized null");
            if (loaded.SchemaVersion > 1)
                throw new InvalidDataException($"Unknown schema_version {loaded.SchemaVersion}");

            Validate(loaded);
            _config = loaded;
            _log.Info($"Config loaded. Connection={_config.Connection.Enabled}, KVM={_config.Kvm.Enabled}, DisplayId='{_config.Kvm.DisplayId}'");
        }
        catch (Exception ex)
        {
            _log.Error("Config load failed; using safe defaults.", ex);
            BackupCorrupted();
            _config = SafeDefault();
        }
    }

    public void Save(AppConfig config)
    {
        Validate(config);
        AtomicSave(config);
        _config = config;
        _log.Info("Config saved.");
    }

    public void UpdateAndSave(Action<AppConfig> mutate)
    {
        // Clone via serialization to avoid partial mutations on failure
        var json = JsonSerializer.Serialize(_config, JsonOpts);
        var clone = JsonSerializer.Deserialize<AppConfig>(json, JsonOpts)!;
        mutate(clone);
        Save(clone);
    }

    private void AtomicSave(AppConfig cfg)
    {
        var dir = Path.GetDirectoryName(_configPath)!;
        Directory.CreateDirectory(dir);
        var tmp = _configPath + ".tmp";
        var json = JsonSerializer.Serialize(cfg, JsonOpts);
        File.WriteAllText(tmp, json, System.Text.Encoding.UTF8);
        File.Move(tmp, _configPath, overwrite: true);
    }

    private static void Validate(AppConfig cfg)
    {
        cfg.Connection ??= new ConnectionConfig();
        cfg.Telemetry ??= new TelemetryConfig();
        cfg.Kvm ??= new KvmConfig();
        cfg.DisplayPower ??= new DisplayPowerConfig();
        cfg.Dashboard ??= new DashboardConfig();
        cfg.Startup ??= new StartupConfig();
        cfg.Logging ??= new LoggingConfig();
        if (cfg.Connection.TelemetryIntervalMs < 500)
            cfg.Connection.TelemetryIntervalMs = 500;
        if (cfg.Connection.KeepaliveIntervalMs < 5000)
            cfg.Connection.KeepaliveIntervalMs = 5000;
        if (cfg.Kvm.VcpCode != 96)
            cfg.Kvm.VcpCode = 96;
        if (cfg.Kvm.MacInput != 6)
            cfg.Kvm.MacInput = 6;
        if (cfg.Kvm.WindowsInput != 7)
            cfg.Kvm.WindowsInput = 7;
        if (cfg.DisplayPower.CommandTimeoutMilliseconds <= 0 ||
            cfg.DisplayPower.CommandTimeoutMilliseconds > 4000)
            cfg.DisplayPower.CommandTimeoutMilliseconds = 3000;
        if (cfg.Dashboard.SendIntervalSeconds < 1 || cfg.Dashboard.SendIntervalSeconds > 3600)
            cfg.Dashboard.SendIntervalSeconds = 10;
        if (cfg.Dashboard.WeatherRefreshMinutes < 1 || cfg.Dashboard.WeatherRefreshMinutes > 1440)
            cfg.Dashboard.WeatherRefreshMinutes = 15;
        cfg.Dashboard.WeatherProvider = cfg.Dashboard.WeatherProvider.Trim().ToLowerInvariant();
        if (cfg.Dashboard.WeatherProvider is not ("open_meteo" or "qweather" or "amap"))
            cfg.Dashboard.WeatherProvider = "open_meteo";
        cfg.Dashboard.CityCode = cfg.Dashboard.CityCode.Trim();
        cfg.Dashboard.CityName = cfg.Dashboard.CityName.Trim();
        cfg.Dashboard.QWeatherApiHost = cfg.Dashboard.QWeatherApiHost.Trim().TrimEnd('/');
        if (string.IsNullOrWhiteSpace(cfg.Dashboard.QWeatherApiKeyCredentialName))
            cfg.Dashboard.QWeatherApiKeyCredentialName = "KVMBridgeWindows.QWeather";
        if (string.IsNullOrWhiteSpace(cfg.Dashboard.AMapApiKeyCredentialName))
            cfg.Dashboard.AMapApiKeyCredentialName = "KVMBridgeWindows.AMap";
        // KVM cannot be enabled without a display ID
        if (cfg.Kvm.Enabled && string.IsNullOrWhiteSpace(cfg.Kvm.DisplayId))
            cfg.Kvm.Enabled = false;
    }

    private static AppConfig SafeDefault() => new()
    {
        SchemaVersion = 1,
        Connection = new ConnectionConfig { Enabled = false },
        Kvm = new KvmConfig { Enabled = false, DisplayId = string.Empty },
        DisplayPower = new DisplayPowerConfig
        {
            Enabled = false,
            PreventSystemSleepWhileEnabled = true,
            CommandTimeoutMilliseconds = 3000
        },
        Dashboard = new DashboardConfig(),
        Startup = new StartupConfig { StartWithWindows = false },
        Logging = new LoggingConfig { Level = "info", RetentionDays = 7 }
    };

    private void BackupCorrupted()
    {
        try
        {
            var backup = _configPath + $".corrupt.{DateTime.Now:yyyyMMddHHmmss}";
            File.Copy(_configPath, backup, overwrite: true);
            _log.Warning($"Corrupted config backed up to {backup}");
        }
        catch { }
    }
}
