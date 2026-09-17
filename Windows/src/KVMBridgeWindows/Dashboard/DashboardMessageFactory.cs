using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace KVMBridgeWindows.Dashboard;

public sealed class DashboardMessageFactory
{
    public const int MaximumUtf8Bytes = 2048;
    private const ulong MaximumJsonSafeSequence = 9_007_199_254_740_991;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = false,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never
    };

    private readonly string _deviceId;
    private ulong _sequence;

    public DashboardMessageFactory(string deviceId) => _deviceId = deviceId;

    public void ResetSequence() => _sequence = 0;

    public string CreateJson(DashboardSnapshot snapshot, long? timestampMilliseconds = null)
    {
        if (_sequence > MaximumJsonSafeSequence)
            throw new InvalidOperationException("Dashboard sequence exhausted; reconnect is required.");
        var sequence = _sequence;
        _sequence++;
        var message = new DashboardMessage
        {
            DeviceId = _deviceId,
            Sequence = sequence,
            TimestampMilliseconds = timestampMilliseconds ?? DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            Weather = snapshot.Weather,
            Markets = snapshot.Markets,
            Error = snapshot.Weather == null && snapshot.Markets == null
                ? NormalizeError(snapshot.Error) ?? DashboardErrors.Loading
                : NormalizeError(snapshot.Error)
        };

        var json = JsonSerializer.Serialize(message, JsonOptions);
        if (Encoding.UTF8.GetByteCount(json) <= MaximumUtf8Bytes)
            return json;

        message.Weather = null;
        message.Markets = null;
        message.Error = DashboardErrors.MessageTooLarge;
        return JsonSerializer.Serialize(message, JsonOptions);
    }

    private static string? NormalizeError(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;
        return Encoding.UTF8.GetByteCount(value) <= 63 ? value : DashboardErrors.WeatherResponseInvalid;
    }
}
