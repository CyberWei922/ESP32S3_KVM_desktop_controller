using System.Text.Json.Serialization;

namespace KVMBridgeWindows.Dashboard;

public sealed class DashboardMessage
{
    [JsonPropertyName("type")] public string Type { get; } = "dashboard";
    [JsonPropertyName("protocol_version")] public int ProtocolVersion { get; } = 1;
    [JsonPropertyName("platform")] public string Platform { get; } = "windows";
    [JsonPropertyName("device_id")] public string DeviceId { get; set; } = string.Empty;
    [JsonPropertyName("sequence")] public ulong Sequence { get; set; }
    [JsonPropertyName("timestamp_milliseconds")] public long TimestampMilliseconds { get; set; }
    [JsonPropertyName("weather")] public WeatherData? Weather { get; set; }
    [JsonPropertyName("markets")] public MarketsData? Markets { get; set; }
    [JsonPropertyName("error")] public string? Error { get; set; }
}

public sealed class WeatherData
{
    [JsonPropertyName("provider")] public string Provider { get; set; } = string.Empty;
    [JsonPropertyName("city_code")] public string CityCode { get; set; } = string.Empty;
    [JsonPropertyName("city_name")] public string CityName { get; set; } = string.Empty;
    [JsonPropertyName("updated_at_milliseconds")] public long UpdatedAtMilliseconds { get; set; }
    [JsonPropertyName("current")] public CurrentWeather Current { get; set; } = new();
    [JsonPropertyName("daily")] public List<DailyWeather> Daily { get; set; } = [];
    [JsonPropertyName("hourly")] public List<HourlyWeather> Hourly { get; set; } = [];
}

public sealed class CurrentWeather
{
    [JsonPropertyName("temperature_c")] public int Temp { get; set; }
    [JsonPropertyName("high_c")] public int High { get; set; }
    [JsonPropertyName("low_c")] public int Low { get; set; }
    [JsonPropertyName("weather_code")] public int Code { get; set; }
    [JsonPropertyName("is_day")] public bool IsDay { get; set; }
}

public sealed class DailyWeather
{
    [JsonPropertyName("day")] public string Day { get; set; } = string.Empty;
    [JsonPropertyName("high_c")] public int High { get; set; }
    [JsonPropertyName("low_c")] public int Low { get; set; }
    [JsonPropertyName("weather_code")] public int Code { get; set; }
}

public sealed class HourlyWeather
{
    [JsonPropertyName("hour")] public int Hour { get; set; }
    [JsonPropertyName("temperature_c")] public int Temp { get; set; }
    [JsonPropertyName("weather_code")] public int Code { get; set; }
    [JsonPropertyName("is_day")] public bool IsDay { get; set; }
}

public sealed class MarketsData
{
    [JsonPropertyName("updated_at_milliseconds")] public long UpdatedAtMilliseconds { get; set; }
    [JsonPropertyName("btc_usdt")] public MarketItem BtcUsdt { get; set; } = new();
    [JsonPropertyName("doge_usdt")] public MarketItem DogeUsdt { get; set; } = new();
    [JsonPropertyName("usd_cny")] public MarketItem UsdCny { get; set; } = new();
}

public sealed class MarketItem
{
    [JsonIgnore] public string Symbol { get; set; } = string.Empty;
    [JsonPropertyName("price")] public decimal Price { get; set; }
    [JsonPropertyName("change_percent")] public decimal ChangePercent { get; set; }
}

public sealed record ResolvedCity(
    string Code,
    string Name,
    double Latitude,
    double Longitude,
    string Timezone);

public sealed record DashboardSnapshot(WeatherData? Weather, MarketsData? Markets, string? Error);

public static class DashboardErrors
{
    public const string Loading = "dashboard_loading";
    public const string CityCodeMissing = "city_code_missing";
    public const string CityNotFound = "city_not_found";
    public const string QWeatherCredentialsMissing = "qweather_credentials_missing";
    public const string WeatherFetchFailed = "weather_fetch_failed";
    public const string WeatherResponseInvalid = "weather_response_invalid";
    public const string HourlyForecastIncomplete = "hourly_forecast_incomplete";
    public const string MarketFetchFailed = "market_fetch_failed";
    public const string MessageTooLarge = "dashboard_message_too_large";
}

public sealed class DashboardDataException : Exception
{
    public string ErrorCode { get; }

    public DashboardDataException(string errorCode, string? detail = null, Exception? inner = null)
        : base(detail ?? errorCode, inner) => ErrorCode = errorCode;
}
