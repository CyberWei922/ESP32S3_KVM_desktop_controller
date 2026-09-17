using System;
using KVMBridgeWindows.Ddc;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Protocol;

namespace KVMBridgeWindows.Metrics;

public sealed class MemoryReader
{
    private readonly AppLogger _log;

    public MemoryReader(AppLogger log) { _log = log; }

    public MetricValue Read(string? sourceName = null)
    {
        var src = string.IsNullOrWhiteSpace(sourceName) ? "GlobalMemoryStatusEx" : sourceName;
        long sampledAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        try
        {
            var status = new MEMORYSTATUSEX { dwLength = (uint)System.Runtime.InteropServices.Marshal.SizeOf<MEMORYSTATUSEX>() };
            if (!Kernel32.GlobalMemoryStatusEx(ref status))
            {
                _log.Warning("GlobalMemoryStatusEx failed.");
                return Invalid("memory_read_failed", src);
            }
            if (status.ullTotalPhys == 0)
                return Invalid("memory_read_failed", src);

            double pct = 100.0 * (status.ullTotalPhys - status.ullAvailPhys) / status.ullTotalPhys;
            pct = Math.Clamp(pct, 0.0, 100.0);
            if (!double.IsFinite(pct))
                return Invalid("memory_read_failed", src);

            return new MetricValue
            {
                Valid = true,
                Value = Math.Round(pct, 1),
                Unit = "percent",
                Source = src,
                Error = null,
                SampledAtMilliseconds = sampledAt
            };
        }
        catch (Exception ex)
        {
            _log.Error("Memory read threw exception.", ex);
            return Invalid("memory_read_failed", src);
        }
    }

    private static MetricValue Invalid(string code, string source) => new()
    {
        Valid = false, Value = null, Unit = "percent",
        Source = source, Error = code, SampledAtMilliseconds = null
    };
}
