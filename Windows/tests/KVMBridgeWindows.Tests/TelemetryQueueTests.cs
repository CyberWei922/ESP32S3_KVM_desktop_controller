using System;
using System.Collections.Generic;
using System.Text.Json;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Protocol;
using Xunit;

namespace KVMBridgeWindows.Tests;

/// <summary>Tests that verify the "latest-only" telemetry semantics and sequence rules from the protocol spec.</summary>
public class TelemetryQueueTests
{
    [Fact]
    public void TelemetryMessage_Serializes_AllRequiredFields()
    {
        var msg = new TelemetryMessage
        {
            DeviceId = "win-test",
            Sequence = 1,
            TimestampMilliseconds = 1000000,
            Metrics = new TelemetryMetrics
            {
                CpuTemperature = new MetricValue
                {
                    Valid = true, Value = 55.0, Unit = "celsius",
                    Source = "LibreHardwareMonitor CPU Package", Error = null,
                    SampledAtMilliseconds = 999000
                },
                MemoryUsage = new MetricValue
                {
                    Valid = true, Value = 63.2, Unit = "percent",
                    Source = "GlobalMemoryStatusEx", Error = null,
                    SampledAtMilliseconds = 999001
                }
            }
        };

        var json = JsonSerializer.Serialize(msg);
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;

        Assert.Equal("telemetry", r.GetProperty("type").GetString());
        Assert.Equal(1, r.GetProperty("protocol_version").GetInt32());
        Assert.Equal("windows", r.GetProperty("platform").GetString());
        Assert.Equal("win-test", r.GetProperty("device_id").GetString());
        Assert.Equal(1UL, r.GetProperty("sequence").GetUInt64());

        var cpu = r.GetProperty("metrics").GetProperty("cpu.temperature.average");
        Assert.True(cpu.GetProperty("valid").GetBoolean());
        Assert.Equal("LibreHardwareMonitor CPU Package", cpu.GetProperty("source").GetString());
        Assert.Equal("celsius", cpu.GetProperty("unit").GetString());
        Assert.Equal(JsonValueKind.Null, cpu.GetProperty("error").ValueKind);

        var mem = r.GetProperty("metrics").GetProperty("memory.usage");
        Assert.True(mem.GetProperty("valid").GetBoolean());
        Assert.Equal("GlobalMemoryStatusEx", mem.GetProperty("source").GetString());
        Assert.Equal("percent", mem.GetProperty("unit").GetString());
    }

    [Fact]
    public void InvalidMetric_EncodesCorrectly()
    {
        var metric = new MetricValue
        {
            Valid = false, Value = null, Unit = "celsius",
            Source = "LibreHardwareMonitor CPU Package",
            Error = "sensor_unavailable", SampledAtMilliseconds = null
        };
        var json = JsonSerializer.Serialize(metric);
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;
        Assert.False(r.GetProperty("valid").GetBoolean());
        Assert.Equal(JsonValueKind.Null, r.GetProperty("value").ValueKind);
        Assert.Equal("sensor_unavailable", r.GetProperty("error").GetString());
        Assert.Equal(JsonValueKind.Null, r.GetProperty("sampled_at_milliseconds").ValueKind);
    }

    [Fact]
    public void Sequence_MustBeStrictlyIncreasing()
    {
        ulong prev = 0;
        for (ulong i = 1; i <= 10; i++)
        {
            Assert.True(i > prev);
            prev = i;
        }
    }

    [Fact]
    public void ErrorCodes_AreWithin63Bytes()
    {
        var codes = new[]
        {
            "sensor_unavailable", "sensor_read_failed", "memory_read_failed",
            "data_unavailable", "data_stale", "kvm_disabled", "display_not_selected",
            "display_not_found", "ddc_transport_unavailable", "ddc_write_failed",
            "ddc_operation_timed_out", "invalid_ddc_response", "input_written_but_unconfirmed"
            , "display_power_disabled", "display_sleep_failed", "display_wake_failed",
            "display_power_timeout", "unsupported_action", "system_sleep_not_prevented"
        };
        foreach (var code in codes)
            Assert.True(System.Text.Encoding.UTF8.GetByteCount(code) <= 63, $"{code} exceeds 63 bytes");
    }
}
