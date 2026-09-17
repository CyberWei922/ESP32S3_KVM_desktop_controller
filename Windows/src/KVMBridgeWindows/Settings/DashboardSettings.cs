using KVMBridgeWindows.Dashboard;

namespace KVMBridgeWindows.Settings;

public sealed record DashboardSettingsUpdate(
    string Provider,
    string CityInput,
    string QWeatherHost,
    string ApiKey);

public sealed record DashboardSettingsResult(
    bool Success,
    string? Error,
    ResolvedCity? City);
