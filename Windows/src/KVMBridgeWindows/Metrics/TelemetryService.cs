using System;
using System.Threading;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Protocol;

namespace KVMBridgeWindows.Metrics;

public sealed class TelemetryService : IDisposable
{
    private readonly ConfigurationService? _config;
    private readonly CpuTemperatureReader _cpu;
    private readonly MemoryReader _mem;
    private readonly AppLogger _log;
    private readonly string _deviceId;
    private ulong _sequence;
    private TelemetryMessage? _latest;
    private readonly object _lock = new();
    private System.Threading.Timer? _timer;
    private bool _disposed;

    public TelemetryService(string deviceId, ConfigurationService? config, CpuTemperatureReader cpu, MemoryReader mem, AppLogger log)
    {
        _deviceId = deviceId;
        _config = config;
        _cpu = cpu;
        _mem = mem;
        _log = log;
    }

    public TelemetryService(string deviceId, CpuTemperatureReader cpu, MemoryReader mem, AppLogger log)
        : this(deviceId, null, cpu, mem, log)
    {
    }

    /// <summary>Starts periodic local sampling so latest metrics are always available offline or online.</summary>
    public void Start(int intervalMs = 1000)
    {
        Collect();
        int period = Math.Max(intervalMs, 500);
        _timer = new System.Threading.Timer(_ =>
        {
            try { Collect(); } catch { }
        }, null, period, period);
    }

    /// <summary>Collect a new snapshot. Called on timer thread or on demand.</summary>
    public TelemetryMessage Collect()
    {
        var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        ulong seq;
        lock (_lock) { seq = ++_sequence; }

        var cpuSource = _config?.Current.Telemetry.CpuTemperatureSource ?? "LibreHardwareMonitor CPU Package";
        var memSource = _config?.Current.Telemetry.MemorySource ?? "GlobalMemoryStatusEx";
        var msg = new TelemetryMessage
        {
            DeviceId = _deviceId,
            Sequence = seq,
            TimestampMilliseconds = now,
            Metrics = new TelemetryMetrics
            {
                CpuTemperature = _cpu.Read(cpuSource),
                MemoryUsage = _mem.Read(memSource)
            }
        };

        lock (_lock) { _latest = msg; }
        return msg;
    }

    public TelemetryMessage? GetLatest()
    {
        lock (_lock) { return _latest; }
    }

    /// <summary>Reset sequence for a new connection session.</summary>
    public void ResetSequence() { lock (_lock) { _sequence = 0; } }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _timer?.Dispose();
        _cpu.Dispose();
    }
}
