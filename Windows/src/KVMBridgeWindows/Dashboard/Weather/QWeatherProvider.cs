using System.Globalization;
using System.Net.Http.Headers;
using System.Text.Json;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Settings;

namespace KVMBridgeWindows.Dashboard.Weather;

public sealed class QWeatherProvider : IWeatherProvider
{
    private readonly HttpClient _http;
    private readonly ICredentialStore _credentials;

    public string ProviderId => "qweather";

    public QWeatherProvider(HttpClient http, ICredentialStore credentials)
    {
        _http = http;
        _credentials = credentials;
    }

    public async Task<ResolvedCity> ResolveCityAsync(string cityInput, DashboardConfig settings, CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(cityInput))
            throw new DashboardDataException(DashboardErrors.CityCodeMissing);
        var (host, key) = GetCredentials(settings);
        var url = $"{host}/geo/v2/city/lookup?location={Uri.EscapeDataString(cityInput.Trim())}&range=cn&number=1&lang=zh";
        try
        {
            using var response = await SendAsync(url, key, cancellationToken).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
                throw new DashboardDataException(DashboardErrors.CityNotFound);
            using var document = JsonDocument.Parse(await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false));
            var root = document.RootElement;
            if (root.TryGetProperty("code", out var codeValue) && codeValue.GetString() is { } responseCode && responseCode != "200")
                throw new DashboardDataException(DashboardErrors.CityNotFound);
            var locations = root.GetProperty("location");
            if (locations.GetArrayLength() == 0) throw new DashboardDataException(DashboardErrors.CityNotFound);
            var location = locations[0];
            return new ResolvedCity(
                location.GetProperty("id").GetString() ?? string.Empty,
                location.GetProperty("name").GetString() ?? string.Empty,
                double.Parse(location.GetProperty("lat").GetString()!, CultureInfo.InvariantCulture),
                double.Parse(location.GetProperty("lon").GetString()!, CultureInfo.InvariantCulture),
                location.TryGetProperty("tz", out var tz) ? tz.GetString() ?? "Asia/Shanghai" : "Asia/Shanghai");
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.CityNotFound, inner: ex);
        }
    }

    public async Task<WeatherData> FetchAsync(ResolvedCity city, DashboardConfig settings, CancellationToken cancellationToken)
    {
        var (host, key) = GetCredentials(settings);
        var lat = city.Latitude.ToString("0.######", CultureInfo.InvariantCulture);
        var lon = city.Longitude.ToString("0.######", CultureInfo.InvariantCulture);
        try
        {
            var currentTask = GetStringAsync($"{host}/weather/v1/current/{lat}/{lon}?lang=zh", key, cancellationToken);
            var dailyTask = GetStringAsync($"{host}/weather/v1/daily/{lat}/{lon}?days=4&localTime=true&lang=zh", key, cancellationToken);
            var hourlyTask = GetStringAsync($"{host}/weather/v1/hourly/{lat}/{lon}?hours=6&localTime=true&lang=zh", key, cancellationToken);
            await Task.WhenAll(currentTask, dailyTask, hourlyTask).ConfigureAwait(false);
            return ParseForecast(currentTask.Result, dailyTask.Result, hourlyTask.Result, city);
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.WeatherFetchFailed, inner: ex);
        }
    }

    internal static WeatherData ParseForecast(string currentJson, string dailyJson, string hourlyJson, ResolvedCity city)
    {
        try
        {
            using var currentDocument = JsonDocument.Parse(currentJson);
            using var dailyDocument = JsonDocument.Parse(dailyJson);
            using var hourlyDocument = JsonDocument.Parse(hourlyJson);
            var currentRoot = currentDocument.RootElement;
            var dailyRoot = dailyDocument.RootElement;
            var hourlyRoot = hourlyDocument.RootElement;
            EnsureSuccess(currentRoot);
            EnsureSuccess(dailyRoot);
            EnsureSuccess(hourlyRoot);

            var current = currentRoot.TryGetProperty("temperature", out _) && currentRoot.TryGetProperty("condition", out _)
                ? currentRoot
                : GetObject(currentRoot, "current", "now");
            var daily = GetArray(dailyRoot, "days", "daily").EnumerateArray().Take(3).ToArray();
            var allHourly = GetArray(hourlyRoot, "hours", "hourly").EnumerateArray().ToArray();
            var hourly = allHourly
                .Select(item => (Item: item, Time: ParseTimestamp(ReadString(item, "forecastTime", "fxTime"))))
                .OrderBy(x => x.Time)
                .Take(5)
                .Select(x => x.Item)
                .ToArray();
            if (daily.Length != 3) throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
            if (hourly.Length != 5) throw new DashboardDataException(DashboardErrors.HourlyForecastIncomplete);
            WeatherProviderHelpers.EnsureConsecutiveDays(
                daily.Select(item => ParseTimestamp(ReadString(item, "forecastStartTime", "fxDate")).DateTime).ToArray());

            var currentTemp = ReadNestedNumber(current, "temperature", "value", "temp");
            var currentCode = ReadNestedInt(current, "condition", "code", "icon");
            var isDay = TryReadBoolean(current, "isDaylight") ?? InferCurrentIsDay(city);

            var data = new WeatherData
            {
                Provider = "qweather",
                CityCode = city.Code,
                CityName = city.Name,
                UpdatedAtMilliseconds = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Current = new CurrentWeather
                {
                    Temp = WeatherProviderHelpers.Rounded(currentTemp),
                    High = WeatherProviderHelpers.Rounded(ReadNestedNumber(daily[0], "temperatureMax", "value", "tempMax")),
                    Low = WeatherProviderHelpers.Rounded(ReadNestedNumber(daily[0], "temperatureMin", "value", "tempMin")),
                    Code = currentCode,
                    IsDay = isDay
                },
                Daily = daily.Select(item =>
                {
                    var date = ParseTimestamp(ReadString(item, "forecastStartTime", "fxDate")).DateTime;
                    return new DailyWeather
                    {
                        Day = WeatherProviderHelpers.DayName(date),
                        High = WeatherProviderHelpers.Rounded(ReadNestedNumber(item, "temperatureMax", "value", "tempMax")),
                        Low = WeatherProviderHelpers.Rounded(ReadNestedNumber(item, "temperatureMin", "value", "tempMin")),
                        Code = ReadDailyCode(item)
                    };
                }).ToList(),
                Hourly = hourly.Select(item =>
                {
                    var time = ParseTimestamp(ReadString(item, "forecastTime", "fxTime"));
                    return new HourlyWeather
                    {
                        Hour = time.Hour,
                        Temp = WeatherProviderHelpers.Rounded(ReadNestedNumber(item, "temperature", "value", "temp")),
                        Code = ReadNestedInt(item, "condition", "code", "icon"),
                        IsDay = TryReadBoolean(item, "isDaylight") ?? time.Hour is >= 7 and < 19
                    };
                }).ToList()
            };
            WeatherProviderHelpers.Validate(data);
            return data;
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex)
        {
            throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid, inner: ex);
        }
    }

    private (string Host, string Key) GetCredentials(DashboardConfig settings)
    {
        var host = settings.QWeatherApiHost.Trim().TrimEnd('/');
        if (!host.Contains("://", StringComparison.Ordinal)) host = "https://" + host;
        var key = _credentials.Read(settings.QWeatherApiKeyCredentialName);
        if (!Uri.TryCreate(host, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttps || string.IsNullOrWhiteSpace(key))
            throw new DashboardDataException(DashboardErrors.QWeatherCredentialsMissing);
        return (host, key);
    }

    private async Task<string> GetStringAsync(string url, string key, CancellationToken cancellationToken)
    {
        using var response = await SendAsync(url, key, cancellationToken).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
            throw new DashboardDataException(DashboardErrors.WeatherFetchFailed);
        return await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
    }

    private async Task<HttpResponseMessage> SendAsync(string url, string key, CancellationToken cancellationToken)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, url);
        request.Headers.TryAddWithoutValidation("X-QW-Api-Key", key);
        return await _http.SendAsync(request, cancellationToken).ConfigureAwait(false);
    }

    private static void EnsureSuccess(JsonElement root)
    {
        if (root.TryGetProperty("code", out var code) && code.ValueKind == JsonValueKind.String && code.GetString() != "200")
            throw new DashboardDataException(DashboardErrors.WeatherFetchFailed);
    }

    private static JsonElement GetObject(JsonElement root, string primary, string alternate) =>
        root.TryGetProperty(primary, out var value) ? value : root.GetProperty(alternate);

    private static JsonElement GetArray(JsonElement root, string primary, string alternate) =>
        root.TryGetProperty(primary, out var value) ? value : root.GetProperty(alternate);

    private static string ReadString(JsonElement item, string primary, string alternate) =>
        item.TryGetProperty(primary, out var value) ? value.GetString() ?? string.Empty : item.GetProperty(alternate).GetString() ?? string.Empty;

    private static double ReadNestedNumber(JsonElement item, string objectName, string valueName, string alternate)
    {
        if (item.TryGetProperty(objectName, out var nested))
        {
            var value = nested.GetProperty(valueName);
            return value.ValueKind == JsonValueKind.String
                ? double.Parse(value.GetString()!, CultureInfo.InvariantCulture)
                : value.GetDouble();
        }
        var alt = item.GetProperty(alternate);
        return alt.ValueKind == JsonValueKind.String ? double.Parse(alt.GetString()!, CultureInfo.InvariantCulture) : alt.GetDouble();
    }

    private static int ReadNestedInt(JsonElement item, string objectName, string valueName, string alternate)
    {
        if (item.TryGetProperty(objectName, out var nested))
        {
            var value = nested.GetProperty(valueName);
            return value.ValueKind == JsonValueKind.String ? int.Parse(value.GetString()!, CultureInfo.InvariantCulture) : value.GetInt32();
        }
        var alt = item.GetProperty(alternate);
        return alt.ValueKind == JsonValueKind.String ? int.Parse(alt.GetString()!, CultureInfo.InvariantCulture) : alt.GetInt32();
    }

    private static int ReadDailyCode(JsonElement item)
    {
        if (item.TryGetProperty("daytime", out var daytimeV1))
            return ReadNestedInt(daytimeV1, "condition", "code", "iconDay");
        if (item.TryGetProperty("daytimeForecast", out var daytime))
            return ReadNestedInt(daytime, "condition", "code", "iconDay");
        var value = item.GetProperty("iconDay");
        return value.ValueKind == JsonValueKind.String ? int.Parse(value.GetString()!, CultureInfo.InvariantCulture) : value.GetInt32();
    }

    private static bool? TryReadBoolean(JsonElement item, string name)
    {
        if (!item.TryGetProperty(name, out var value)) return null;
        return value.ValueKind switch
        {
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            JsonValueKind.Number => value.GetInt32() == 1,
            JsonValueKind.String when bool.TryParse(value.GetString(), out var parsed) => parsed,
            JsonValueKind.String => value.GetString() == "1",
            _ => null
        };
    }

    private static bool InferCurrentIsDay(ResolvedCity city)
    {
        try
        {
            var timezone = TimeZoneInfo.FindSystemTimeZoneById(city.Timezone);
            var local = TimeZoneInfo.ConvertTime(DateTimeOffset.UtcNow, timezone);
            return local.Hour is >= 7 and < 19;
        }
        catch { return DateTime.Now.Hour is >= 7 and < 19; }
    }

    private static DateTimeOffset ParseTimestamp(string value)
    {
        if (DateTimeOffset.TryParse(value, CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out var timestamp))
            return timestamp;
        if (DateOnly.TryParse(value, CultureInfo.InvariantCulture, DateTimeStyles.None, out var date))
            return new DateTimeOffset(date.ToDateTime(TimeOnly.MinValue));
        throw new FormatException("Invalid forecast timestamp.");
    }
}
