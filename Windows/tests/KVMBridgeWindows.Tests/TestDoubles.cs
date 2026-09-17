using System.Collections.Concurrent;
using KVMBridgeWindows.Ddc;
using KVMBridgeWindows.DisplayPower;

namespace KVMBridgeWindows.Tests;

internal sealed class FakeDisplayPowerPlatform : IDisplayPowerPlatform
{
    public PlatformCallResult MonitorResult { get; set; } = PlatformCallResult.Success();
    public PlatformCallResult ExecutionResult { get; set; } = PlatformCallResult.Success();
    public int DelayMilliseconds { get; set; }
    public ConcurrencyTracker? Tracker { get; set; }
    public ConcurrentQueue<(int Parameter, uint Timeout)> MonitorCalls { get; } = new();
    public ConcurrentQueue<uint> ExecutionCalls { get; } = new();
    public ConcurrentQueue<string> CallOrder { get; } = new();

    public PlatformCallResult SendMonitorPower(int powerParameter, uint timeoutMilliseconds)
    {
        using var lease = Tracker?.Enter();
        MonitorCalls.Enqueue((powerParameter, timeoutMilliseconds));
        CallOrder.Enqueue($"monitor:{powerParameter}");
        if (DelayMilliseconds > 0) Thread.Sleep(DelayMilliseconds);
        return MonitorResult;
    }

    public PlatformCallResult SetExecutionState(uint flags)
    {
        ExecutionCalls.Enqueue(flags);
        CallOrder.Enqueue($"execution:{flags}");
        return ExecutionResult;
    }
}

internal sealed class FakeDdcService : IDdcService
{
    public DdcResult Result { get; set; } = DdcResult.SuccessUnconfirmed(6);
    public int DelayMilliseconds { get; set; }
    public int CallCount;
    public ConcurrencyTracker? Tracker { get; set; }

    public DdcResult WriteInput(string displayId, byte vcpCode, uint inputValue, bool verifyReadback, TimeSpan timeout)
    {
        using var lease = Tracker?.Enter();
        Interlocked.Increment(ref CallCount);
        if (DelayMilliseconds > 0) Thread.Sleep(DelayMilliseconds);
        return Result.WriteSucceeded ? DdcResult.SuccessUnconfirmed((int)inputValue) : Result;
    }
}

internal sealed class FakeMonitorDiscoveryService : IMonitorDiscoveryService
{
    private readonly MonitorInfo _monitor = new()
    {
        StableId = "mon-test-display",
        FriendlyName = "Test display",
        DdcAvailable = true
    };

    public IReadOnlyList<MonitorInfo> Monitors => [_monitor];
    public void Scan() { }
    public MonitorInfo? FindById(string stableId) => stableId == _monitor.StableId ? _monitor : null;
}

internal sealed class ConcurrencyTracker
{
    private int _active;
    private int _maxActive;
    public int MaxActive => Volatile.Read(ref _maxActive);

    public IDisposable Enter()
    {
        var active = Interlocked.Increment(ref _active);
        int observed;
        do
        {
            observed = Volatile.Read(ref _maxActive);
            if (active <= observed) break;
        } while (Interlocked.CompareExchange(ref _maxActive, active, observed) != observed);
        return new Releaser(this);
    }

    private sealed class Releaser : IDisposable
    {
        private readonly ConcurrencyTracker _owner;
        private int _disposed;
        public Releaser(ConcurrencyTracker owner) => _owner = owner;
        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
                Interlocked.Decrement(ref _owner._active);
        }
    }
}
