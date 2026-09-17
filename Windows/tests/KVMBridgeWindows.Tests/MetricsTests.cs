using KVMBridgeWindows.Ddc;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Metrics;
using Xunit;

namespace KVMBridgeWindows.Tests;

public class MetricsTests
{
    private readonly AppLogger _log;
    public MetricsTests()
    {
        _log = new AppLogger(Path.Combine(Path.GetTempPath(), "kvmtest_metrics"), LogLevel.Debug, 1);
    }

    [Fact]
    public void Memory_ReturnsValidOrInvalidMetric()
    {
        var reader = new MemoryReader(_log);
        var m = reader.Read();
        Assert.Equal("GlobalMemoryStatusEx", m.Source);
        Assert.Equal("percent", m.Unit);
        if (m.Valid)
        {
            Assert.NotNull(m.Value);
            Assert.InRange(m.Value!.Value, 0.0, 100.0);
            Assert.Null(m.Error);
            Assert.NotNull(m.SampledAtMilliseconds);
        }
        else
        {
            Assert.Null(m.Value);
            Assert.NotNull(m.Error);
            Assert.Null(m.SampledAtMilliseconds);
        }
    }

    [Fact]
    public void Memory_ValueClamped_0_To_100()
    {
        var reader = new MemoryReader(_log);
        var m = reader.Read();
        if (m.Valid)
            Assert.InRange(m.Value!.Value, 0.0, 100.0);
    }

    [Fact]
    public void Cpu_ReturnsMetric_WithCorrectSource()
    {
        var reader = new CpuTemperatureReader(_log);
        reader.TryInitialize();
        var m = reader.Read();
        Assert.Equal("LibreHardwareMonitor CPU Package", m.Source);
        Assert.Equal("celsius", m.Unit);
        if (m.Valid)
        {
            Assert.NotNull(m.Value);
            Assert.True(m.Value!.Value > 0 && m.Value.Value < 110);
            Assert.Null(m.Error);
        }
        else
        {
            Assert.Null(m.Value);
            Assert.NotNull(m.Error);
            // CPU failure must not produce incorrect valid=true
            Assert.False(m.Valid);
        }
        reader.Dispose();
    }

    [Fact]
    public void TelemetryService_SequenceStrictlyIncreasing()
    {
        var cpu = new CpuTemperatureReader(_log);
        cpu.TryInitialize();
        var mem = new MemoryReader(_log);
        var svc = new TelemetryService("win-test", cpu, mem, _log);

        var msgs = new List<ulong>();
        for (int i = 0; i < 5; i++)
            msgs.Add(svc.Collect().Sequence);

        for (int i = 1; i < msgs.Count; i++)
            Assert.True(msgs[i] > msgs[i - 1], $"Sequence not increasing at index {i}");

        svc.Dispose();
    }

    [Fact]
    public void TelemetryService_SequenceResets_OnNewSession()
    {
        var cpu = new CpuTemperatureReader(_log);
        cpu.TryInitialize();
        var mem = new MemoryReader(_log);
        var svc = new TelemetryService("win-test", cpu, mem, _log);

        svc.Collect();
        svc.Collect();
        svc.ResetSequence();
        var first = svc.Collect().Sequence;
        Assert.Equal(1UL, first);
        svc.Dispose();
    }
}
