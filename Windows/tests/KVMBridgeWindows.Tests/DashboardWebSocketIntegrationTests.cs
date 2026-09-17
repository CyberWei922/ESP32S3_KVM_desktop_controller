using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Dashboard;
using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Metrics;
using KVMBridgeWindows.Networking;
using KVMBridgeWindows.Protocol;
using Xunit;

namespace KVMBridgeWindows.Tests;

public sealed class DashboardWebSocketIntegrationTests
{
    private const string DeviceId = "win-dashboard-integration";

    [Fact]
    public async Task HelloIsFirst_AndDashboardUsesIndependentSequenceAndSingleFrames()
    {
        var tempDir = Path.Combine(Path.GetTempPath(), "kvm-dashboard-ws-" + Guid.NewGuid().ToString("N"));
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
            c.Dashboard.Enabled = true;
            c.Dashboard.SendIntervalSeconds = 1;
        });
        using var telemetry = new TelemetryService(DeviceId, config, new CpuTemperatureReader(log), new MemoryReader(log), log);
        using var displayPower = new DisplayPowerService(new FakeDisplayPowerPlatform(), log);
        var codec = new ProtocolCodec(DeviceId, log);
        var dispatcher = new CommandDispatcher(config, new FakeDdcService(), new FakeMonitorDiscoveryService(), codec, displayPower, log);
        var source = new StaticDashboardSource();
        var factory = new DashboardMessageFactory(DeviceId);
        using var client = new WebSocketService(config, telemetry, codec, dispatcher, displayPower, source, factory, log);

        try
        {
            client.Start();
            var context = await listener.GetContextAsync().WaitAsync(TimeSpan.FromSeconds(10));
            var webSocketContext = await context.AcceptWebSocketAsync(null);
            using var server = webSocketContext.WebSocket;

            var first = await ReceiveSingleFrame(server);
            using (var hello = JsonDocument.Parse(first))
                Assert.Equal("hello", hello.RootElement.GetProperty("type").GetString());

            var dashboardSequences = new List<ulong>();
            var telemetrySeen = false;
            for (var i = 0; i < 20 && dashboardSequences.Count < 2; i++)
            {
                using var message = JsonDocument.Parse(await ReceiveSingleFrame(server));
                var type = message.RootElement.GetProperty("type").GetString();
                if (type == "telemetry") telemetrySeen = true;
                if (type == "dashboard") dashboardSequences.Add(message.RootElement.GetProperty("sequence").GetUInt64());
            }

            Assert.True(telemetrySeen);
            Assert.Equal([(ulong)0, (ulong)1], dashboardSequences);
        }
        finally
        {
            client.Stop();
            listener.Stop();
            try { Directory.Delete(tempDir, true); } catch { }
        }
    }

    private static int ReserveLocalPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        return port;
    }

    private static async Task<string> ReceiveSingleFrame(WebSocket socket)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var buffer = new byte[4096];
        var result = await socket.ReceiveAsync(buffer, timeout.Token);
        Assert.Equal(WebSocketMessageType.Text, result.MessageType);
        Assert.True(result.EndOfMessage);
        return Encoding.UTF8.GetString(buffer, 0, result.Count);
    }

    private sealed class StaticDashboardSource : IDashboardSnapshotSource
    {
        public DashboardSnapshot GetSnapshot() => new(null, null, DashboardErrors.Loading);
    }
}
