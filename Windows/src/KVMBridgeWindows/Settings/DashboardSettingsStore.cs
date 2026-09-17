using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Dashboard;

namespace KVMBridgeWindows.Settings;

public sealed class DashboardSettingsStore
{
    private readonly ConfigurationService _config;
    private readonly DashboardCoordinator _dashboard;
    private readonly ICredentialStore _credentials;

    public DashboardSettingsStore(ConfigurationService config, DashboardCoordinator dashboard, ICredentialStore credentials)
    {
        _config = config;
        _dashboard = dashboard;
        _credentials = credentials;
    }

    public async Task<DashboardSettingsResult> ApplyAsync(DashboardSettingsUpdate update, CancellationToken cancellationToken = default)
    {
        var current = _config.Current.Dashboard;
        var proposed = new DashboardConfig
        {
            Enabled = current.Enabled,
            SendIntervalSeconds = current.SendIntervalSeconds,
            WeatherRefreshMinutes = current.WeatherRefreshMinutes,
            WeatherProvider = update.Provider.Trim().ToLowerInvariant(),
            CityCode = current.CityCode,
            CityName = current.CityName,
            Latitude = current.Latitude,
            Longitude = current.Longitude,
            Timezone = current.Timezone,
            QWeatherApiHost = update.QWeatherHost.Trim().TrimEnd('/'),
            QWeatherApiKeyCredentialName = current.QWeatherApiKeyCredentialName,
            AMapApiKeyCredentialName = current.AMapApiKeyCredentialName
        };

        try
        {
            if (!string.IsNullOrWhiteSpace(update.ApiKey))
            {
                var target = proposed.WeatherProvider == "qweather"
                    ? proposed.QWeatherApiKeyCredentialName
                    : proposed.WeatherProvider == "amap"
                        ? proposed.AMapApiKeyCredentialName
                        : null;
                if (target != null) _credentials.Write(target, update.ApiKey.Trim());
            }

            var city = await _dashboard.ResolveCityAsync(proposed, update.CityInput, cancellationToken).ConfigureAwait(false);
            _config.UpdateAndSave(config =>
            {
                config.Dashboard.WeatherProvider = proposed.WeatherProvider;
                config.Dashboard.QWeatherApiHost = proposed.QWeatherApiHost;
                config.Dashboard.CityCode = city.Code;
                config.Dashboard.CityName = city.Name;
                config.Dashboard.Latitude = city.Latitude;
                config.Dashboard.Longitude = city.Longitude;
                config.Dashboard.Timezone = city.Timezone;
            });
            await _dashboard.RefreshWeatherNowAsync(cancellationToken).ConfigureAwait(false);
            return new DashboardSettingsResult(true, null, city);
        }
        catch (DashboardDataException ex)
        {
            return new DashboardSettingsResult(false, ex.ErrorCode, null);
        }
        catch (OperationCanceledException) { throw; }
        catch
        {
            return new DashboardSettingsResult(false, DashboardErrors.WeatherFetchFailed, null);
        }
    }
}
