using System.IO;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Startup;
using Xunit;

namespace KVMBridgeWindows.Tests;

public class ConfigurationTests : IDisposable
{
    private readonly string _tempDir;
    private readonly AppLogger _log;

    public ConfigurationTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "kvmbridgetest_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
        _log = new AppLogger(Path.Combine(_tempDir, "logs"), LogLevel.Debug, 1);
    }

    [Fact]
    public void FirstRun_CreatesDefaultConfig_KvmDisabled()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        Assert.False(svc.Current.Kvm.Enabled);
        Assert.Equal(string.Empty, svc.Current.Kvm.DisplayId);
        Assert.True(File.Exists(Path.Combine(_tempDir, "config.json")));
    }

    [Fact]
    public void KvmCannotBeEnabled_WithoutDisplayId()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        // Try to enable KVM without display ID
        svc.UpdateAndSave(c =>
        {
            c.Kvm.DisplayId = "";
            c.Kvm.Enabled = true;
        });
        // Validation should have forced it back to false
        Assert.False(svc.Current.Kvm.Enabled);
    }

    [Fact]
    public void KvmCanBeEnabled_WithDisplayId()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        svc.UpdateAndSave(c =>
        {
            c.Kvm.DisplayId = "mon-abcdef123456";
            c.Kvm.Enabled = true;
        });
        Assert.True(svc.Current.Kvm.Enabled);
        Assert.Equal("mon-abcdef123456", svc.Current.Kvm.DisplayId);
    }

    [Fact]
    public void CorruptConfig_FallsBackToSafeDefaults()
    {
        File.WriteAllText(Path.Combine(_tempDir, "config.json"), "{ not valid json {{{{");
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        Assert.False(svc.Current.Kvm.Enabled);
        Assert.False(svc.Current.Connection.Enabled);
    }

    [Fact]
    public void AtomicSave_ProducesValidJson()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        svc.UpdateAndSave(c => c.Connection.Host = "10.0.0.1");
        var svc2 = new ConfigurationService(_tempDir, _log);
        svc2.Load();
        Assert.Equal("10.0.0.1", svc2.Current.Connection.Host);
    }

    [Fact]
    public void DeviceId_PersistsAcrossLoads()
    {
        var id1 = new DeviceIdentityService(_tempDir, _log);
        id1.Load();
        var first = id1.DeviceId;
        Assert.StartsWith("win-", first);

        var id2 = new DeviceIdentityService(_tempDir, _log);
        id2.Load();
        Assert.Equal(first, id2.DeviceId);
    }

    [Fact]
    public void TelemetryInterval_EnforcedMinimum()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        svc.UpdateAndSave(c => c.Connection.TelemetryIntervalMs = 100); // below 500
        Assert.Equal(500, svc.Current.Connection.TelemetryIntervalMs);
    }

    [Fact]
    public void VcpCode_AlwaysFixed96()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        svc.UpdateAndSave(c => c.Kvm.VcpCode = 99);
        Assert.Equal(96, svc.Current.Kvm.VcpCode);
    }

    [Fact]
    public void MacInput_AlwaysFixed6()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        svc.UpdateAndSave(c => c.Kvm.MacInput = 99);
        Assert.Equal(6, svc.Current.Kvm.MacInput);
    }

    [Fact]
    public void WindowsInput_AlwaysFixed7()
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        svc.UpdateAndSave(c => c.Kvm.WindowsInput = 99);
        Assert.Equal(7, svc.Current.Kvm.WindowsInput);
    }

    [Fact]
    public void V1Config_MigratesDisplayPowerWithSafeDefaults_WithoutChangingOldValues()
    {
        File.WriteAllText(
            Path.Combine(_tempDir, "config.json"),
            """
            {
              "schema_version": 1,
              "connection": {
                "enabled": true,
                "host": "10.20.30.40",
                "port": 81,
                "path": "/statsforkvm",
                "telemetry_interval_ms": 1000,
                "keepalive_interval_ms": 15000
              },
              "telemetry": {
                "enabled": true,
                "cpu_temperature_source": "LibreHardwareMonitor CPU Package",
                "memory_source": "GlobalMemoryStatusEx"
              },
              "kvm": {
                "enabled": true,
                "display_id": "mon-existing",
                "vcp_code": 96,
                "mac_input": 6,
                "windows_input": 7,
                "verify_readback": false,
                "ddc_backend": "native"
              },
              "startup": { "start_with_windows": true },
              "logging": { "level": "warning", "retention_days": 9 }
            }
            """);

        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();

        Assert.True(svc.Current.Connection.Enabled);
        Assert.Equal("10.20.30.40", svc.Current.Connection.Host);
        Assert.True(svc.Current.Kvm.Enabled);
        Assert.Equal("mon-existing", svc.Current.Kvm.DisplayId);
        Assert.True(svc.Current.Startup.StartWithWindows);
        Assert.Equal("warning", svc.Current.Logging.Level);
        Assert.False(svc.Current.DisplayPower.Enabled);
        Assert.True(svc.Current.DisplayPower.PreventSystemSleepWhileEnabled);
        Assert.Equal(3000, svc.Current.DisplayPower.CommandTimeoutMilliseconds);
        Assert.True(svc.Current.Dashboard.Enabled);
        Assert.Equal(10, svc.Current.Dashboard.SendIntervalSeconds);
        Assert.Equal(15, svc.Current.Dashboard.WeatherRefreshMinutes);
        Assert.Equal("open_meteo", svc.Current.Dashboard.WeatherProvider);
        Assert.Equal(string.Empty, svc.Current.Dashboard.CityCode);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(5000)]
    public void DisplayPowerTimeout_OutsideCommandWindow_UsesSafeDefault(int timeout)
    {
        var svc = new ConfigurationService(_tempDir, _log);
        svc.Load();
        svc.UpdateAndSave(c => c.DisplayPower.CommandTimeoutMilliseconds = timeout);
        Assert.Equal(3000, svc.Current.DisplayPower.CommandTimeoutMilliseconds);
    }

    public void Dispose()
    {
        _log.Dispose();
        try { Directory.Delete(_tempDir, recursive: true); } catch { }
    }
}
