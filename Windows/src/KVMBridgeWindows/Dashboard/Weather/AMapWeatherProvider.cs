using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Settings;

namespace KVMBridgeWindows.Dashboard.Weather;

public sealed partial class AMapWeatherProvider : IWeatherProvider
{
    private const string WeatherUrl = "https://restapi.amap.com/v3/weather/weatherInfo";
    private const string GeocodeUrl = "https://restapi.amap.com/v3/geocode/geo";
    private readonly HttpClient _http;
    private readonly ICredentialStore _credentials;
    private readonly OpenMeteoWeatherProvider _openMeteo;
    private readonly AppLogger _log;

    public string ProviderId => "amap";

    public AMapWeatherProvider(HttpClient http, ICredentialStore credentials, OpenMeteoWeatherProvider openMeteo, AppLogger log)
    {
        _http = http;
        _credentials = credentials;
        _openMeteo = openMeteo;
        _log = log;
    }

    public async Task<ResolvedCity> ResolveCityAsync(string cityInput, DashboardConfig settings, CancellationToken cancellationToken)
    {
        var adcode = cityInput.Trim();
        if (!AdcodePattern().IsMatch(adcode))
            throw new DashboardDataException(string.IsNullOrEmpty(adcode) ? DashboardErrors.CityCodeMissing : DashboardErrors.CityNotFound);
        var key = GetKey(settings);
        try
        {
            var liveJson = await GetStringAsync(
                $"{WeatherUrl}?city={Uri.EscapeDataString(adcode)}&extensions=base&output=json&key={Uri.EscapeDataString(key)}",
                DashboardErrors.CityNotFound,
                cancellationToken).ConfigureAwait(false);
            using var liveDocument = JsonDocument.Parse(liveJson);
            EnsureAmapSuccess(liveDocument.RootElement, DashboardErrors.CityNotFound);
            var lives = liveDocument.RootElement.GetProperty("lives");
            if (lives.GetArrayLength() == 0) throw new DashboardDataException(DashboardErrors.CityNotFound);
            var cityName = lives[0].GetProperty("city").GetString() ?? string.Empty;
            var canonicalCode = lives[0].GetProperty("adcode").GetString() ?? adcode;

            var geoJson = await GetStringAsync(
                $"{GeocodeUrl}?address={Uri.EscapeDataString(cityName)}&city={Uri.EscapeDataString(canonicalCode)}&output=json&key={Uri.EscapeDataString(key)}",
                DashboardErrors.CityNotFound,
                cancellationToken).ConfigureAwait(false);
            using var geoDocument = JsonDocument.Parse(geoJson);
            EnsureAmapSuccess(geoDocument.RootElement, DashboardErrors.CityNotFound);
            var geocodes = geoDocument.RootElement.GetProperty("geocodes");
            if (geocodes.GetArrayLength() == 0) throw new DashboardDataException(DashboardErrors.CityNotFound);
            var coordinates = (geocodes[0].GetProperty("location").GetString() ?? string.Empty).Split(',');
            if (coordinates.Length != 2) throw new DashboardDataException(DashboardErrors.CityNotFound);
            return new ResolvedCity(
                canonicalCode,
                cityName,
                double.Parse(coordinates[1], CultureInfo.InvariantCulture),
                double.Parse(coordinates[0], CultureInfo.InvariantCulture),
                "Asia/Shanghai");
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.CityNotFound, inner: ex);
        }
    }

    public async Task<WeatherData> FetchAsync(ResolvedCity city, DashboardConfig settings, CancellationToken cancellationToken)
    {
        var key = GetKey(settings);
        try
        {
            var liveTask = GetStringAsync(
                $"{WeatherUrl}?city={Uri.EscapeDataString(city.Code)}&extensions=base&output=json&key={Uri.EscapeDataString(key)}",
                DashboardErrors.WeatherFetchFailed,
                cancellationToken);
            var forecastTask = GetStringAsync(
                $"{WeatherUrl}?city={Uri.EscapeDataString(city.Code)}&extensions=all&output=json&key={Uri.EscapeDataString(key)}",
                DashboardErrors.WeatherFetchFailed,
                cancellationToken);
            var hourlyTask = _openMeteo.FetchAsync(city, settings, cancellationToken);
            await Task.WhenAll(liveTask, forecastTask, hourlyTask).ConfigureAwait(false);
            var data = ParseWeather(liveTask.Result, forecastTask.Result, city, hourlyTask.Result.Hourly);
            _log.Info("AMap weather refreshed; hourly forecast supplied by Open-Meteo fallback.");
            return data;
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.WeatherFetchFailed, inner: ex);
        }
    }

    internal static WeatherData ParseWeather(string liveJson, string forecastJson, ResolvedCity city, IReadOnlyList<HourlyWeather> hourly)
    {
        try
        {
            using var liveDocument = JsonDocument.Parse(liveJson);
            using var forecastDocument = JsonDocument.Parse(forecastJson);
            EnsureAmapSuccess(liveDocument.RootElement, DashboardErrors.WeatherResponseInvalid);
            EnsureAmapSuccess(forecastDocument.RootElement, DashboardErrors.WeatherResponseInvalid);
            var liveArray = liveDocument.RootElement.GetProperty("lives");
            var forecastArray = forecastDocument.RootElement.GetProperty("forecasts");
            if (liveArray.GetArrayLength() == 0 || forecastArray.GetArrayLength() == 0)
                throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
            var live = liveArray[0];
            var casts = forecastArray[0].GetProperty("casts").EnumerateArray().Take(3).ToArray();
            if (casts.Length != 3) throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
            if (hourly.Count != 5) throw new DashboardDataException(DashboardErrors.HourlyForecastIncomplete);
            WeatherProviderHelpers.EnsureConsecutiveDays(casts.Select(cast =>
                DateTime.ParseExact(cast.GetProperty("date").GetString()!, "yyyy-MM-dd", CultureInfo.InvariantCulture)).ToArray());
            var now = DateTime.Now;
            var data = new WeatherData
            {
                Provider = "amap",
                CityCode = city.Code,
                CityName = city.Name,
                UpdatedAtMilliseconds = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Current = new CurrentWeather
                {
                    Temp = ParseRounded(live, "temperature_float", "temperature"),
                    High = ParseRounded(casts[0], "daytemp_float", "daytemp"),
                    Low = ParseRounded(casts[0], "nighttemp_float", "nighttemp"),
                    Code = WeatherCodeMapper.FromAmapText(live.GetProperty("weather").GetString()),
                    IsDay = now.Hour is >= 6 and < 18
                },
                Daily = casts.Select(cast => new DailyWeather
                {
                    Day = WeatherProviderHelpers.DayName(DateTime.ParseExact(cast.GetProperty("date").GetString()!, "yyyy-MM-dd", CultureInfo.InvariantCulture)),
                    High = ParseRounded(cast, "daytemp_float", "daytemp"),
                    Low = ParseRounded(cast, "nighttemp_float", "nighttemp"),
                    Code = WeatherCodeMapper.FromAmapText(cast.GetProperty("dayweather").GetString())
                }).ToList(),
                Hourly = hourly.Select(x => new HourlyWeather { Hour = x.Hour, Temp = x.Temp, Code = x.Code, IsDay = x.IsDay }).ToList()
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

    private string GetKey(DashboardConfig settings)
    {
        var key = _credentials.Read(settings.AMapApiKeyCredentialName);
        if (string.IsNullOrWhiteSpace(key))
            throw new DashboardDataException(DashboardErrors.WeatherFetchFailed, "AMap credential is missing");
        return key;
    }

    private async Task<string> GetStringAsync(string url, string errorCode, CancellationToken cancellationToken)
    {
        using var response = await _http.GetAsync(url, cancellationToken).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode) throw new DashboardDataException(errorCode);
        return await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false);
    }

    private static void EnsureAmapSuccess(JsonElement root, string errorCode)
    {
        if (!root.TryGetProperty("status", out var status) || status.GetString() != "1")
            throw new DashboardDataException(errorCode);
    }

    private static int ParseRounded(JsonElement item, string preferred, string fallback)
    {
        var value = item.TryGetProperty(preferred, out var precise) && precise.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(precise.GetString())
            ? precise.GetString()!
            : item.GetProperty(fallback).GetString()!;
        return WeatherProviderHelpers.Rounded(double.Parse(value, CultureInfo.InvariantCulture));
    }

    [GeneratedRegex("^[0-9]{6}$", RegexOptions.CultureInvariant)]
    private static partial Regex AdcodePattern();
}
