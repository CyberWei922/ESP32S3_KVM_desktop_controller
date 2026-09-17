using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Networking;
using KVMBridgeWindows.Protocol;
using Xunit;

namespace KVMBridgeWindows.Tests;

public class CommandDispatcherTests : IDisposable
{
    private const string DeviceId = "win-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    private readonly string _tempDir;
    private readonly AppLogger _log;
    private readonly ConfigurationService _config;
    private readonly ProtocolCodec _codec;
    private readonly FakeDdcService _ddc = new();
    private readonly FakeMonitorDiscoveryService _discovery = new();
    private readonly FakeDisplayPowerPlatform _platform = new();
    private readonly DisplayPowerService _displayPower;

    public CommandDispatcherTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "kvmcmd_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
        _log = new AppLogger(Path.Combine(_tempDir, "logs"), LogLevel.Debug, 1);
        _config = new ConfigurationService(_tempDir, _log);
        _config.Load();
        _codec = new ProtocolCodec(DeviceId, _log);
        _displayPower = new DisplayPowerService(_platform, _log);
    }

    private CommandDispatcher MakeDispatcher() =>
        new(_config, _ddc, _discovery, _codec, _displayPower, _log);

    private static IncomingCommand MakeCmd(string action, string requestId = "req-1", string? target = null) => new()
    {
        Type = "command",
        ProtocolVersion = 1,
        RequestId = requestId,
        TargetDeviceId = DeviceId,
        Action = action,
        Target = target
    };

    [Fact]
    public void KvmDisabled_ReturnsOriginalV1Shape()
    {
        var result = Assert.IsType<CommandResultMessage>(
            MakeDispatcher().Execute(MakeCmd("switch_display", target: "mac")));
        Assert.False(result.Success);
        Assert.False(result.WriteSucceeded);
        Assert.Equal(6, result.RequestedInput);
        Assert.Equal("kvm_disabled", result.Error);
    }

    [Theory]
    [InlineData("mac", 6)]
    [InlineData("windows", 7)]
    public void SwitchDisplay_PreservesTargetMapping(string target, int expectedInput)
    {
        var result = Assert.IsType<CommandResultMessage>(
            MakeDispatcher().Execute(MakeCmd("switch_display", $"r-{target}", target)));
        Assert.Equal(expectedInput, result.RequestedInput);
    }

    [Fact]
    public void DisplayPowerDisabled_ReturnsStableFailure()
    {
        var result = Assert.IsType<DisplayPowerCommandResultMessage>(
            MakeDispatcher().Execute(MakeCmd("display_sleep")));
        Assert.False(result.Success);
        Assert.Equal("unchanged", result.DisplayPowerState);
        Assert.Equal("display_power_disabled", result.Error);
        Assert.Empty(_platform.MonitorCalls);
    }

    [Fact]
    public void DisplaySleep_SuccessAndDuplicate_InvokePlatformOnce()
    {
        _config.UpdateAndSave(c =>
        {
            c.Connection.Enabled = true;
            c.DisplayPower.Enabled = true;
        });
        Assert.True(_displayPower.UpdateSystemSleepAssertion(true, true, true));
        var dispatcher = MakeDispatcher();
        var first = Assert.IsType<DisplayPowerCommandResultMessage>(
            dispatcher.Execute(MakeCmd("display_sleep", "dup-sleep")));
        var duplicate = Assert.IsType<DisplayPowerCommandResultMessage>(
            dispatcher.Execute(MakeCmd("display_sleep", "dup-sleep")));

        Assert.True(first.Success);
        Assert.Equal("sleep_requested", first.DisplayPowerState);
        Assert.Null(first.Error);
        Assert.Same(first, duplicate);
        Assert.Single(_platform.MonitorCalls);
    }

    [Fact]
    public void SleepAssertionFailure_StopsDisplaySleep()
    {
        _platform.ExecutionResult = PlatformCallResult.Failure(5);
        _config.UpdateAndSave(c =>
        {
            c.Connection.Enabled = true;
            c.DisplayPower.Enabled = true;
        });
        Assert.False(_displayPower.UpdateSystemSleepAssertion(true, true, true));

        var result = Assert.IsType<DisplayPowerCommandResultMessage>(
            MakeDispatcher().Execute(MakeCmd("display_sleep")));

        Assert.False(result.Success);
        Assert.Equal("system_sleep_not_prevented", result.Error);
        Assert.Empty(_platform.MonitorCalls);
    }

    [Fact]
    public void UnknownAction_ReturnsUnsupportedActionWithoutExecution()
    {
        var result = Assert.IsType<DisplayPowerCommandResultMessage>(
            MakeDispatcher().Execute(MakeCmd("future_action")));
        Assert.False(result.Success);
        Assert.Equal("future_action", result.Action);
        Assert.Equal("unsupported_action", result.Error);
        Assert.Empty(_platform.MonitorCalls);
    }

    [Fact]
    public async Task SwitchSleepWake_ShareOneSerialScheduler()
    {
        var tracker = new ConcurrencyTracker();
        _ddc.Tracker = tracker;
        _ddc.DelayMilliseconds = 40;
        _platform.Tracker = tracker;
        _platform.DelayMilliseconds = 40;
        _platform.MonitorResult = PlatformCallResult.Failure(31);
        _displayPower.RequestSleep(3000);
        _platform.MonitorCalls.Clear();
        _config.UpdateAndSave(c =>
        {
            c.Kvm.DisplayId = "mon-test-display";
            c.Kvm.Enabled = true;
            c.DisplayPower.Enabled = true;
            c.DisplayPower.PreventSystemSleepWhileEnabled = false;
        });
        var dispatcher = MakeDispatcher();

        var tasks = new[]
        {
            Task.Run(() => dispatcher.Execute(MakeCmd("switch_display", "serial-switch", "mac"))),
            Task.Run(() => dispatcher.Execute(MakeCmd("display_sleep", "serial-sleep"))),
            Task.Run(() => dispatcher.Execute(MakeCmd("display_wake", "serial-wake")))
        };
        await Task.WhenAll(tasks);

        Assert.Equal(1, tracker.MaxActive);
        Assert.Equal(1, _ddc.CallCount);
        Assert.Equal(2, _platform.MonitorCalls.Count);
    }

    public void Dispose()
    {
        _displayPower.Dispose();
        _log.Dispose();
        try { Directory.Delete(_tempDir, recursive: true); } catch { }
    }
}
