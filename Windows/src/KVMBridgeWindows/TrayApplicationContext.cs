using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Ddc;
using KVMBridgeWindows.Dashboard;
using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Metrics;
using KVMBridgeWindows.Networking;
using KVMBridgeWindows.Protocol;
using KVMBridgeWindows.Startup;
using KVMBridgeWindows.Settings;

namespace KVMBridgeWindows;

public sealed class TrayApplicationContext : ApplicationContext, IDisposable
{
    private readonly ConfigurationService _config;
    private readonly DeviceIdentityService _identity;
    private readonly StartupService _startup;
    private readonly MonitorDiscoveryService _discovery;
    private readonly DdcService _ddc;
    private readonly TelemetryService _telemetry;
    private readonly WebSocketService _ws;
    private readonly ProtocolCodec _codec;
    private readonly CommandDispatcher _dispatcher;
    private readonly DisplayPowerService _displayPower;
    private readonly DashboardCoordinator _dashboard;
    private readonly DashboardSettingsStore _dashboardSettings;
    private readonly AppLogger _log;

    private readonly NotifyIcon _tray;
    private readonly ContextMenuStrip _menu;

    // Cached icons
    private static readonly Icon IconGray = CreateColorIcon(Color.Gray);
    private static readonly Icon IconYellow = CreateColorIcon(Color.Yellow);
    private static readonly Icon IconGreen = CreateColorIcon(Color.Green);
    private static readonly Icon IconRed = CreateColorIcon(Color.Red);

    // Menu items we need to update
    private ToolStripMenuItem _miStatus = null!;
    private ToolStripMenuItem _miDataStatus = null!;
    private ToolStripMenuItem _miDisplayPowerStatus = null!;
    private ToolStripMenuItem _miKeepOnlineStatus = null!;
    private ToolStripMenuItem _miWeatherStatus = null!;
    private ToolStripMenuItem _miEnableConnection = null!;
    private ToolStripMenuItem _miEnableTelemetry = null!;
    private ToolStripMenuItem _miEnableKvm = null!;
    private ToolStripMenuItem _miEnableDisplayPower = null!;
    private ToolStripMenuItem _miStartWithWindows = null!;
    private ToolStripMenuItem _miMonitorMenu = null!;
    private ToolStripMenuItem _miTestMac = null!;
    private ToolStripMenuItem _miTestWin = null!;
    private ToolStripMenuItem _miTestDisplaySleep = null!;
    private ToolStripMenuItem _miTestDisplayWake = null!;

    private bool _menuUpdating;
    private System.Windows.Forms.Timer? _uiTimer;
    private readonly SynchronizationContext? _syncContext;

    public TrayApplicationContext(
        ConfigurationService config, DeviceIdentityService identity, StartupService startup,
        MonitorDiscoveryService discovery, DdcService ddc, TelemetryService telemetry,
        WebSocketService ws, ProtocolCodec codec, CommandDispatcher dispatcher,
        DisplayPowerService displayPower, DashboardCoordinator dashboard,
        DashboardSettingsStore dashboardSettings, AppLogger log)
    {
        _config = config; _identity = identity; _startup = startup;
        _discovery = discovery; _ddc = ddc; _telemetry = telemetry;
        _ws = ws; _codec = codec; _log = log;
        _dispatcher = dispatcher; _displayPower = displayPower;
        _dashboard = dashboard; _dashboardSettings = dashboardSettings;
        _syncContext = SynchronizationContext.Current;

        _menu = BuildMenu();
        _tray = new NotifyIcon
        {
            Text = "KVMBridgeWindows",
            Icon = IconGray,
            ContextMenuStrip = _menu,
            Visible = true
        };

        _ws.StateChanged += _ =>
        {
            if (_syncContext != null)
                _syncContext.Post(_ => UpdateMenu(), null);
        };

        _discovery.Scan();
        UpdateMenu();
        _ws.Start();

        _uiTimer = new System.Windows.Forms.Timer { Interval = 1000 };
        _uiTimer.Tick += (_, _) => UpdateMenu();
        _uiTimer.Start();
    }

    private ContextMenuStrip BuildMenu()
    {
        var menu = new ContextMenuStrip();

        _miStatus = new ToolStripMenuItem("ESP32：已禁用") { Enabled = false };
        _miDataStatus = new ToolStripMenuItem("Windows 数据：正常") { Enabled = false };
        _miDisplayPowerStatus = new ToolStripMenuItem("显示输出：活动") { Enabled = false };
        _miKeepOnlineStatus = new ToolStripMenuItem("保持主机在线：未启用") { Enabled = false };
        _miWeatherStatus = new ToolStripMenuItem("天气城市：未配置") { Enabled = false };
        menu.Items.Add(_miStatus);
        menu.Items.Add(_miDataStatus);
        menu.Items.Add(_miDisplayPowerStatus);
        menu.Items.Add(_miKeepOnlineStatus);
        menu.Items.Add(_miWeatherStatus);
        menu.Items.Add(new ToolStripSeparator());

        _miEnableConnection = new ToolStripMenuItem("启用 ESP32 连接") { CheckOnClick = false };
        _miEnableConnection.Click += (_, _) => ToggleConnection();
        menu.Items.Add(_miEnableConnection);

        _miEnableTelemetry = new ToolStripMenuItem("发送系统数据") { CheckOnClick = false };
        _miEnableTelemetry.Click += (_, _) => ToggleTelemetry();
        menu.Items.Add(_miEnableTelemetry);

        _miEnableKvm = new ToolStripMenuItem("启用 KVM / DDC") { CheckOnClick = false };
        _miEnableKvm.Click += (_, _) => ToggleKvm();
        menu.Items.Add(_miEnableKvm);

        _miEnableDisplayPower = new ToolStripMenuItem("显示电源联动") { CheckOnClick = false };
        _miEnableDisplayPower.Click += (_, _) => ToggleDisplayPower();
        menu.Items.Add(_miEnableDisplayPower);

        _miStartWithWindows = new ToolStripMenuItem("随 Windows 启动") { CheckOnClick = false };
        _miStartWithWindows.Click += (_, _) => ToggleStartup();
        menu.Items.Add(_miStartWithWindows);

        var miWeatherSettings = new ToolStripMenuItem("天气与行情设置…");
        miWeatherSettings.Click += async (_, _) => await ConfigureWeatherAsync();
        menu.Items.Add(miWeatherSettings);

        var miRefreshDashboard = new ToolStripMenuItem("立即刷新天气与行情");
        miRefreshDashboard.Click += async (_, _) => await RefreshDashboardAsync();
        menu.Items.Add(miRefreshDashboard);

        menu.Items.Add(new ToolStripSeparator());

        _miMonitorMenu = new ToolStripMenuItem("目标显示器");
        menu.Items.Add(_miMonitorMenu);

        _miTestMac = new ToolStripMenuItem("测试：切换显示器到 Mac（HDMI 2 / 6）");
        _miTestMac.Click += (_, _) => TestDdc(6, "Mac");
        menu.Items.Add(_miTestMac);

        _miTestWin = new ToolStripMenuItem("测试：切换显示器到 Windows（DP / 7）");
        _miTestWin.Click += (_, _) => TestDdc(7, "Windows");
        menu.Items.Add(_miTestWin);

        _miTestDisplaySleep = new ToolStripMenuItem("测试关闭显示输出（不会关闭 Windows）");
        _miTestDisplaySleep.Click += (_, _) => TestDisplaySleep();
        menu.Items.Add(_miTestDisplaySleep);

        _miTestDisplayWake = new ToolStripMenuItem("测试恢复显示输出");
        _miTestDisplayWake.Click += (_, _) => TestDisplayPower("display_wake");
        menu.Items.Add(_miTestDisplayWake);

        var miReconnect = new ToolStripMenuItem("立即重连 ESP32");
        miReconnect.Click += (_, _) => _ws.Restart();
        menu.Items.Add(miReconnect);

        menu.Items.Add(new ToolStripSeparator());

        var miOpenConfig = new ToolStripMenuItem("打开配置文件");
        miOpenConfig.Click += (_, _) => OpenFile(Path.Combine(GetAppDataDir(), "config.json"));
        menu.Items.Add(miOpenConfig);

        var miOpenLogs = new ToolStripMenuItem("打开日志文件夹");
        miOpenLogs.Click += (_, _) => OpenFolder(Path.Combine(GetAppDataDir(), "logs"));
        menu.Items.Add(miOpenLogs);

        menu.Items.Add(new ToolStripMenuItem("版本 3.0.0") { Enabled = false });
        menu.Items.Add(new ToolStripSeparator());

        var miExit = new ToolStripMenuItem("退出");
        miExit.Click += (_, _) => ExitApp();
        menu.Items.Add(miExit);

        return menu;
    }

    private void UpdateMenu()
    {
        if (_menuUpdating) return;
        _menuUpdating = true;
        try
        {
            var cfg = _config.Current;
            var state = _ws.State;

            _miStatus.Text = "ESP32：" + state switch
            {
                ConnectionState.Connected => "已连接",
                ConnectionState.Connecting => "正在连接",
                ConnectionState.Reconnecting => "等待重连",
                ConnectionState.Disabled => "已禁用",
                _ => "故障"
            };

            var latest = _telemetry.GetLatest();
            bool cpuOk = latest?.Metrics.CpuTemperature.Valid == true;
            bool memOk = latest?.Metrics.MemoryUsage.Valid == true;
            _miDataStatus.Text = (!cpuOk && !memOk) ? "Windows 数据：读取失败"
                : !cpuOk ? "Windows 数据：CPU 温度不可用"
                : !memOk ? "Windows 数据：内存读取失败"
                : "Windows 数据：正常";

            _miDisplayPowerStatus.Text = "显示输出：" + (_displayPower.State switch
            {
                DisplayOutputState.Active => "活动",
                DisplayOutputState.SleepRequested or DisplayOutputState.OutputSleeping => "已请求休眠",
                DisplayOutputState.WakeRequested => "已请求唤醒",
                _ => "错误"
            });

            _miKeepOnlineStatus.Text = !cfg.DisplayPower.Enabled
                ? "保持主机在线：未启用"
                : !cfg.DisplayPower.PreventSystemSleepWhileEnabled
                    ? "保持主机在线：已关闭"
                    : _displayPower.AssertionState switch
                    {
                        SystemSleepAssertionState.Active => "保持主机在线：已生效",
                        SystemSleepAssertionState.Warning => "保持主机在线：警告（建立失败）",
                        _ => cfg.Connection.Enabled ? "保持主机在线：未建立" : "保持主机在线：等待连接启用"
                    };

            _miWeatherStatus.Text = string.IsNullOrWhiteSpace(cfg.Dashboard.CityName)
                ? "天气城市：未配置"
                : $"天气城市：{cfg.Dashboard.CityName}（{cfg.Dashboard.WeatherProvider}）";

            _miEnableConnection.Checked = cfg.Connection.Enabled;
            _miEnableTelemetry.Checked = cfg.Telemetry.Enabled;
            _miEnableKvm.Checked = cfg.Kvm.Enabled;
            _miEnableDisplayPower.Checked = cfg.DisplayPower.Enabled;
            bool canEnableKvm = !string.IsNullOrWhiteSpace(cfg.Kvm.DisplayId);
            _miEnableKvm.Enabled = canEnableKvm;
            if (!canEnableKvm) _miEnableKvm.ToolTipText = "请先选择目标显示器";
            _miStartWithWindows.Checked = _startup.IsEnabled();

            bool hasDisplay = !string.IsNullOrWhiteSpace(cfg.Kvm.DisplayId);
            _miTestMac.Enabled = hasDisplay;
            _miTestWin.Enabled = hasDisplay;
            _miTestDisplaySleep.Enabled = cfg.DisplayPower.Enabled;
            _miTestDisplayWake.Enabled = cfg.DisplayPower.Enabled;

            RebuildMonitorSubMenu(cfg.Kvm.DisplayId);

            _tray.Icon = state switch
            {
                ConnectionState.Connected => IconGreen,
                ConnectionState.Connecting or ConnectionState.Reconnecting => IconYellow,
                ConnectionState.Disabled => IconGray,
                _ => IconRed
            };
        }
        catch (Exception ex)
        {
            _log.Warning($"UpdateMenu error: {ex.Message}");
        }
        finally { _menuUpdating = false; }
    }

    private void RebuildMonitorSubMenu(string selectedId)
    {
        _miMonitorMenu.DropDownItems.Clear();
        foreach (var m in _discovery.Monitors)
        {
            var label = $"{m.FriendlyName} [...{m.IdSuffix}]{(m.DdcAvailable ? "" : " (无DDC)")}";
            var item = new ToolStripMenuItem(label)
            {
                Checked = m.StableId == selectedId,
                Tag = m.StableId
            };
            item.Click += (_, _) => SelectMonitor(m.StableId);
            _miMonitorMenu.DropDownItems.Add(item);
        }
        _miMonitorMenu.DropDownItems.Add(new ToolStripSeparator());
        var rescan = new ToolStripMenuItem("重新扫描");
        rescan.Click += (_, _) => { _discovery.Scan(); UpdateMenu(); };
        _miMonitorMenu.DropDownItems.Add(rescan);
    }

    private void ToggleConnection()
    {
        _config.UpdateAndSave(c => c.Connection.Enabled = !c.Connection.Enabled);
        UpdateSystemSleepAssertion();
        _ws.Stop();
        _ws.Start();
        UpdateMenu();
    }

    private void ToggleTelemetry()
    {
        _config.UpdateAndSave(c => c.Telemetry.Enabled = !c.Telemetry.Enabled);
        UpdateMenu();
    }

    private void ToggleKvm()
    {
        var cfg = _config.Current;
        if (!cfg.Kvm.Enabled && string.IsNullOrWhiteSpace(cfg.Kvm.DisplayId))
        {
            _tray.ShowBalloonTip(3000, "KVM", "请先在[目标显示器]中选择显示器", ToolTipIcon.Warning);
            return;
        }
        _config.UpdateAndSave(c => c.Kvm.Enabled = !c.Kvm.Enabled);
        UpdateMenu();
    }

    private void ToggleDisplayPower()
    {
        _config.UpdateAndSave(c => c.DisplayPower.Enabled = !c.DisplayPower.Enabled);
        UpdateSystemSleepAssertion();
        UpdateMenu();
    }

    private void UpdateSystemSleepAssertion()
    {
        var cfg = _config.Current;
        _displayPower.UpdateSystemSleepAssertion(
            cfg.Connection.Enabled,
            cfg.DisplayPower.Enabled,
            cfg.DisplayPower.PreventSystemSleepWhileEnabled);
    }

    private void ToggleStartup()
    {
        bool current = _startup.IsEnabled();
        _startup.SetEnabled(!current);
        _config.UpdateAndSave(c => c.Startup.StartWithWindows = !current);
        UpdateMenu();
    }

    private async Task ConfigureWeatherAsync()
    {
        using var dialog = new WeatherSettingsDialog(_config.Current.Dashboard);
        if (dialog.ShowDialog() != DialogResult.OK) return;
        try
        {
            var result = await _dashboardSettings.ApplyAsync(dialog.SettingsUpdate);
            if (result.Success)
            {
                _tray.ShowBalloonTip(4000, "天气设置", $"已切换到 {result.City!.Name}，数据已刷新。", ToolTipIcon.Info);
            }
            else
            {
                _tray.ShowBalloonTip(5000, "天气设置未保存", ErrorText(result.Error), ToolTipIcon.Error);
            }
        }
        catch (OperationCanceledException) { }
    }

    private async Task RefreshDashboardAsync()
    {
        try
        {
            await Task.WhenAll(_dashboard.RefreshWeatherNowAsync(), _dashboard.RefreshMarketsNowAsync());
            var snapshot = _dashboard.GetSnapshot();
            var message = snapshot.Error == null ? "天气与行情数据已刷新。" : $"刷新完成：{ErrorText(snapshot.Error)}";
            _tray.ShowBalloonTip(4000, "仪表盘数据", message, snapshot.Error == null ? ToolTipIcon.Info : ToolTipIcon.Warning);
        }
        catch (OperationCanceledException) { }
    }

    private static string ErrorText(string? error) => error switch
    {
        DashboardErrors.CityCodeMissing => "请先配置城市。",
        DashboardErrors.CityNotFound => "未找到城市，原配置已保留。",
        DashboardErrors.QWeatherCredentialsMissing => "请填写 QWeather API Host 和 API Key。",
        DashboardErrors.WeatherFetchFailed => "天气数据获取失败，原缓存已保留。",
        DashboardErrors.WeatherResponseInvalid => "天气响应格式无效，原缓存已保留。",
        DashboardErrors.HourlyForecastIncomplete => "小时预报不足 5 条，原缓存已保留。",
        DashboardErrors.MarketFetchFailed => "行情数据获取失败，原缓存已保留。",
        _ => error ?? "数据尚未准备好。"
    };

    private void SelectMonitor(string stableId)
    {
        _config.UpdateAndSave(c =>
        {
            c.Kvm.DisplayId = stableId;
            if (c.Kvm.Enabled && string.IsNullOrWhiteSpace(c.Kvm.DisplayId))
                c.Kvm.Enabled = false;
        });
        _log.Info($"Monitor selected: {stableId}");
        UpdateMenu();
    }

    private void TestDdc(int inputValue, string label)
    {
        var cfg = _config.Current;
        if (string.IsNullOrWhiteSpace(cfg.Kvm.DisplayId))
        {
            _tray.ShowBalloonTip(3000, "DDC 测试", "尚未选择显示器", ToolTipIcon.Warning);
            return;
        }
        _discovery.Scan();
        if (_discovery.FindById(cfg.Kvm.DisplayId) == null)
        {
            _tray.ShowBalloonTip(3000, "DDC 测试", "配置的显示器未找到，请重新选择", ToolTipIcon.Error);
            return;
        }

        System.Threading.Tasks.Task.Run(() =>
        {
            var result = _ddc.WriteInput(cfg.Kvm.DisplayId, (byte)cfg.Kvm.VcpCode,
                (uint)inputValue, false, TimeSpan.FromSeconds(4));
            string msg = result.WriteSucceeded
                ? $"切换到 {label}（{inputValue}）成功"
                : $"切换失败：{result.ErrorCode}";
            _log.Info($"Manual DDC test {label} ({inputValue}): {(result.WriteSucceeded ? "ok" : result.ErrorCode)}");
            _tray.ShowBalloonTip(3000, "DDC 测试", msg, result.WriteSucceeded ? ToolTipIcon.Info : ToolTipIcon.Error);
        });
    }

    private void TestDisplaySleep()
    {
        MessageBox.Show(
            "此测试只会停止显示输出，不会关闭 Windows，网络和后台任务会继续运行。\n\n" +
            "恢复方式：使用键盘或鼠标唤醒显示器，或让 ESP32 发送 display_wake。恢复画面后也可在托盘中选择“测试恢复显示输出”。",
            "测试关闭显示输出",
            MessageBoxButtons.OK,
            MessageBoxIcon.Information);
        TestDisplayPower("display_sleep");
    }

    private void TestDisplayPower(string action)
    {
        var command = new IncomingCommand
        {
            Type = "command",
            ProtocolVersion = 1,
            RequestId = $"local-{Guid.NewGuid():N}",
            TargetDeviceId = _identity.DeviceId,
            Action = action
        };

        System.Threading.Tasks.Task.Run(() =>
        {
            var result = _dispatcher.Execute(command);
            var message = result.Success
                ? action == "display_sleep"
                    ? "显示输出关闭请求已提交；Windows 仍在运行"
                    : "显示输出恢复请求已提交"
                : $"显示电源测试失败：{result.Error}";
            _syncContext?.Post(_ =>
            {
                _tray.ShowBalloonTip(
                    4000,
                    "显示电源测试",
                    message,
                    result.Success ? ToolTipIcon.Info : ToolTipIcon.Error);
                UpdateMenu();
            }, null);
        });
    }

    private void ExitApp()
    {
        _log.Info("Exit requested from tray menu.");
        _uiTimer?.Stop();
        _uiTimer?.Dispose();
        _uiTimer = null;
        _ws.Stop();
        _displayPower.Dispose();
        _tray.Visible = false;
        _tray.Dispose();
        Application.Exit();
    }

    private static string GetAppDataDir() =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "KVMBridgeWindows");

    private static void OpenFile(string path)
    {
        try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path) { UseShellExecute = true }); }
        catch { }
    }

    private static void OpenFolder(string path)
    {
        try
        {
            Directory.CreateDirectory(path);
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("explorer.exe", path) { UseShellExecute = true });
        }
        catch { }
    }

    private static Icon CreateColorIcon(Color color)
    {
        using var bmp = new Bitmap(16, 16);
        using var g = Graphics.FromImage(bmp);
        g.Clear(Color.Transparent);
        using var brush = new SolidBrush(color);
        g.FillEllipse(brush, 2, 2, 12, 12);
        return Icon.FromHandle(bmp.GetHicon());
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _uiTimer?.Stop();
            _uiTimer?.Dispose();
            _tray.Dispose();
            _menu.Dispose();
            _ws.Dispose();
            _dashboard.Dispose();
            _telemetry.Dispose();
            _ddc.Dispose();
            _displayPower.Dispose();
            _log.Dispose();
        }
        base.Dispose(disposing);
    }
}
