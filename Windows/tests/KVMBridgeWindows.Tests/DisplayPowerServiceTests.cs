using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using Xunit;

namespace KVMBridgeWindows.Tests;

public sealed class DisplayPowerServiceTests : IDisposable
{
    private readonly string _tempDir = Path.Combine(Path.GetTempPath(), "kvmpower_" + Guid.NewGuid().ToString("N"));
    private readonly AppLogger _log;

    public DisplayPowerServiceTests()
    {
        Directory.CreateDirectory(_tempDir);
        _log = new AppLogger(Path.Combine(_tempDir, "logs"), LogLevel.Debug, 1);
    }

    [Fact]
    public void Sleep_UsesBoundedMonitorPowerMessage()
    {
        var platform = new FakeDisplayPowerPlatform();
        using var service = new DisplayPowerService(platform, _log);

        var result = service.RequestSleep(2345);

        Assert.True(result.Success);
        Assert.Equal("sleep_requested", result.DisplayPowerState);
        Assert.Null(result.Error);
        Assert.Equal(DisplayOutputState.OutputSleeping, service.State);
        Assert.Equal((2, 2345U), Assert.Single(platform.MonitorCalls));
        Assert.DoesNotContain(DisplayPowerService.EsDisplayRequired, platform.ExecutionCalls);
    }

    [Fact]
    public void Wake_RequestsMonitorFirstThenResetsDisplayIdleTimer()
    {
        var platform = new FakeDisplayPowerPlatform();
        using var service = new DisplayPowerService(platform, _log);
        service.RequestSleep(3000);
        platform.MonitorCalls.Clear();
        platform.ExecutionCalls.Clear();
        platform.CallOrder.Clear();

        var result = service.RequestWake(1234);

        Assert.True(result.Success);
        Assert.Equal("wake_requested", result.DisplayPowerState);
        Assert.Equal(DisplayOutputState.Active, service.State);
        Assert.Equal((-1, 1234U), Assert.Single(platform.MonitorCalls));
        Assert.Equal(DisplayPowerService.EsDisplayRequired, Assert.Single(platform.ExecutionCalls));
        Assert.Equal(
            ["monitor:-1", $"execution:{DisplayPowerService.EsDisplayRequired}"],
            platform.CallOrder.ToArray());
    }

    [Theory]
    [InlineData(true, "display_power_timeout")]
    [InlineData(false, "display_sleep_failed")]
    public void SleepFailure_MapsStableError(bool timedOut, string expected)
    {
        var platform = new FakeDisplayPowerPlatform
        {
            MonitorResult = PlatformCallResult.Failure(timedOut ? 1460 : 5, timedOut)
        };
        using var service = new DisplayPowerService(platform, _log);

        var result = service.RequestSleep(3000);

        Assert.False(result.Success);
        Assert.Equal("unchanged", result.DisplayPowerState);
        Assert.Equal(expected, result.Error);
        Assert.Equal(DisplayOutputState.Error, service.State);
    }

    [Fact]
    public void WakeExecutionStateFailure_ReturnsWakeFailed()
    {
        var platform = new FakeDisplayPowerPlatform();
        using var service = new DisplayPowerService(platform, _log);
        service.RequestSleep(3000);
        platform.ExecutionResult = PlatformCallResult.Failure(5);

        var result = service.RequestWake(3000);

        Assert.False(result.Success);
        Assert.Equal("display_wake_failed", result.Error);
        Assert.Equal(DisplayOutputState.Error, service.State);
    }

    [Fact]
    public void RepeatedSleepAndActiveWake_AreIdempotent()
    {
        var platform = new FakeDisplayPowerPlatform();
        using var service = new DisplayPowerService(platform, _log);
        service.RequestWake(3000);
        Assert.Empty(platform.MonitorCalls);

        service.RequestSleep(3000);
        service.RequestSleep(3000);
        Assert.Single(platform.MonitorCalls);
    }

    [Fact]
    public void SystemSleepAssertion_UsesOnlySystemRequiredAndReleases()
    {
        var platform = new FakeDisplayPowerPlatform();
        using var service = new DisplayPowerService(platform, _log);

        Assert.True(service.UpdateSystemSleepAssertion(true, true, true));
        Assert.Equal(SystemSleepAssertionState.Active, service.AssertionState);
        Assert.Equal(
            DisplayPowerService.EsContinuous | DisplayPowerService.EsSystemRequired,
            Assert.Single(platform.ExecutionCalls));
        Assert.DoesNotContain(
            platform.ExecutionCalls,
            flags => (flags & DisplayPowerService.EsDisplayRequired) != 0);

        platform.ExecutionCalls.Clear();
        Assert.True(service.UpdateSystemSleepAssertion(false, true, true));
        Assert.Equal(DisplayPowerService.EsContinuous, Assert.Single(platform.ExecutionCalls));
        Assert.Equal(SystemSleepAssertionState.Disabled, service.AssertionState);
    }

    [Fact]
    public void Dispose_ReleasesContinuousAssertion()
    {
        var platform = new FakeDisplayPowerPlatform();
        var service = new DisplayPowerService(platform, _log);
        service.UpdateSystemSleepAssertion(true, true, true);
        platform.ExecutionCalls.Clear();

        service.Dispose();

        Assert.Equal(DisplayPowerService.EsContinuous, Assert.Single(platform.ExecutionCalls));
    }

    public void Dispose()
    {
        _log.Dispose();
        try { Directory.Delete(_tempDir, recursive: true); } catch { }
    }
}
