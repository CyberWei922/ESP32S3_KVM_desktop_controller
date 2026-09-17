using System;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Dashboard;
using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Metrics;
using KVMBridgeWindows.Protocol;

namespace KVMBridgeWindows.Networking;

public enum ConnectionState { Disabled, Connecting, Connected, Reconnecting, Faulted }

public sealed class WebSocketService : IDisposable
{
    private readonly ConfigurationService _config;
    private readonly TelemetryService _telemetry;
    private readonly ProtocolCodec _codec;
    private readonly CommandDispatcher _dispatcher;
    private readonly DisplayPowerService _displayPower;
    private readonly IDashboardSnapshotSource? _dashboard;
    private readonly DashboardMessageFactory? _dashboardFactory;
    private readonly AppLogger _log;

    private CancellationTokenSource _cts = new();
    private Task? _loopTask;
    private ConnectionState _state = ConnectionState.Disabled;
    private bool _disposed;

    // Single-writer channel: priority slot (command results) + telemetry slot
    // We use a bounded channel; telemetry overwrites the pending slot, results go first
    private readonly Channel<(string Json, bool IsResult)> _sendChannel =
        Channel.CreateBounded<(string, bool)>(new BoundedChannelOptions(64)
        {
            FullMode = BoundedChannelFullMode.DropOldest,
            SingleReader = true,
            SingleWriter = false
        });

    // Latest pending telemetry (replaces previous unsent one)
    private string? _pendingTelemetry;
    private readonly object _telLock = new();

    public ConnectionState State { get { lock (this) return _state; } }
    public event Action<ConnectionState>? StateChanged;

    public WebSocketService(ConfigurationService config, TelemetryService telemetry,
        ProtocolCodec codec, CommandDispatcher dispatcher, DisplayPowerService displayPower, AppLogger log)
        : this(config, telemetry, codec, dispatcher, displayPower, null, null, log)
    {
    }

    public WebSocketService(ConfigurationService config, TelemetryService telemetry,
        ProtocolCodec codec, CommandDispatcher dispatcher, DisplayPowerService displayPower,
        IDashboardSnapshotSource? dashboard, DashboardMessageFactory? dashboardFactory, AppLogger log)
    {
        _config = config;
        _telemetry = telemetry;
        _codec = codec;
        _dispatcher = dispatcher;
        _displayPower = displayPower;
        _dashboard = dashboard;
        _dashboardFactory = dashboardFactory;
        _log = log;
    }

    public void Start()
    {
        if (_disposed) return;
        var cfg = _config.Current;
        if (!cfg.Connection.Enabled)
        {
            SetState(ConnectionState.Disabled);
            return;
        }
        _cts = new CancellationTokenSource();
        _loopTask = Task.Run(() => RunLoop(_cts.Token));
    }

    public void Restart()
    {
        _log.Info("Manual reconnect requested.");
        _cts.Cancel();
        // Start will be re-triggered via TrayApplicationContext after cancel completes
        Task.Run(async () =>
        {
            try { if (_loopTask != null) await _loopTask; } catch { }
            Start();
        });
    }

    public void Stop()
    {
        _cts.Cancel();
    }

    public void EnqueueTelemetry(string json)
    {
        lock (_telLock) { _pendingTelemetry = json; }
    }

    public void EnqueueResult(string json)
    {
        _sendChannel.Writer.TryWrite((json, true));
    }

    private async Task RunLoop(CancellationToken ct)
    {
        int[] backoffSeconds = { 1, 2, 4, 8, 16, 30 };
        int backoffIdx = 0;

        while (!ct.IsCancellationRequested)
        {
            var cfg = _config.Current;
            if (!cfg.Connection.Enabled)
            {
                SetState(ConnectionState.Disabled);
                await Task.Delay(2000, ct).ConfigureAwait(false);
                continue;
            }

            SetState(ConnectionState.Connecting);
            using var ws = new ClientWebSocket();
            ws.Options.KeepAliveInterval = TimeSpan.FromMilliseconds(cfg.Connection.KeepaliveIntervalMs);
            ws.Options.Proxy = null;

            try
            {
                _log.Info($"Connecting to {cfg.Connection.WebSocketUrl}...");
                await ws.ConnectAsync(new Uri(cfg.Connection.WebSocketUrl), ct).ConfigureAwait(false);
                _log.Info("WebSocket connected.");

                // Reset sequence and send Hello
                _telemetry.ResetSequence();
                _dashboardFactory?.ResetSequence();
                var hello = _codec.EncodeHello();
                await SendText(ws, hello, ct).ConfigureAwait(false);
                _log.Info("Hello sent.");
                SetState(ConnectionState.Connected);
                backoffIdx = 0;

                // Run send + receive concurrently
                using var sessionCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
                var sendTask = SendLoop(ws, cfg, sessionCts.Token);
                var recvTask = ReceiveLoop(ws, sessionCts.Token);

                var finished = await Task.WhenAny(sendTask, recvTask).ConfigureAwait(false);
                sessionCts.Cancel();
                try { await Task.WhenAll(sendTask, recvTask); } catch { }
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                _log.Warning($"Connection error: {ex.Message}");
            }

            if (ct.IsCancellationRequested) break;

            int delay = backoffSeconds[Math.Min(backoffIdx, backoffSeconds.Length - 1)];
            backoffIdx++;
            _log.Info($"Reconnecting in {delay}s...");
            SetState(ConnectionState.Reconnecting);
            try { await Task.Delay(TimeSpan.FromSeconds(delay), ct); } catch { }
        }

        SetState(ConnectionState.Disabled);
        _log.Info("WebSocket loop exited.");
    }

    private async Task SendLoop(ClientWebSocket ws, AppConfig cfg, CancellationToken ct)
    {
        var telemetryInterval = TimeSpan.FromMilliseconds(Math.Max(cfg.Connection.TelemetryIntervalMs, 500));
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(100));
        var nextTelemetry = DateTimeOffset.UtcNow;
        var nextDashboard = DateTimeOffset.UtcNow;
        int sendCount = 0;
        int dashboardSendCount = 0;

        while (!ct.IsCancellationRequested)
        {
            // Always drain priority result messages first
            while (_sendChannel.Reader.TryRead(out var item))
            {
                if (!item.IsResult) continue; // handled below
                await SendText(ws, item.Json, ct).ConfigureAwait(false);
            }

            var now = DateTimeOffset.UtcNow;

            // Telemetry and dashboard clocks are independent; neither performs network fetches here.
            if (_config.Current.Telemetry.Enabled && now >= nextTelemetry)
            {
                var snapshot = _telemetry.Collect();
                var json = _codec.EncodeTelemetry(snapshot);
                await SendText(ws, json, ct).ConfigureAwait(false);
                if (++sendCount % 5 == 1)
                {
                    _log.Info($"[Telemetry #{snapshot.Sequence}] Sent ({json.Length}B): CPU={snapshot.Metrics.CpuTemperature.Value}°C (source='{snapshot.Metrics.CpuTemperature.Source}'), RAM={snapshot.Metrics.MemoryUsage.Value}% (source='{snapshot.Metrics.MemoryUsage.Source}')");
                }
                if (_displayPower.State == DisplayOutputState.OutputSleeping && sendCount % 30 == 1)
                    _log.Info("Display sleep has been requested; WebSocket remains online and telemetry continues.");
            }
            if (now >= nextTelemetry)
                nextTelemetry = now + telemetryInterval;

            var dashboardConfig = _config.Current.Dashboard;
            if (dashboardConfig.Enabled && _dashboard != null && _dashboardFactory != null && now >= nextDashboard)
            {
                var json = _dashboardFactory.CreateJson(_dashboard.GetSnapshot(), now.ToUnixTimeMilliseconds());
                await SendText(ws, json, ct).ConfigureAwait(false);
                if (++dashboardSendCount % 6 == 1)
                    _log.Info($"[Dashboard #{dashboardSendCount - 1}] Sent ({Encoding.UTF8.GetByteCount(json)}B).");
            }
            if (now >= nextDashboard)
                nextDashboard = now + TimeSpan.FromSeconds(Math.Max(dashboardConfig.SendIntervalSeconds, 1));

            try { await timer.WaitForNextTickAsync(ct); } catch { break; }
        }
    }

    private async Task ReceiveLoop(ClientWebSocket ws, CancellationToken ct)
    {
        var buf = new byte[4096];
        var sb = new StringBuilder();
        while (!ct.IsCancellationRequested && ws.State == WebSocketState.Open)
        {
            sb.Clear();
            WebSocketReceiveResult result;
            do
            {
                result = await ws.ReceiveAsync(new ArraySegment<byte>(buf), ct).ConfigureAwait(false);
                if (result.MessageType == WebSocketMessageType.Close) return;
                if (result.MessageType != WebSocketMessageType.Text) continue;
                sb.Append(Encoding.UTF8.GetString(buf, 0, result.Count));
            } while (!result.EndOfMessage);

            var msg = sb.ToString();
            if (msg.Length == 0) continue;

            var cmd = _codec.TryParseCommand(msg, out var parseError);
            if (parseError != null)
                _log.Warning($"Received invalid message: {parseError}");
            if (cmd == null) continue;

            // Keep command receive order and execute through the shared serial dispatcher.
            try
            {
                var cmdResult = await Task.Run(() => _dispatcher.Execute(cmd), ct).ConfigureAwait(false);
                var json = _codec.EncodeCommandResult(cmdResult);
                EnqueueResult(json);
                _log.Info($"Command result queued: action={cmd.Action} success={cmdResult.Success} request_id={cmd.RequestId}");
            }
            catch (Exception ex) { _log.Error("Command execution failed.", ex); }
        }
    }

    private static async Task SendText(ClientWebSocket ws, string text, CancellationToken ct)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        await ws.SendAsync(new ArraySegment<byte>(bytes), WebSocketMessageType.Text, true, ct).ConfigureAwait(false);
    }

    private void SetState(ConnectionState s)
    {
        lock (this) { _state = s; }
        try { StateChanged?.Invoke(s); } catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _cts.Cancel();
        _cts.Dispose();
    }
}
