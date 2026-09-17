namespace KVMBridgeWindows.Dashboard.Weather;

public static class WeatherCodeMapper
{
    public static int FromAmapText(string? text)
    {
        var value = text?.Trim() ?? string.Empty;
        if (value.Contains("雷")) return 95;
        if (value.Contains("雨夹雪")) return 71;
        if (value.Contains("雪")) return 71;
        if (value.Contains("雨")) return 61;
        if (value.Contains("雾") || value.Contains("霾") || value.Contains("沙") || value.Contains("尘")) return 45;
        if (value.Contains("阴") || value.Contains("多云") || value.Contains("少云")) return 3;
        if (value.Contains("晴")) return 0;
        return 3;
    }
}
