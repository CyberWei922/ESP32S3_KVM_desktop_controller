using System.Text.Json;

namespace KVMBridgeWindows.Dashboard.Markets;

public sealed class ExchangeRateProvider : IMarketProvider
{
    private const string Url = "https://api.frankfurter.app/latest?from=USD&to=CNY";
    private readonly HttpClient _http;

    public ExchangeRateProvider(HttpClient http) => _http = http;

    public async Task<IReadOnlyList<MarketItem>> FetchAsync(CancellationToken cancellationToken)
    {
        try
        {
            using var response = await _http.GetAsync(Url, cancellationToken).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
                throw new DashboardDataException(DashboardErrors.MarketFetchFailed);
            return [Parse(await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false))];
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.MarketFetchFailed, inner: ex);
        }
    }

    internal static MarketItem Parse(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            var price = document.RootElement.GetProperty("rates").GetProperty("CNY").GetDecimal();
            if (price <= 0) throw new FormatException("rate must be positive");
            return new MarketItem { Symbol = "USD/CNY", Price = price, ChangePercent = 0.0m };
        }
        catch (Exception ex)
        {
            throw new DashboardDataException(DashboardErrors.MarketFetchFailed, inner: ex);
        }
    }
}
