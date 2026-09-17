using System.Text.Json;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Protocol;
using Xunit;

namespace KVMBridgeWindows.Tests;

public class ProtocolTests
{
    private const string DeviceId = "win-12345678-1234-1234-1234-123456789abc";
    private readonly ProtocolCodec _codec;
    private readonly AppLogger _log;

    public ProtocolTests()
    {
        _log = new AppLogger(Path.Combine(Path.GetTempPath(), "kvmtest_logs"), LogLevel.Debug, 1);
        _codec = new ProtocolCodec(DeviceId, _log);
    }

    [Fact]
    public void Hello_HasCorrectFields()
    {
        var json = _codec.EncodeHello();
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;
        Assert.Equal("hello", r.GetProperty("type").GetString());
        Assert.Equal(1, r.GetProperty("protocol_version").GetInt32());
        Assert.Equal(DeviceId, r.GetProperty("device_id").GetString());
        Assert.Equal("windows", r.GetProperty("platform").GetString());
        Assert.Equal("KVMBridgeWindows", r.GetProperty("app").GetString());
        Assert.Equal(
            ["switch_display", "display_sleep", "display_wake", "dashboard_data"],
            r.GetProperty("capabilities").EnumerateArray().Select(x => x.GetString()!).ToArray());
    }

    [Fact]
    public void ValidCommand_ParsedCorrectly()
    {
        var json = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":""esp32-00000001"",""target_device_id"":""{DeviceId}"",""action"":""switch_display"",""target"":""mac""}}";
        var cmd = _codec.TryParseCommand(json, out var err);
        Assert.Null(err);
        Assert.NotNull(cmd);
        Assert.Equal("mac", cmd!.Target);
        Assert.Equal("esp32-00000001", cmd.RequestId);
    }

    [Fact]
    public void Command_WrongDeviceId_ReturnsNull()
    {
        var json = @"{""type"":""command"",""protocol_version"":1,""request_id"":""r1"",""target_device_id"":""win-other"",""action"":""switch_display"",""target"":""mac""}";
        var cmd = _codec.TryParseCommand(json, out _);
        Assert.Null(cmd);
    }

    [Fact]
    public void Command_WrongVersion_ReturnsError()
    {
        var json = $@"{{""type"":""command"",""protocol_version"":2,""request_id"":""r1"",""target_device_id"":""{DeviceId}"",""action"":""switch_display"",""target"":""mac""}}";
        var cmd = _codec.TryParseCommand(json, out var err);
        Assert.Null(cmd);
        Assert.NotNull(err);
    }

    [Fact]
    public void Command_UnknownTarget_ReturnsError()
    {
        var json = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":""r1"",""target_device_id"":""{DeviceId}"",""action"":""switch_display"",""target"":""linux""}}";
        var cmd = _codec.TryParseCommand(json, out var err);
        Assert.Null(cmd);
        Assert.NotNull(err);
    }

    [Fact]
    public void Command_EmptyRequestId_ReturnsError()
    {
        var json = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":"""",""target_device_id"":""{DeviceId}"",""action"":""switch_display"",""target"":""mac""}}";
        var cmd = _codec.TryParseCommand(json, out var err);
        Assert.Null(cmd);
        Assert.NotNull(err);
    }

    [Fact]
    public void Command_TooLong_ReturnsError()
    {
        var big = new string('x', 3000);
        var cmd = _codec.TryParseCommand(big, out var err);
        Assert.Null(cmd);
        Assert.NotNull(err);
    }

    [Fact]
    public void SuccessUnconfirmed_Fields()
    {
        var r = _codec.BuildSuccessUnconfirmed("rid-1", 6);
        Assert.True(r.Success);
        Assert.True(r.WriteSucceeded);
        Assert.False(r.Confirmed);
        Assert.Equal(6, r.RequestedInput);
        Assert.Null(r.ReadBackInput);
        Assert.Equal("input_written_but_unconfirmed", r.Error);
        // success must equal write_succeeded
        Assert.Equal(r.Success, r.WriteSucceeded);
        // confirmed=false => error must be non-null
        Assert.False(r.Confirmed);
        Assert.NotNull(r.Error);
        // error code <= 63 bytes
        Assert.True(System.Text.Encoding.UTF8.GetByteCount(r.Error) <= 63);
    }

    [Fact]
    public void SuccessConfirmed_Fields()
    {
        var r = _codec.BuildSuccessConfirmed("rid-2", 7, 7);
        Assert.True(r.Success);
        Assert.True(r.WriteSucceeded);
        Assert.True(r.Confirmed);
        Assert.Equal(7, r.RequestedInput);
        Assert.Equal(7, r.ReadBackInput);
        Assert.Null(r.Error);
    }

    [Fact]
    public void FailureResult_Fields()
    {
        var r = _codec.BuildFailureResult("rid-3", 6, "ddc_write_failed");
        Assert.False(r.Success);
        Assert.False(r.WriteSucceeded);
        Assert.False(r.Confirmed);
        // success must equal write_succeeded
        Assert.Equal(r.Success, r.WriteSucceeded);
        Assert.True(System.Text.Encoding.UTF8.GetByteCount(r.Error!) <= 63);
    }

    [Fact]
    public void NonCommand_TypeIgnored()
    {
        var json = @"{""type"":""telemetry"",""protocol_version"":1}";
        var cmd = _codec.TryParseCommand(json, out _);
        Assert.Null(cmd);
    }

    [Theory]
    [InlineData("[]")]
    [InlineData("{\"type\":1}")]
    public void NonObjectOrInvalidType_IsRejectedWithoutThrowing(string json)
    {
        var exception = Record.Exception(() => _codec.TryParseCommand(json, out _));
        Assert.Null(exception);
        Assert.Null(_codec.TryParseCommand(json, out var error));
        Assert.NotNull(error);
    }

    [Fact]
    public void BothTargets_Parsed()
    {
        foreach (var target in new[] { "mac", "windows" })
        {
            var json = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":""r"",""target_device_id"":""{DeviceId}"",""action"":""switch_display"",""target"":""{target}""}}";
            var cmd = _codec.TryParseCommand(json, out _);
            Assert.NotNull(cmd);
            Assert.Equal(target, cmd!.Target);
        }
    }

    [Theory]
    [InlineData("display_sleep")]
    [InlineData("display_wake")]
    public void DisplayPowerCommand_ParsesWithoutInputFields(string action)
    {
        var json = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":""power-1"",""target_device_id"":""{DeviceId}"",""action"":""{action}""}}";
        var command = _codec.TryParseCommand(json, out var error);
        Assert.Null(error);
        Assert.NotNull(command);
        Assert.Equal(action, command!.Action);
        Assert.Null(command.Target);
    }

    [Theory]
    [InlineData("target", "\"mac\"")]
    [InlineData("requested_input", "6")]
    [InlineData("vcp_code", "96")]
    public void DisplayPowerCommand_RejectsDisplayInputFields(string field, string value)
    {
        var json = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":""power-2"",""target_device_id"":""{DeviceId}"",""action"":""display_sleep"",""{field}"":{value}}}";
        Assert.Null(_codec.TryParseCommand(json, out var error));
        Assert.NotNull(error);
    }

    [Fact]
    public void UnknownAction_WithCompleteIdentity_IsReturnedForUnsupportedResult()
    {
        var json = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":""future-1"",""target_device_id"":""{DeviceId}"",""action"":""future_action""}}";
        var command = _codec.TryParseCommand(json, out var error);
        Assert.Null(error);
        Assert.NotNull(command);
        Assert.Equal("future_action", command!.Action);
    }

    [Theory]
    [InlineData("display_sleep", true, "sleep_requested", null)]
    [InlineData("display_wake", true, "wake_requested", null)]
    [InlineData("display_wake", false, "unchanged", "display_wake_failed")]
    public void DisplayPowerResult_MatchesContract(
        string action,
        bool success,
        string state,
        string? error)
    {
        var result = _codec.BuildDisplayPowerResult("power-result", action, success, state, error);
        var json = _codec.EncodeCommandResult(result);
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        Assert.Equal(
            ["type", "protocol_version", "request_id", "device_id", "action", "success", "display_power_state", "error"],
            root.EnumerateObject().Select(p => p.Name).ToArray());
        Assert.Equal("command_result", root.GetProperty("type").GetString());
        Assert.Equal(1, root.GetProperty("protocol_version").GetInt32());
        Assert.Equal("power-result", root.GetProperty("request_id").GetString());
        Assert.Equal(DeviceId, root.GetProperty("device_id").GetString());
        Assert.Equal(action, root.GetProperty("action").GetString());
        Assert.Equal(success, root.GetProperty("success").GetBoolean());
        Assert.Equal(state, root.GetProperty("display_power_state").GetString());
        if (error == null)
            Assert.Equal(JsonValueKind.Null, root.GetProperty("error").ValueKind);
        else
            Assert.Equal(error, root.GetProperty("error").GetString());

        Assert.False(root.TryGetProperty("target", out _));
        Assert.False(root.TryGetProperty("requested_input", out _));
        Assert.False(root.TryGetProperty("write_succeeded", out _));
    }
}
