using System;
using System.Collections.Generic;
using System.Linq;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Protocol;
using LibreHardwareMonitor.Hardware;

namespace KVMBridgeWindows.Metrics;

public sealed class CpuTemperatureReader : IDisposable
{
    private readonly AppLogger _log;
    private Computer? _computer;
    private bool _initialized;
    private bool _disposed;

    public CpuTemperatureReader(AppLogger log) { _log = log; }

    public void TryInitialize()
    {
        if (_initialized) return;
        try
        {
            _computer = new Computer { IsCpuEnabled = true };
            _computer.Open();
            _initialized = true;
            _log.Info("LibreHardwareMonitor CPU monitoring opened.");
        }
        catch (Exception ex)
        {
            _log.Warning($"LibreHardwareMonitor init failed (may need elevated rights): {ex.Message}");
            _computer = null;
            _initialized = true;
        }
    }

    public MetricValue Read(string? sourceName = null)
    {
        var src = string.IsNullOrWhiteSpace(sourceName) ? "LibreHardwareMonitor CPU Package" : sourceName;
        long sampledAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (_computer == null)
            return Invalid("sensor_unavailable", src, sampledAt);

        try
        {
            var temps = new List<float>();
            foreach (var hw in _computer.Hardware)
            {
                if (hw.HardwareType != HardwareType.Cpu) continue;
                hw.Update();
                // Prefer "CPU Package" sensor; fall back to any Temperature sensor
                var packageSensors = hw.Sensors
                    .Where(s => s.SensorType == SensorType.Temperature && s.Name == "CPU Package" && s.Value.HasValue)
                    .ToList();
                if (packageSensors.Count > 0)
                {
                    temps.AddRange(packageSensors.Select(s => s.Value!.Value));
                    continue;
                }
                // Fallback: first available Temperature sensor on this CPU
                var fallback = hw.Sensors
                    .Where(s => s.SensorType == SensorType.Temperature && s.Value.HasValue)
                    .OrderBy(s => s.Name)
                    .FirstOrDefault();
                if (fallback?.Value != null)
                    temps.Add(fallback.Value.Value);
            }

            if (temps.Count == 0)
                return Invalid("sensor_unavailable", src, sampledAt);

            double avg = temps.Average();
            if (avg <= 0 || avg >= 110)
                return Invalid("sensor_read_failed", src, sampledAt);

            return new MetricValue
            {
                Valid = true,
                Value = Math.Round(avg, 1),
                Unit = "celsius",
                Source = src,
                Error = null,
                SampledAtMilliseconds = sampledAt
            };
        }
        catch (Exception ex)
        {
            _log.Error("CPU temperature read failed.", ex);
            return Invalid("sensor_read_failed", src, sampledAt);
        }
    }

    private static MetricValue Invalid(string code, string source, long? sampledAt = null) => new()
    {
        Valid = false, Value = null, Unit = "celsius",
        Source = source, Error = code, SampledAtMilliseconds = null
    };

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _computer?.Close(); } catch { }
    }
}
