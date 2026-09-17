using System;
using System.Text;
using System.Text.Json;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.Protocol;

public sealed class ProtocolCodec
{
    private static readonly JsonSerializerOptions Opts = new()
    {
        WriteIndented = false,
        DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.Never
    };

    private readonly string _deviceId;
    private readonly AppLogger _log;

    public ProtocolCodec(string deviceId, AppLogger log)
    {
        _deviceId = deviceId;
        _log = log;
    }

    public string EncodeHello() =>
        JsonSerializer.Serialize(new HelloMessage { DeviceId = _deviceId }, Opts);

    public string EncodeTelemetry(TelemetryMessage msg) =>
        JsonSerializer.Serialize(msg, Opts);

    public string EncodeCommandResult(ICommandResultMessage msg) =>
        JsonSerializer.Serialize(msg, msg.GetType(), Opts);

    /// <summary>
    /// Validates and parses an incoming message from ESP32.
    /// Returns IncomingCommand if it is a structurally valid command for this device,
    /// null if the message should be ignored or rejected; parseError explains logged protocol violations.
    /// </summary>
    public IncomingCommand? TryParseCommand(string json, out string? parseError)
    {
        parseError = null;

        if (string.IsNullOrWhiteSpace(json))
        {
            parseError = "empty message";
            return null;
        }

        if (Encoding.UTF8.GetByteCount(json) > 2048)
        {
            parseError = "message exceeds 2048 bytes";
            return null;
        }

        JsonDocument doc;
        try { doc = JsonDocument.Parse(json); }
        catch (JsonException ex)
        {
            parseError = $"invalid JSON: {ex.Message}";
            return null;
        }

        using (doc)
        {
            var root = doc.RootElement;

            if (root.ValueKind != JsonValueKind.Object)
            {
                parseError = "message must be a JSON object";
                return null;
            }

            if (!root.TryGetProperty("type", out var typeProp) || typeProp.ValueKind != JsonValueKind.String)
            {
                parseError = "missing or invalid type";
                return null;
            }
            var type = typeProp.GetString();
            if (type != "command")
            {
                // Not a command; ignore gracefully
                return null;
            }

            // Structural validation
            if (!root.TryGetProperty("protocol_version", out var versionProp) ||
                versionProp.ValueKind != JsonValueKind.Number ||
                !versionProp.TryGetInt32(out var version) || version != 1)
            {
                parseError = "unsupported or invalid protocol_version";
                return null;
            }
            if (!TryReadBoundedString(root, "request_id", 128, out var requestId))
            {
                parseError = "invalid request_id";
                return null;
            }
            if (!TryReadBoundedString(root, "target_device_id", 128, out var targetDeviceId))
            {
                parseError = "invalid target_device_id";
                return null;
            }
            if (targetDeviceId != _deviceId)
                return null;

            if (!TryReadBoundedString(root, "action", 128, out var action))
            {
                parseError = "invalid action";
                return null;
            }

            string? target = null;
            if (action == "switch_display")
            {
                if (!TryReadBoundedString(root, "target", 16, out target) ||
                    (target != "mac" && target != "windows"))
                {
                    parseError = "unknown or invalid target";
                    return null;
                }
            }
            else if (action is "display_sleep" or "display_wake")
            {
                if (root.TryGetProperty("target", out _) ||
                    root.TryGetProperty("requested_input", out _) ||
                    root.TryGetProperty("vcp_code", out _))
                {
                    parseError = $"{action} must not contain display input fields";
                    return null;
                }
            }

            return new IncomingCommand
            {
                Type = "command",
                ProtocolVersion = version,
                RequestId = requestId,
                TargetDeviceId = targetDeviceId,
                Action = action,
                Target = target
            };
        }
    }

    private static bool TryReadBoundedString(
        JsonElement root,
        string propertyName,
        int maxUtf8Bytes,
        out string value)
    {
        value = string.Empty;
        if (!root.TryGetProperty(propertyName, out var prop) || prop.ValueKind != JsonValueKind.String)
            return false;
        value = prop.GetString() ?? string.Empty;
        return value.Length > 0 && Encoding.UTF8.GetByteCount(value) <= maxUtf8Bytes;
    }

    public CommandResultMessage BuildFailureResult(string requestId, int requestedInput, string errorCode)
    {
        var code = errorCode.Length > 63 ? errorCode[..63] : errorCode;
        return new CommandResultMessage
        {
            RequestId = requestId,
            DeviceId = _deviceId,
            Success = false,
            WriteSucceeded = false,
            Confirmed = false,
            RequestedInput = requestedInput,
            ReadBackInput = null,
            Error = code
        };
    }

    public CommandResultMessage BuildSuccessUnconfirmed(string requestId, int requestedInput)
    {
        return new CommandResultMessage
        {
            RequestId = requestId,
            DeviceId = _deviceId,
            Success = true,
            WriteSucceeded = true,
            Confirmed = false,
            RequestedInput = requestedInput,
            ReadBackInput = null,
            Error = "input_written_but_unconfirmed"
        };
    }

    public CommandResultMessage BuildSuccessConfirmed(string requestId, int requestedInput, int readBack)
    {
        return new CommandResultMessage
        {
            RequestId = requestId,
            DeviceId = _deviceId,
            Success = true,
            WriteSucceeded = true,
            Confirmed = true,
            RequestedInput = requestedInput,
            ReadBackInput = readBack,
            Error = null
        };
    }

    public DisplayPowerCommandResultMessage BuildDisplayPowerResult(
        string requestId,
        string action,
        bool success,
        string displayPowerState,
        string? error)
    {
        return new DisplayPowerCommandResultMessage
        {
            RequestId = requestId,
            DeviceId = _deviceId,
            Action = action,
            Success = success,
            DisplayPowerState = displayPowerState,
            Error = success ? null : error
        };
    }
}
