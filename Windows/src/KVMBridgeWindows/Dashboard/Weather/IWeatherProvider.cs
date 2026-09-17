using KVMBridgeWindows.Configuration;

namespace KVMBridgeWindows.Dashboard.Weather;

public interface IWeatherProvider
{
    string ProviderId { get; }
    Task<ResolvedCity> ResolveCityAsync(string cityInput, DashboardConfig settings, CancellationToken cancellationToken);
    Task<WeatherData> FetchAsync(ResolvedCity city, DashboardConfig settings, CancellationToken cancellationToken);
}

internal static class WeatherProviderHelpers
{
    private static readonly string[] DayNames = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"];

    public static string DayName(DateTime value) => DayNames[(int)value.DayOfWeek];

    public static int Rounded(double value) => (int)Math.Round(value, MidpointRounding.AwayFromZero);

    public static void EnsureConsecutiveDays(IReadOnlyList<DateTime> dates)
    {
        if (dates.Count != 3 || dates[1].Date != dates[0].Date.AddDays(1) || dates[2].Date != dates[0].Date.AddDays(2))
            throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
    }

    public static void Validate(WeatherData data)
    {
        if (data.Daily.Count != 3)
            throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid, "daily forecast must contain today, tomorrow and the day after tomorrow");
        if (data.Hourly.Count != 5)
            throw new DashboardDataException(DashboardErrors.HourlyForecastIncomplete);
        if (data.Hourly.Any(x => x.Hour is < 0 or > 23) || data.Current.Code is < 0 or > 999 ||
            data.Daily.Any(x => x.Code is < 0 or > 999) || data.Hourly.Any(x => x.Code is < 0 or > 999))
            throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
        var temperatures = new[] { data.Current.Temp, data.Current.High, data.Current.Low }
            .Concat(data.Daily.SelectMany(x => new[] { x.High, x.Low }))
            .Concat(data.Hourly.Select(x => x.Temp));
        if (temperatures.Any(x => x is < -80 or > 70))
            throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
    }
}
