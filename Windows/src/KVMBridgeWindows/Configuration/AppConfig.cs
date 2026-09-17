using System.Text.Json.Serialization;

namespace KVMBridgeWindows.Configuration;

public sealed class AppConfig
{
    [JsonPropertyName("schema_version")]
    public int SchemaVersion { get; set; } = 1;

    [JsonPropertyName("connection")]
    public ConnectionConfig Connection { get; set; } = new();

    [JsonPropertyName("telemetry")]
    public TelemetryConfig Telemetry { get; set; } = new();

    [JsonPropertyName("kvm")]
    public KvmConfig Kvm { get; set; } = new();

    [JsonPropertyName("displayPower")]
    public DisplayPowerConfig DisplayPower { get; set; } = new();

    [JsonPropertyName("dashboard")]
    public DashboardConfig Dashboard { get; set; } = new();

    [JsonPropertyName("startup")]
    public StartupConfig Startup { get; set; } = new();

    [JsonPropertyName("logging")]
    public LoggingConfig Logging { get; set; } = new();
}

public sealed class DashboardConfig
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = true;

    [JsonPropertyName("send_interval_seconds")]
    public int SendIntervalSeconds { get; set; } = 10;

    [JsonPropertyName("weather_refresh_minutes")]
    public int WeatherRefreshMinutes { get; set; } = 15;

    [JsonPropertyName("weather_provider")]
    public string WeatherProvider { get; set; } = "open_meteo";

    [JsonPropertyName("city_code")]
    public string CityCode { get; set; } = string.Empty;

    [JsonPropertyName("city_name")]
    public string CityName { get; set; } = string.Empty;

    [JsonPropertyName("latitude")]
    public double? Latitude { get; set; }

    [JsonPropertyName("longitude")]
    public double? Longitude { get; set; }

    [JsonPropertyName("timezone")]
    public string Timezone { get; set; } = string.Empty;

    [JsonPropertyName("qweather_api_host")]
    public string QWeatherApiHost { get; set; } = string.Empty;

    [JsonPropertyName("qweather_api_key_credential_name")]
    public string QWeatherApiKeyCredentialName { get; set; } = "KVMBridgeWindows.QWeather";

    [JsonPropertyName("amap_api_key_credential_name")]
    public string AMapApiKeyCredentialName { get; set; } = "KVMBridgeWindows.AMap";
}

public sealed class DisplayPowerConfig
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = false;

    [JsonPropertyName("preventSystemSleepWhileEnabled")]
    public bool PreventSystemSleepWhileEnabled { get; set; } = true;

    [JsonPropertyName("commandTimeoutMilliseconds")]
    public int CommandTimeoutMilliseconds { get; set; } = 3000;
}

public sealed class ConnectionConfig
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = false;

    [JsonPropertyName("host")]
    public string Host { get; set; } = "192.168.1.50";

    [JsonPropertyName("port")]
    public int Port { get; set; } = 81;

    [JsonPropertyName("path")]
    public string Path { get; set; } = "/statsforkvm";

    [JsonPropertyName("telemetry_interval_ms")]
    public int TelemetryIntervalMs { get; set; } = 1000;

    [JsonPropertyName("keepalive_interval_ms")]
    public int KeepaliveIntervalMs { get; set; } = 15000;

    public string WebSocketUrl => $"ws://{Host}:{Port}{Path}";
}

public sealed class TelemetryConfig
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = true;

    [JsonPropertyName("cpu_temperature_source")]
    public string CpuTemperatureSource { get; set; } = "LibreHardwareMonitor CPU Package";

    [JsonPropertyName("memory_source")]
    public string MemorySource { get; set; } = "GlobalMemoryStatusEx";
}

public sealed class KvmConfig
{
    [JsonPropertyName("enabled")]
    public bool Enabled { get; set; } = false;

    [JsonPropertyName("display_id")]
    public string DisplayId { get; set; } = string.Empty;

    [JsonPropertyName("vcp_code")]
    public int VcpCode { get; set; } = 96; // 0x60

    [JsonPropertyName("mac_input")]
    public int MacInput { get; set; } = 6;

    [JsonPropertyName("windows_input")]
    public int WindowsInput { get; set; } = 7;

    [JsonPropertyName("verify_readback")]
    public bool VerifyReadback { get; set; } = false;

    [JsonPropertyName("ddc_backend")]
    public string DdcBackend { get; set; } = "native";

    [JsonPropertyName("control_my_monitor_path")]
    public string? ControlMyMonitorPath { get; set; } = null;

    [JsonPropertyName("control_my_monitor_monitor_id")]
    public string? ControlMyMonitorMonitorId { get; set; } = null;
}

public sealed class StartupConfig
{
    [JsonPropertyName("start_with_windows")]
    public bool StartWithWindows { get; set; } = false;
}

public sealed class LoggingConfig
{
    [JsonPropertyName("level")]
    public string Level { get; set; } = "info";

    [JsonPropertyName("retention_days")]
    public int RetentionDays { get; set; } = 7;
}
