using System;
using System.IO;
using System.Threading;
using System.Windows.Forms;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Dashboard;
using KVMBridgeWindows.Dashboard.Markets;
using KVMBridgeWindows.Dashboard.Weather;
using KVMBridgeWindows.Ddc;
using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Metrics;
using KVMBridgeWindows.Networking;
using KVMBridgeWindows.Protocol;
using KVMBridgeWindows.Startup;
using KVMBridgeWindows.Settings;

namespace KVMBridgeWindows;

static class Program
{
    private const string MutexName = @"Local\KVMBridgeWindows.Singleton";

    [STAThread]
    static void Main()
    {
        // Single instance guard
        using var mutex = new Mutex(true, MutexName, out bool isNew);
        if (!isNew)
        {
            MessageBox.Show("KVMBridgeWindows 已在运行。", "KVMBridgeWindows",
                MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.SetHighDpiMode(HighDpiMode.SystemAware);

        var appDataDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "KVMBridgeWindows");
        Directory.CreateDirectory(appDataDir);
        Directory.CreateDirectory(Path.Combine(appDataDir, "logs"));

        // Bootstrap in order: log → config → identity → services
        var log = new AppLogger(Path.Combine(appDataDir, "logs"), LogLevel.Info, 7);
        log.Info("KVMBridgeWindows v3.0.0 starting.");

        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (s, e) => log.Error("Unhandled UI thread exception", e.Exception);
        AppDomain.CurrentDomain.UnhandledException += (s, e) =>
            log.Error("Unhandled AppDomain exception", e.ExceptionObject as Exception ?? new Exception(e.ExceptionObject?.ToString()));

        var configSvc = new ConfigurationService(appDataDir, log);
        configSvc.Load();

        // Apply log level from config
        var level = configSvc.Current.Logging.Level?.ToLowerInvariant() switch
        {
            "debug" => LogLevel.Debug,
            "warning" => LogLevel.Warning,
            "error" => LogLevel.Error,
            _ => LogLevel.Info
        };
        log.SetLevel(level);

        var identity = new DeviceIdentityService(appDataDir, log);
        identity.Load();

        var startup = new StartupService(log);
        var codec = new ProtocolCodec(identity.DeviceId, log);
        var discovery = new MonitorDiscoveryService(log);
        var ddc = new DdcService(discovery, log);
        var displayPower = new DisplayPowerService(new WindowsDisplayPowerPlatform(), log);
        displayPower.UpdateSystemSleepAssertion(
            configSvc.Current.Connection.Enabled,
            configSvc.Current.DisplayPower.Enabled,
            configSvc.Current.DisplayPower.PreventSystemSleepWhileEnabled);

        var cpuReader = new CpuTemperatureReader(log);
        cpuReader.TryInitialize();
        var memReader = new MemoryReader(log);
        var telemetry = new TelemetryService(identity.DeviceId, configSvc, cpuReader, memReader, log);
        telemetry.Start(configSvc.Current.Connection.TelemetryIntervalMs);

        using var dashboardHttp = new HttpClient { Timeout = TimeSpan.FromSeconds(12) };
        var credentials = new WindowsCredentialStore();
        var openMeteo = new OpenMeteoWeatherProvider(dashboardHttp);
        var qweather = new QWeatherProvider(dashboardHttp, credentials);
        var amap = new AMapWeatherProvider(dashboardHttp, credentials, openMeteo, log);
        var dashboard = new DashboardCoordinator(
            configSvc,
            [openMeteo, qweather, amap],
            [new BinanceMarketProvider(dashboardHttp), new ExchangeRateProvider(dashboardHttp)],
            log);
        var dashboardFactory = new DashboardMessageFactory(identity.DeviceId);
        var dashboardSettings = new DashboardSettingsStore(configSvc, dashboard, credentials);
        dashboard.Start();

        var dispatcher = new CommandDispatcher(configSvc, ddc, discovery, codec, displayPower, log);
        var ws = new WebSocketService(
            configSvc, telemetry, codec, dispatcher, displayPower,
            dashboard, dashboardFactory, log);

        log.Info($"Device ID: {identity.DeviceId}");
        log.Info($"Connection enabled: {configSvc.Current.Connection.Enabled}");
        log.Info($"KVM enabled: {configSvc.Current.Kvm.Enabled}");

        var ctx = new TrayApplicationContext(
            configSvc, identity, startup, discovery, ddc, telemetry, ws, codec,
            dispatcher, displayPower, dashboard, dashboardSettings, log);

        try
        {
            Application.Run(ctx);
            log.Info("KVMBridgeWindows exited normally.");
        }
        finally
        {
            ctx.Dispose();
        }
    }
}
