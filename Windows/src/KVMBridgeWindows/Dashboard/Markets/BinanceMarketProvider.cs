using System.Globalization;
using System.Text.Json;

namespace KVMBridgeWindows.Dashboard.Markets;

public sealed class BinanceMarketProvider : IMarketProvider
{
    private const string Url = "https://api.binance.com/api/v3/ticker/24hr?symbols=%5B%22BTCUSDT%22%2C%22DOGEUSDT%22%5D";
    private readonly HttpClient _http;

    public BinanceMarketProvider(HttpClient http) => _http = http;

    public async Task<IReadOnlyList<MarketItem>> FetchAsync(CancellationToken cancellationToken)
    {
        try
        {
            using var response = await _http.GetAsync(Url, cancellationToken).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
                throw new DashboardDataException(DashboardErrors.MarketFetchFailed);
            return Parse(await response.Content.ReadAsStringAsync(cancellationToken).ConfigureAwait(false));
        }
        catch (DashboardDataException) { throw; }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new DashboardDataException(DashboardErrors.MarketFetchFailed, inner: ex);
        }
    }

    internal static IReadOnlyList<MarketItem> Parse(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            var bySymbol = document.RootElement.EnumerateArray().ToDictionary(
                x => x.GetProperty("symbol").GetString() ?? string.Empty,
                x => x,
                StringComparer.Ordinal);
            var result = new List<MarketItem>(2);
            foreach (var (apiSymbol, outputSymbol) in new[] { ("BTCUSDT", "BTC/USDT"), ("DOGEUSDT", "DOGE/USDT") })
            {
                var item = bySymbol[apiSymbol];
                var price = decimal.Parse(item.GetProperty("lastPrice").GetString()!, CultureInfo.InvariantCulture);
                var change = decimal.Parse(item.GetProperty("priceChangePercent").GetString()!, CultureInfo.InvariantCulture);
                if (price <= 0) throw new FormatException("price must be positive");
                result.Add(new MarketItem { Symbol = outputSymbol, Price = price, ChangePercent = change });
            }
            return result;
        }
        catch (Exception ex)
        {
            throw new DashboardDataException(DashboardErrors.MarketFetchFailed, inner: ex);
        }
    }
}
