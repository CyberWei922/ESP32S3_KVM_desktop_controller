using System.Globalization;
using System.Text.Json;
using KVMBridgeWindows.Configuration;

namespace KVMBridgeWindows.Dashboard.Weather;

public sealed class OpenMeteoWeatherProvider : IWeatherProvider
{
    private const string GeocodingBase = "https://geocoding-api.open-meteo.com/v1";
    private const string ForecastBase = "https://api.open-meteo.com/v1/forecast";
    private readonly HttpClient _http;

    public string ProviderId => "open_meteo";

    public OpenMeteoWeatherProvider(HttpClient http) => _http = http;

    public async Task<ResolvedCity> ResolveCityAsync(string cityInput, DashboardConfig settings, CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(cityInput))
            throw new DashboardDataException(DashboardErrors.CityCodeMissing);

        var input = cityInput.Trim();
        var url = long.TryParse(input, NumberStyles.None, CultureInfo.InvariantCulture, out _)
            ? $"{GeocodingBase}/get?id={Uri.EscapeDataString(input)}"
            : $"{GeocodingBase}/search?name={Uri.EscapeDataString(input)}&count=1&language=zh&countryCode=CN";

        try
        {
            using var response = await _http.GetAsync(url, cancellationToken).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
                throw new DashboardDataException(DashboardErrors.CityNotFound);
            using var document = await JsonDocument.ParseAsync(
                await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false),
                cancellationToken: cancellationToken).ConfigureAwait(false);

            var root = document.RootElement;
            JsonElement location;
            if (root.TryGetProperty("results", out var results) && results.ValueKind == JsonValueKind.Array && results.GetArrayLength() > 0)
                location = results[0];
            else if (root.TryGetProperty("id", out _))
                location = root;
            else
                throw new DashboardDataException(DashboardErrors.CityNotFound);

            var code = ReadStringOrNumber(location, "id");
            var name = location.GetProperty("name").GetString() ?? string.Empty;
            var latitude = location.GetProperty("latitude").GetDouble();
            var longitude = location.GetProperty("longitude").GetDouble();
            var timezone = location.TryGetProperty("timezone", out var tz) ? tz.GetString() ?? "Asia/Shanghai" : "Asia/Shanghai";
            if (string.IsNullOrWhiteSpace(code) || string.IsNullOrWhiteSpace(name))
                throw new DashboardDataException(DashboardErrors.CityNotFound);
            return new ResolvedCity(code, name, latitude, longitude, timezone);
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.CityNotFound, inner: ex);
        }
    }

    public async Task<WeatherData> FetchAsync(ResolvedCity city, DashboardConfig settings, CancellationToken cancellationToken)
    {
        var lat = city.Latitude.ToString("0.######", CultureInfo.InvariantCulture);
        var lon = city.Longitude.ToString("0.######", CultureInfo.InvariantCulture);
        var url = $"{ForecastBase}?latitude={lat}&longitude={lon}" +
            "&current=temperature_2m,weather_code,is_day" +
            "&hourly=temperature_2m,weather_code,is_day" +
            "&daily=weather_code,temperature_2m_max,temperature_2m_min" +
            "&timezone=auto&forecast_days=4";
        try
        {
            using var response = await _http.GetAsync(url, cancellationToken).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
                throw new DashboardDataException(DashboardErrors.WeatherFetchFailed);
            var json = await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
            return ParseForecast(json, city);
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.WeatherFetchFailed, inner: ex);
        }
    }

    internal static WeatherData ParseForecast(string json, ResolvedCity city)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            var current = root.GetProperty("current");
            var daily = root.GetProperty("daily");
            var hourly = root.GetProperty("hourly");

            var dates = ReadStringArray(daily, "time").Select(ParseDate).ToArray();
            var highs = ReadDoubleArray(daily, "temperature_2m_max");
            var lows = ReadDoubleArray(daily, "temperature_2m_min");
            var dailyCodes = ReadIntArray(daily, "weather_code");
            if (dates.Length < 3 || highs.Length < 3 || lows.Length < 3 || dailyCodes.Length < 3)
                throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
            WeatherProviderHelpers.EnsureConsecutiveDays(dates.Take(3).ToArray());

            var currentTime = ParseDateTime(current.GetProperty("time").GetString()!);
            var allHourTimes = ReadStringArray(hourly, "time").Select(ParseDateTime).ToArray();
            var allHourTemps = ReadDoubleArray(hourly, "temperature_2m");
            var allHourCodes = ReadIntArray(hourly, "weather_code");
            var allHourIsDay = ReadIntArray(hourly, "is_day");
            var availableCount = new[] { allHourTimes.Length, allHourTemps.Length, allHourCodes.Length, allHourIsDay.Length }.Min();
            var selectedIndices = Enumerable.Range(0, availableCount)
                .Where(i => allHourTimes[i] > currentTime)
                .OrderBy(i => allHourTimes[i])
                .Take(5)
                .ToArray();
            if (selectedIndices.Length < 5)
                throw new DashboardDataException(DashboardErrors.HourlyForecastIncomplete);

            var data = new WeatherData
            {
                Provider = "open_meteo",
                CityCode = city.Code,
                CityName = city.Name,
                UpdatedAtMilliseconds = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Current = new CurrentWeather
                {
                    Temp = WeatherProviderHelpers.Rounded(current.GetProperty("temperature_2m").GetDouble()),
                    High = WeatherProviderHelpers.Rounded(highs[0]),
                    Low = WeatherProviderHelpers.Rounded(lows[0]),
                    Code = current.GetProperty("weather_code").GetInt32(),
                    IsDay = current.GetProperty("is_day").GetInt32() == 1
                },
                Daily = Enumerable.Range(0, 3).Select(i => new DailyWeather
                {
                    Day = WeatherProviderHelpers.DayName(dates[i]),
                    High = WeatherProviderHelpers.Rounded(highs[i]),
                    Low = WeatherProviderHelpers.Rounded(lows[i]),
                    Code = dailyCodes[i]
                }).ToList(),
                Hourly = selectedIndices.Select(i => new HourlyWeather
                {
                    Hour = allHourTimes[i].Hour,
                    Temp = WeatherProviderHelpers.Rounded(allHourTemps[i]),
                    Code = allHourCodes[i],
                    IsDay = allHourIsDay[i] == 1
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

    private static string ReadStringOrNumber(JsonElement value, string name)
    {
        var item = value.GetProperty(name);
        return item.ValueKind == JsonValueKind.String ? item.GetString() ?? string.Empty : item.GetRawText();
    }

    private static string[] ReadStringArray(JsonElement parent, string name) =>
        parent.GetProperty(name).EnumerateArray().Select(x => x.GetString() ?? string.Empty).ToArray();

    private static double[] ReadDoubleArray(JsonElement parent, string name) =>
        parent.GetProperty(name).EnumerateArray().Select(x => x.GetDouble()).ToArray();

    private static int[] ReadIntArray(JsonElement parent, string name) =>
        parent.GetProperty(name).EnumerateArray().Select(x => x.GetInt32()).ToArray();

    private static DateTime ParseDate(string value) =>
        DateTime.ParseExact(value, "yyyy-MM-dd", CultureInfo.InvariantCulture, DateTimeStyles.None);

    private static DateTime ParseDateTime(string value) =>
        DateTime.Parse(value, CultureInfo.InvariantCulture, DateTimeStyles.AllowWhiteSpaces);
}
