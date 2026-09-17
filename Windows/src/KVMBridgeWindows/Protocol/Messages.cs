using System.Text.Json;
using System.Text.Json.Serialization;

namespace KVMBridgeWindows.Protocol;

// ── Outbound ──────────────────────────────────────────────────────────────────

public sealed class HelloMessage
{
    [JsonPropertyName("type")] public string Type { get; } = "hello";
    [JsonPropertyName("protocol_version")] public int ProtocolVersion { get; } = 1;
    [JsonPropertyName("device_id")] public string DeviceId { get; set; } = "";
    [JsonPropertyName("platform")] public string Platform { get; } = "windows";
    [JsonPropertyName("app")] public string App { get; } = "KVMBridgeWindows";
    [JsonPropertyName("capabilities")] public string[] Capabilities { get; } =
        ["switch_display", "display_sleep", "display_wake", "dashboard_data"];
}

public sealed class MetricValue
{
    [JsonPropertyName("valid")] public bool Valid { get; set; }
    [JsonPropertyName("value")] public double? Value { get; set; }
    [JsonPropertyName("unit")] public string Unit { get; set; } = "";
    [JsonPropertyName("source")] public string Source { get; set; } = "";
    [JsonPropertyName("error")] public string? Error { get; set; }
    [JsonPropertyName("sampled_at_milliseconds")] public long? SampledAtMilliseconds { get; set; }
}

public sealed class TelemetryMessage
{
    [JsonPropertyName("type")] public string Type { get; } = "telemetry";
    [JsonPropertyName("protocol_version")] public int ProtocolVersion { get; } = 1;
    [JsonPropertyName("device_id")] public string DeviceId { get; set; } = "";
    [JsonPropertyName("platform")] public string Platform { get; } = "windows";
    [JsonPropertyName("sequence")] public ulong Sequence { get; set; }
    [JsonPropertyName("timestamp_milliseconds")] public long TimestampMilliseconds { get; set; }
    [JsonPropertyName("metrics")] public TelemetryMetrics Metrics { get; set; } = new();
}

public sealed class TelemetryMetrics
{
    [JsonPropertyName("cpu.temperature.average")] public MetricValue CpuTemperature { get; set; } = new();
    [JsonPropertyName("memory.usage")] public MetricValue MemoryUsage { get; set; } = new();
}

public interface ICommandResultMessage
{
    string RequestId { get; }
    string DeviceId { get; }
    bool Success { get; }
    string? Error { get; }
}

public sealed class CommandResultMessage : ICommandResultMessage
{
    [JsonPropertyName("type")] public string Type { get; } = "command_result";
    [JsonPropertyName("protocol_version")] public int ProtocolVersion { get; } = 1;
    [JsonPropertyName("request_id")] public string RequestId { get; set; } = "";
    [JsonPropertyName("device_id")] public string DeviceId { get; set; } = "";
    [JsonPropertyName("success")] public bool Success { get; set; }
    [JsonPropertyName("write_succeeded")] public bool WriteSucceeded { get; set; }
    [JsonPropertyName("confirmed")] public bool Confirmed { get; set; }
    [JsonPropertyName("requested_input")] public int RequestedInput { get; set; }
    [JsonPropertyName("read_back_input")] public int? ReadBackInput { get; set; }
    [JsonPropertyName("error")] public string? Error { get; set; }
}

public sealed class DisplayPowerCommandResultMessage : ICommandResultMessage
{
    [JsonPropertyName("type")] public string Type { get; } = "command_result";
    [JsonPropertyName("protocol_version")] public int ProtocolVersion { get; } = 1;
    [JsonPropertyName("request_id")] public string RequestId { get; set; } = "";
    [JsonPropertyName("device_id")] public string DeviceId { get; set; } = "";
    [JsonPropertyName("action")] public string Action { get; set; } = "";
    [JsonPropertyName("success")] public bool Success { get; set; }
    [JsonPropertyName("display_power_state")] public string DisplayPowerState { get; set; } = "unchanged";
    [JsonPropertyName("error")] public string? Error { get; set; }
}

// ── Inbound ───────────────────────────────────────────────────────────────────

public sealed class IncomingCommand
{
    [JsonPropertyName("type")] public string Type { get; set; } = "";
    [JsonPropertyName("protocol_version")] public int ProtocolVersion { get; set; }
    [JsonPropertyName("request_id")] public string RequestId { get; set; } = "";
    [JsonPropertyName("target_device_id")] public string TargetDeviceId { get; set; } = "";
    [JsonPropertyName("action")] public string Action { get; set; } = "";
    [JsonPropertyName("target")] public string? Target { get; set; }
}
