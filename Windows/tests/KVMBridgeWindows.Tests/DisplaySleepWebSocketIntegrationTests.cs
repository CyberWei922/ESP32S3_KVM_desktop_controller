using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Metrics;
using KVMBridgeWindows.Networking;
using KVMBridgeWindows.Protocol;
using Xunit;

namespace KVMBridgeWindows.Tests;

public sealed class DisplaySleepWebSocketIntegrationTests
{
    private const string DeviceId = "win-11111111-2222-3333-4444-555555555555";

    [Fact]
    public async Task TelemetryContinuesAfterDisplaySleepCommand()
    {
        var tempDir = Path.Combine(Path.GetTempPath(), "kvmws_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tempDir);
        using var log = new AppLogger(Path.Combine(tempDir, "logs"), LogLevel.Debug, 1);
        var port = ReserveLocalPort();
        using var listener = new HttpListener();
        listener.Prefixes.Add($"http://127.0.0.1:{port}/statsforkvm/");
        listener.Start();

        var config = new ConfigurationService(tempDir, log);
        config.Load();
        config.UpdateAndSave(c =>
        {
            c.Connection.Enabled = true;
            c.Connection.Host = "127.0.0.1";
            c.Connection.Port = port;
            c.Connection.Path = "/statsforkvm/";
            c.Connection.TelemetryIntervalMs = 500;
            c.Connection.KeepaliveIntervalMs = 5000;
            c.Telemetry.Enabled = true;
            c.DisplayPower.Enabled = true;
            c.DisplayPower.PreventSystemSleepWhileEnabled = false;
        });

        var cpu = new CpuTemperatureReader(log);
        var memory = new MemoryReader(log);
        using var telemetry = new TelemetryService(DeviceId, config, cpu, memory, log);
        telemetry.Start(500);
        var platform = new FakeDisplayPowerPlatform();
        using var displayPower = new DisplayPowerService(platform, log);
        var codec = new ProtocolCodec(DeviceId, log);
        var dispatcher = new CommandDispatcher(
            config,
            new FakeDdcService(),
            new FakeMonitorDiscoveryService(),
            codec,
            displayPower,
            log);
        using var client = new WebSocketService(config, telemetry, codec, dispatcher, displayPower, log);

        try
        {
            client.Start();
            var context = await listener.GetContextAsync().WaitAsync(TimeSpan.FromSeconds(10));
            var webSocketContext = await context.AcceptWebSocketAsync(null);
            using var server = webSocketContext.WebSocket;

            using (var hello = JsonDocument.Parse(await ReceiveText(server)))
                Assert.Equal("hello", hello.RootElement.GetProperty("type").GetString());

            var sleepCommand = $@"{{""type"":""command"",""protocol_version"":1,""request_id"":""sleep-integration"",""target_device_id"":""{DeviceId}"",""action"":""display_sleep""}}";
            await SendText(server, sleepCommand);

            JsonDocument? result = null;
            for (var i = 0; i < 8 && result == null; i++)
            {
                var message = JsonDocument.Parse(await ReceiveText(server));
                if (message.RootElement.GetProperty("type").GetString() == "command_result")
                    result = message;
                else
                    message.Dispose();
            }

            Assert.NotNull(result);
            using (result)
            {
                Assert.True(result!.RootElement.GetProperty("success").GetBoolean());
                Assert.Equal("sleep_requested", result.RootElement.GetProperty("display_power_state").GetString());
            }
            Assert.Equal(DisplayOutputState.OutputSleeping, displayPower.State);

            JsonDocument? telemetryAfterSleep = null;
            for (var i = 0; i < 8 && telemetryAfterSleep == null; i++)
            {
                var message = JsonDocument.Parse(await ReceiveText(server));
                if (message.RootElement.GetProperty("type").GetString() == "telemetry")
                    telemetryAfterSleep = message;
                else
                    message.Dispose();
            }

            Assert.NotNull(telemetryAfterSleep);
            using (telemetryAfterSleep)
            {
                Assert.True(telemetryAfterSleep!.RootElement.GetProperty("sequence").GetUInt64() > 0);
            }
            Assert.Equal(ConnectionState.Connected, client.State);
        }
        finally
        {
            client.Stop();
            listener.Stop();
            try { Directory.Delete(tempDir, recursive: true); } catch { }
        }
    }

    private static int ReserveLocalPort()
    {
        var socket = new TcpListener(IPAddress.Loopback, 0);
        socket.Start();
        var port = ((IPEndPoint)socket.LocalEndpoint).Port;
        socket.Stop();
        return port;
    }

    private static async Task<string> ReceiveText(WebSocket socket)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var buffer = new byte[8192];
        using var stream = new MemoryStream();
        WebSocketReceiveResult result;
        do
        {
            result = await socket.ReceiveAsync(buffer, timeout.Token);
            Assert.Equal(WebSocketMessageType.Text, result.MessageType);
            stream.Write(buffer, 0, result.Count);
        } while (!result.EndOfMessage);
        return Encoding.UTF8.GetString(stream.ToArray());
    }

    private static async Task SendText(WebSocket socket, string text)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        await socket.SendAsync(bytes, WebSocketMessageType.Text, true, CancellationToken.None);
    }
}
