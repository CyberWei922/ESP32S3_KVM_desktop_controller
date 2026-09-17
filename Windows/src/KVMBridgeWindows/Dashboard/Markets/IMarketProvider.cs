namespace KVMBridgeWindows.Dashboard.Markets;

public interface IMarketProvider
{
    Task<IReadOnlyList<MarketItem>> FetchAsync(CancellationToken cancellationToken);
}
