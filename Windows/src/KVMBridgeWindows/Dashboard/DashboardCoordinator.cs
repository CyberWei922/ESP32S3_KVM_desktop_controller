using System.Text;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Dashboard.Markets;
using KVMBridgeWindows.Dashboard.Weather;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Dashboard;

public interface IDashboardSnapshotSource
{
    DashboardSnapshot GetSnapshot();
}

public sealed class DashboardCoordinator : IDashboardSnapshotSource, IDisposable
{
    private static readonly TimeSpan[] RetryDelays =
    [
        TimeSpan.FromSeconds(30),
        TimeSpan.FromSeconds(60),
        TimeSpan.FromSeconds(120),
        TimeSpan.FromSeconds(300)
    ];

    private readonly ConfigurationService _config;
    private readonly IReadOnlyDictionary<string, IWeatherProvider> _weatherProviders;
    private readonly IReadOnlyList<IMarketProvider> _marketProviders;
    private readonly AppLogger _log;
    private readonly object _stateLock = new();
    private readonly SemaphoreSlim _weatherGate = new(1, 1);
    private readonly SemaphoreSlim _marketGate = new(1, 1);
    private CancellationTokenSource _cts = new();
    private Task? _weatherLoop;
    private Task? _marketLoop;
    private WeatherData? _weather;
    private MarketsData? _markets;
    private string? _weatherError = DashboardErrors.Loading;
    private string? _marketError = DashboardErrors.Loading;
    private bool _disposed;

    public DashboardCoordinator(
        ConfigurationService config,
        IEnumerable<IWeatherProvider> weatherProviders,
        IEnumerable<IMarketProvider> marketProviders,
        AppLogger log)
    {
        _config = config;
        _weatherProviders = weatherProviders.ToDictionary(x => x.ProviderId, StringComparer.Ordinal);
        _marketProviders = marketProviders.ToArray();
        _log = log;
    }

    public void Start()
    {
        if (_disposed || _weatherLoop is { IsCompleted: false }) return;
        _cts = new CancellationTokenSource();
        _weatherLoop = Task.Run(() => WeatherLoopAsync(_cts.Token));
        _marketLoop = Task.Run(() => MarketLoopAsync(_cts.Token));
    }

    public DashboardSnapshot GetSnapshot()
    {
        lock (_stateLock)
        {
            var error = _weatherError ?? _marketError;
            if (_weather == null && _markets == null && error == null)
                error = DashboardErrors.Loading;
            return new DashboardSnapshot(_weather, _markets, error);
        }
    }

    public async Task<ResolvedCity> ResolveCityAsync(DashboardConfig proposed, string cityInput, CancellationToken cancellationToken)
    {
        if (!_weatherProviders.TryGetValue(proposed.WeatherProvider, out var provider))
            throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
        var city = await provider.ResolveCityAsync(cityInput, proposed, cancellationToken).ConfigureAwait(false);
        ValidateResolvedCity(provider.ProviderId, city);
        return city;
    }

    public Task RefreshWeatherNowAsync(CancellationToken cancellationToken = default) =>
        RefreshWeatherAsync(cancellationToken);

    public Task RefreshMarketsNowAsync(CancellationToken cancellationToken = default) =>
        RefreshMarketsAsync(cancellationToken);

    private async Task WeatherLoopAsync(CancellationToken cancellationToken)
    {
        var retry = 0;
        while (!cancellationToken.IsCancellationRequested)
        {
            var success = await RefreshWeatherAsync(cancellationToken).ConfigureAwait(false);
            var delay = success
                ? TimeSpan.FromMinutes(_config.Current.Dashboard.WeatherRefreshMinutes)
                : RetryDelays[Math.Min(retry++, RetryDelays.Length - 1)];
            if (success) retry = 0;
            try { await Task.Delay(delay, cancellationToken).ConfigureAwait(false); }
            catch (OperationCanceledException) { break; }
        }
    }

    private async Task MarketLoopAsync(CancellationToken cancellationToken)
    {
        var retry = 0;
        while (!cancellationToken.IsCancellationRequested)
        {
            var success = await RefreshMarketsAsync(cancellationToken).ConfigureAwait(false);
            var delay = success
                ? TimeSpan.FromSeconds(10)
                : RetryDelays[Math.Min(retry++, RetryDelays.Length - 1)];
            if (success) retry = 0;
            try { await Task.Delay(delay, cancellationToken).ConfigureAwait(false); }
            catch (OperationCanceledException) { break; }
        }
    }

    private async Task<bool> RefreshWeatherAsync(CancellationToken cancellationToken)
    {
        if (!await _weatherGate.WaitAsync(0, cancellationToken).ConfigureAwait(false)) return true;
        try
        {
            var settings = _config.Current.Dashboard;
            if (!settings.Enabled) return true;
            if (string.IsNullOrWhiteSpace(settings.CityCode) || settings.Latitude is null || settings.Longitude is null)
                throw new DashboardDataException(DashboardErrors.CityCodeMissing);
            if (!_weatherProviders.TryGetValue(settings.WeatherProvider, out var provider))
                throw new DashboardDataException(DashboardErrors.WeatherResponseInvalid);
            var city = new ResolvedCity(
                settings.CityCode,
                settings.CityName,
                settings.Latitude.Value,
                settings.Longitude.Value,
                settings.Timezone);
            ValidateResolvedCity(provider.ProviderId, city);
            var weather = await provider.FetchAsync(city, settings, cancellationToken).ConfigureAwait(false);
            WeatherProviderHelpers.Validate(weather);
            lock (_stateLock)
            {
                _weather = weather;
                _weatherError = null;
            }
            _log.Info($"Dashboard weather refreshed: provider={provider.ProviderId}, city={city.Name}");
            return true;
        }
        catch (DashboardDataException ex)
        {
            lock (_stateLock) _weatherError = ex.ErrorCode;
            _log.Warning($"Dashboard weather refresh failed: {ex.ErrorCode}");
            return false;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { return false; }
        catch
        {
            lock (_stateLock) _weatherError = DashboardErrors.WeatherFetchFailed;
            _log.Warning($"Dashboard weather refresh failed: {DashboardErrors.WeatherFetchFailed}");
            return false;
        }
        finally { _weatherGate.Release(); }
    }

    private async Task<bool> RefreshMarketsAsync(CancellationToken cancellationToken)
    {
        if (!await _marketGate.WaitAsync(0, cancellationToken).ConfigureAwait(false)) return true;
        try
        {
            if (!_config.Current.Dashboard.Enabled) return true;
            var tasks = _marketProviders.Select(x => x.FetchAsync(cancellationToken)).ToArray();
            await Task.WhenAll(tasks).ConfigureAwait(false);
            var items = tasks.SelectMany(x => x.Result).ToList();
            var required = new[] { "BTC/USDT", "DOGE/USDT", "USD/CNY" };
            if (items.Count != 3 || required.Any(symbol => items.All(x => x.Symbol != symbol)) || items.Any(x => x.Price <= 0))
                throw new DashboardDataException(DashboardErrors.MarketFetchFailed);
            var markets = new MarketsData
            {
                UpdatedAtMilliseconds = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                BtcUsdt = items.Single(x => x.Symbol == "BTC/USDT"),
                DogeUsdt = items.Single(x => x.Symbol == "DOGE/USDT"),
                UsdCny = items.Single(x => x.Symbol == "USD/CNY")
            };
            lock (_stateLock)
            {
                _markets = markets;
                _marketError = null;
            }
            _log.Info("Dashboard markets refreshed.");
            return true;
        }
        catch (DashboardDataException ex)
        {
            lock (_stateLock) _marketError = ex.ErrorCode;
            _log.Warning($"Dashboard markets refresh failed: {ex.ErrorCode}");
            return false;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { return false; }
        catch
        {
            lock (_stateLock) _marketError = DashboardErrors.MarketFetchFailed;
            _log.Warning($"Dashboard markets refresh failed: {DashboardErrors.MarketFetchFailed}");
            return false;
        }
        finally { _marketGate.Release(); }
    }

    private static void ValidateResolvedCity(string provider, ResolvedCity city)
    {
        if (Encoding.UTF8.GetByteCount(provider) > 15 || string.IsNullOrWhiteSpace(city.Code) ||
            string.IsNullOrWhiteSpace(city.Name) || Encoding.UTF8.GetByteCount(city.Code) >= 32 ||
            Encoding.UTF8.GetByteCount(city.Name) >= 32 || double.IsNaN(city.Latitude) || double.IsNaN(city.Longitude) ||
            city.Latitude is < -90 or > 90 || city.Longitude is < -180 or > 180)
            throw new DashboardDataException(DashboardErrors.CityNotFound);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _cts.Cancel();
        try
        {
            var loops = new[] { _weatherLoop, _marketLoop }.Where(x => x != null).Cast<Task>().ToArray();
            if (loops.Length > 0) Task.WaitAll(loops, TimeSpan.FromSeconds(2));
        }
        catch { }
        _cts.Dispose();
        _weatherGate.Dispose();
        _marketGate.Dispose();
    }
}
