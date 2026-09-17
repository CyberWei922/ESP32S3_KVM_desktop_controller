using System;
using System.IO;
using KVMBridgeWindows.Ddc;
using KVMBridgeWindows.Logging;
using Xunit;
using Xunit.Abstractions;

namespace KVMBridgeWindows.Tests;

public class MonitorDiscoveryTests
{
    private readonly ITestOutputHelper _output;

    public MonitorDiscoveryTests(ITestOutputHelper output)
    {
        _output = output;
    }

    [Fact]
    public void Scan_DoesNotThrow_AndProducesStableIdsWhenPhysicalMonitorsAreAvailable()
    {
        using var log = new AppLogger(Path.Combine(Path.GetTempPath(), "test_mon_logs"), LogLevel.Debug, 1);
        var discovery = new MonitorDiscoveryService(log);
        discovery.Scan();

        _output.WriteLine($"Monitors count: {discovery.Monitors.Count}");
        foreach (var m in discovery.Monitors)
        {
            _output.WriteLine($"Monitor: Friendly='{m.FriendlyName}', Desc='{m.Description}', Device='{m.DeviceName}', DDC={m.DdcAvailable}, StableId={m.StableId}");
        }

        foreach (var monitor in discovery.Monitors)
        {
            Assert.False(string.IsNullOrWhiteSpace(monitor.StableId));
            Assert.False(string.IsNullOrWhiteSpace(monitor.FriendlyName));
        }
    }
}
