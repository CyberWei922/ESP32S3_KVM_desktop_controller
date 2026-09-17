using System.Text;
using System.Text.Json;
using KVMBridgeWindows.Dashboard;
using KVMBridgeWindows.Dashboard.Markets;
using KVMBridgeWindows.Dashboard.Weather;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Settings;
using System.Net;
using Xunit;

namespace KVMBridgeWindows.Tests;

public sealed class DashboardTests
{
    private static readonly ResolvedCity Shanghai = new("1796236", "上海", 31.23, 121.47, "Asia/Shanghai");

    [Fact]
    public void OpenMeteoSample_MapsTodayTomorrowDayAfter_AndFiveHours()
    {
        var json = """
        {
          "current":{"time":"2026-09-17T10:30","temperature_2m":27.4,"weather_code":2,"is_day":1},
          "daily":{"time":["2026-09-17","2026-09-18","2026-09-19"],"weather_code":[2,61,3],"temperature_2m_max":[29.4,28.2,27.1],"temperature_2m_min":[22.2,21.1,20.3]},
          "hourly":{"time":["2026-09-17T11:00","2026-09-17T12:00","2026-09-17T13:00","2026-09-17T14:00","2026-09-17T15:00"],"temperature_2m":[28.1,29.0,29.4,29.2,28.7],"weather_code":[2,2,2,3,3],"is_day":[1,1,1,1,1]}
        }
        """;

        var data = OpenMeteoWeatherProvider.ParseForecast(json, Shanghai);

        Assert.Equal("open_meteo", data.Provider);
        Assert.Equal("上海", data.CityName);
        Assert.Equal(["THU", "FRI", "SAT"], data.Daily.Select(x => x.Day).ToArray());
        Assert.Equal([11, 12, 13, 14, 15], data.Hourly.Select(x => x.Hour).ToArray());
        Assert.Equal(5, data.Hourly.Count);
    }

    [Fact]
    public void QWeatherV1Sample_MapsUnifiedContract()
    {
        var current = """
        {"metadata":{"tag":"sample"},"temperature":{"value":27.2},"condition":{"code":"101"},"isDaylight":true}
        """;
        var daily = """
        {"days":[
          {"forecastStartTime":"2026-09-17T00:00+08:00","temperatureMax":{"value":30},"temperatureMin":{"value":22},"daytime":{"condition":{"code":"101"}}},
          {"forecastStartTime":"2026-09-18T00:00+08:00","temperatureMax":{"value":29},"temperatureMin":{"value":21},"daytime":{"condition":{"code":"305"}}},
          {"forecastStartTime":"2026-09-19T00:00+08:00","temperatureMax":{"value":28},"temperatureMin":{"value":20},"daytime":{"condition":{"code":"104"}}}
        ]}
        """;
        var hourly = """
        {"hours":[
          {"forecastTime":"2026-09-17T11:00+08:00","temperature":{"value":28},"condition":{"code":"101"},"isDaylight":true},
          {"forecastTime":"2026-09-17T12:00+08:00","temperature":{"value":29},"condition":{"code":"101"},"isDaylight":true},
          {"forecastTime":"2026-09-17T13:00+08:00","temperature":{"value":30},"condition":{"code":"100"},"isDaylight":true},
          {"forecastTime":"2026-09-17T14:00+08:00","temperature":{"value":30},"condition":{"code":"100"},"isDaylight":true},
          {"forecastTime":"2026-09-17T15:00+08:00","temperature":{"value":29},"condition":{"code":"101"},"isDaylight":true}
        ]}
        """;

        var data = QWeatherProvider.ParseForecast(current, daily, hourly, new("101020100", "上海", 31.23, 121.47, "Asia/Shanghai"));

        Assert.Equal("qweather", data.Provider);
        Assert.Equal(3, data.Daily.Count);
        Assert.Equal([11, 12, 13, 14, 15], data.Hourly.Select(x => x.Hour).ToArray());
        Assert.True(data.Current.IsDay);
    }

    [Fact]
    public void AMapSample_UsesThreeDaysAndMappedCodes()
    {
        var live = """
        {"status":"1","lives":[{"city":"上海市","adcode":"310000","weather":"雷阵雨","temperature":"27","temperature_float":"27.4"}]}
        """;
        var forecast = """
        {"status":"1","forecasts":[{"casts":[
          {"date":"2026-09-17","dayweather":"多云","daytemp":"30","daytemp_float":"30.2","nighttemp":"22","nighttemp_float":"21.8"},
          {"date":"2026-09-18","dayweather":"小雨","daytemp":"29","nighttemp":"21"},
          {"date":"2026-09-19","dayweather":"阴","daytemp":"28","nighttemp":"20"}
        ]}]}
        """;
        var hours = Enumerable.Range(11, 5).Select(hour => new HourlyWeather { Hour = hour, Temp = 28, Code = 2, IsDay = true }).ToList();

        var data = AMapWeatherProvider.ParseWeather(live, forecast, new("310000", "上海市", 31.23, 121.47, "Asia/Shanghai"), hours);

        Assert.Equal("amap", data.Provider);
        Assert.Equal(95, data.Current.Code);
        Assert.Equal([3, 61, 3], data.Daily.Select(x => x.Code).ToArray());
        Assert.Equal(5, data.Hourly.Count);
    }

    [Theory]
    [InlineData("晴", 0)]
    [InlineData("多云", 3)]
    [InlineData("大雨", 61)]
    [InlineData("暴雪", 71)]
    [InlineData("雾", 45)]
    public void AMapWeatherText_MapsToStableCodes(string text, int expected) =>
        Assert.Equal(expected, WeatherCodeMapper.FromAmapText(text));

    [Fact]
    public void BinanceSample_ParsesActualPercentValues()
    {
        var items = BinanceMarketProvider.Parse("""
        [{"symbol":"BTCUSDT","lastPrice":"117500.12","priceChangePercent":"2.345"},{"symbol":"DOGEUSDT","lastPrice":"0.2845","priceChangePercent":"-1.250"}]
        """);
        Assert.Equal(["BTC/USDT", "DOGE/USDT"], items.Select(x => x.Symbol).ToArray());
        Assert.Equal(2.345m, items[0].ChangePercent);
        Assert.Equal(-1.250m, items[1].ChangePercent);
    }

    [Fact]
    public void FrankfurterSample_UsesZeroChange()
    {
        var item = ExchangeRateProvider.Parse("""{"amount":1.0,"base":"USD","date":"2026-09-16","rates":{"CNY":6.7071}}""");
        Assert.Equal("USD/CNY", item.Symbol);
        Assert.Equal(6.7071m, item.Price);
        Assert.Equal(0m, item.ChangePercent);
    }

    [Fact]
    public void DashboardMessage_IsUtf8SafeUnderLimit_AndSequenceResets()
    {
        var factory = new DashboardMessageFactory("win-test");
        var snapshot = new DashboardSnapshot(SampleWeather(), SampleMarkets(), null);
        var first = factory.CreateJson(snapshot, 1234567890);
        var second = factory.CreateJson(snapshot, 1234567891);
        factory.ResetSequence();
        var reset = factory.CreateJson(snapshot, 1234567892);

        Assert.True(Encoding.UTF8.GetByteCount(first) <= 2048);
        using var firstDoc = JsonDocument.Parse(first);
        using var secondDoc = JsonDocument.Parse(second);
        using var resetDoc = JsonDocument.Parse(reset);
        Assert.Equal("上海", firstDoc.RootElement.GetProperty("weather").GetProperty("city_name").GetString());
        Assert.Equal((ulong)0, firstDoc.RootElement.GetProperty("sequence").GetUInt64());
        Assert.Equal((ulong)1, secondDoc.RootElement.GetProperty("sequence").GetUInt64());
        Assert.Equal((ulong)0, resetDoc.RootElement.GetProperty("sequence").GetUInt64());
        Assert.Equal(
            ["type", "protocol_version", "platform", "device_id", "sequence", "timestamp_milliseconds", "weather", "markets", "error"],
            firstDoc.RootElement.EnumerateObject().Select(x => x.Name).ToArray());
        var weather = firstDoc.RootElement.GetProperty("weather");
        Assert.Equal(
            ["provider", "city_code", "city_name", "updated_at_milliseconds", "current", "daily", "hourly"],
            weather.EnumerateObject().Select(x => x.Name).ToArray());
        Assert.Equal(
            ["temperature_c", "high_c", "low_c", "weather_code", "is_day"],
            weather.GetProperty("current").EnumerateObject().Select(x => x.Name).ToArray());
        var markets = firstDoc.RootElement.GetProperty("markets");
        Assert.Equal(
            ["updated_at_milliseconds", "btc_usdt", "doge_usdt", "usd_cny"],
            markets.EnumerateObject().Select(x => x.Name).ToArray());
        Assert.Equal("0.0", markets.GetProperty("usd_cny").GetProperty("change_percent").GetRawText());
    }

    [Fact]
    public void OversizedDashboard_UsesStableErrorInsteadOfFragmenting()
    {
        var weather = SampleWeather();
        weather.CityName = new string('城', 1000);
        var json = new DashboardMessageFactory("win-test").CreateJson(new DashboardSnapshot(weather, SampleMarkets(), null));
        using var document = JsonDocument.Parse(json);
        Assert.True(Encoding.UTF8.GetByteCount(json) <= 2048);
        Assert.Equal(DashboardErrors.MessageTooLarge, document.RootElement.GetProperty("error").GetString());
        Assert.Equal(JsonValueKind.Null, document.RootElement.GetProperty("weather").ValueKind);
    }

    [Fact]
    public async Task ChineseCityName_AtOrAbove32Utf8Bytes_IsRejected()
    {
        var temp = Path.Combine(Path.GetTempPath(), "kvm-dashboard-city-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temp);
        using var log = new AppLogger(Path.Combine(temp, "logs"), LogLevel.Debug, 1);
        var config = new ConfigurationService(temp, log);
        config.Load();
        using var coordinator = new DashboardCoordinator(config, [new LongCityProvider()], [], log);
        var proposed = new DashboardConfig { WeatherProvider = "open_meteo" };

        var exception = await Assert.ThrowsAsync<DashboardDataException>(() =>
            coordinator.ResolveCityAsync(proposed, "城市", CancellationToken.None));

        Assert.Equal(DashboardErrors.CityNotFound, exception.ErrorCode);
        coordinator.Dispose();
        log.Dispose();
        try { Directory.Delete(temp, true); } catch { }
    }

    [Fact]
    public async Task ApiKey_IsNeverWrittenToDashboardLogs()
    {
        const string secret = "super-secret-qweather-key";
        var temp = Path.Combine(Path.GetTempPath(), "kvm-dashboard-secret-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temp);
        var log = new AppLogger(Path.Combine(temp, "logs"), LogLevel.Debug, 1);
        var config = new ConfigurationService(temp, log);
        config.Load();
        config.UpdateAndSave(c =>
        {
            c.Dashboard.WeatherProvider = "qweather";
            c.Dashboard.CityCode = "101020100";
            c.Dashboard.CityName = "上海";
            c.Dashboard.Latitude = 31.23;
            c.Dashboard.Longitude = 121.47;
            c.Dashboard.QWeatherApiHost = "example.invalid";
        });
        using var http = new HttpClient(new FixedStatusHandler(HttpStatusCode.Unauthorized));
        var provider = new QWeatherProvider(http, new FixedCredentialStore(secret));
        using (var coordinator = new DashboardCoordinator(config, [provider], [], log))
            await coordinator.RefreshWeatherNowAsync();
        log.Dispose();

        var allLogs = string.Join("\n", Directory.GetFiles(Path.Combine(temp, "logs")).Select(File.ReadAllText));
        Assert.DoesNotContain(secret, allLogs, StringComparison.Ordinal);
        try { Directory.Delete(temp, true); } catch { }
    }

    [Fact]
    public async Task FailedCityResolution_PreservesPreviousCanonicalCity()
    {
        var temp = Path.Combine(Path.GetTempPath(), "kvm-dashboard-settings-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temp);
        var log = new AppLogger(Path.Combine(temp, "logs"), LogLevel.Debug, 1);
        var config = new ConfigurationService(temp, log);
        config.Load();
        config.UpdateAndSave(c =>
        {
            c.Dashboard.CityCode = "1796236";
            c.Dashboard.CityName = "上海";
            c.Dashboard.Latitude = 31.23;
            c.Dashboard.Longitude = 121.47;
            c.Dashboard.Timezone = "Asia/Shanghai";
        });
        using var coordinator = new DashboardCoordinator(config, [new RejectCityProvider()], [], log);
        var store = new DashboardSettingsStore(config, coordinator, new FixedCredentialStore(""));

        var result = await store.ApplyAsync(new DashboardSettingsUpdate("open_meteo", "不存在城市", "", ""));

        Assert.False(result.Success);
        Assert.Equal(DashboardErrors.CityNotFound, result.Error);
        Assert.Equal("1796236", config.Current.Dashboard.CityCode);
        Assert.Equal("上海", config.Current.Dashboard.CityName);
        log.Dispose();
        try { Directory.Delete(temp, true); } catch { }
    }

    private static WeatherData SampleWeather() => new()
    {
        Provider = "open_meteo",
        CityCode = "1796236",
        CityName = "上海",
        UpdatedAtMilliseconds = 1,
        Current = new CurrentWeather { Temp = 27, High = 30, Low = 22, Code = 2, IsDay = true },
        Daily = [
            new() { Day = "THU", High = 30, Low = 22, Code = 2 },
            new() { Day = "FRI", High = 29, Low = 21, Code = 61 },
            new() { Day = "SAT", High = 28, Low = 20, Code = 3 }],
        Hourly = Enumerable.Range(11, 5).Select(hour => new HourlyWeather { Hour = hour, Temp = 28, Code = 2, IsDay = true }).ToList()
    };

    private static MarketsData SampleMarkets() => new()
    {
        UpdatedAtMilliseconds = 1,
        BtcUsdt = new() { Symbol = "BTC/USDT", Price = 117500.12m, ChangePercent = 2.34m },
        DogeUsdt = new() { Symbol = "DOGE/USDT", Price = 0.2845m, ChangePercent = -1.25m },
        UsdCny = new() { Symbol = "USD/CNY", Price = 6.7071m, ChangePercent = 0.0m }
    };

    private sealed class LongCityProvider : IWeatherProvider
    {
        public string ProviderId => "open_meteo";
        public Task<ResolvedCity> ResolveCityAsync(string cityInput, DashboardConfig settings, CancellationToken cancellationToken) =>
            Task.FromResult(new ResolvedCity("123", "一二三四五六七八九十一", 31, 121, "Asia/Shanghai"));
        public Task<WeatherData> FetchAsync(ResolvedCity city, DashboardConfig settings, CancellationToken cancellationToken) =>
            throw new NotSupportedException();
    }

    private sealed class FixedCredentialStore(string secret) : ICredentialStore
    {
        public string? Read(string targetName) => secret;
        public void Write(string targetName, string value) { }
    }

    private sealed class RejectCityProvider : IWeatherProvider
    {
        public string ProviderId => "open_meteo";
        public Task<ResolvedCity> ResolveCityAsync(string cityInput, DashboardConfig settings, CancellationToken cancellationToken) =>
            Task.FromException<ResolvedCity>(new DashboardDataException(DashboardErrors.CityNotFound));
        public Task<WeatherData> FetchAsync(ResolvedCity city, DashboardConfig settings, CancellationToken cancellationToken) =>
            throw new NotSupportedException();
    }

    private sealed class FixedStatusHandler(HttpStatusCode status) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken) =>
            Task.FromResult(new HttpResponseMessage(status) { Content = new StringContent("{}") });
    }
}
