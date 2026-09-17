using System;
using System.Collections.Generic;
using System.Diagnostics;
using KVMBridgeWindows.Configuration;
using KVMBridgeWindows.Ddc;
using KVMBridgeWindows.DisplayPower;
using KVMBridgeWindows.Logging;
using KVMBridgeWindows.Protocol;

namespace KVMBridgeWindows.Networking;

public sealed class CommandDispatcher
{
    private readonly ConfigurationService _config;
    private readonly IDdcService _ddc;
    private readonly IMonitorDiscoveryService _discovery;
    private readonly ProtocolCodec _codec;
    private readonly DisplayPowerService _displayPower;
    private readonly AppLogger _log;

    private readonly LinkedList<(string Id, ICommandResultMessage Result)> _cache = new();
    private readonly object _cacheLock = new();
    private readonly object _executionLock = new();
    private const int MaxCacheSize = 64;
    private static readonly TimeSpan DdcTimeout = TimeSpan.FromSeconds(4);

    public CommandDispatcher(
        ConfigurationService config,
        IDdcService ddc,
        IMonitorDiscoveryService discovery,
        ProtocolCodec codec,
        DisplayPowerService displayPower,
        AppLogger log)
    {
        _config = config;
        _ddc = ddc;
        _discovery = discovery;
        _codec = codec;
        _displayPower = displayPower;
        _log = log;
    }

    public ICommandResultMessage Execute(IncomingCommand cmd)
    {
        var elapsed = Stopwatch.StartNew();
        _log.Info($"Command received: action={cmd.Action} request_id={cmd.RequestId} target={cmd.Target ?? "(none)"}");

        lock (_executionLock)
        {
            var cached = FindCached(cmd.RequestId);
            if (cached != null)
            {
                elapsed.Stop();
                _log.Info($"Command complete: action={cmd.Action} request_id={cmd.RequestId} duplicate=true success={cached.Success} elapsed_ms={elapsed.ElapsedMilliseconds}");
                return cached;
            }

            var result = ExecuteInternal(cmd);
            CacheResult(cmd.RequestId, result);
            elapsed.Stop();
            _log.Info($"Command complete: action={cmd.Action} request_id={cmd.RequestId} duplicate=false success={result.Success} elapsed_ms={elapsed.ElapsedMilliseconds}");
            return result;
        }
    }

    private ICommandResultMessage ExecuteInternal(IncomingCommand cmd) => cmd.Action switch
    {
        "switch_display" => ExecuteSwitchDisplay(cmd),
        "display_sleep" or "display_wake" => ExecuteDisplayPower(cmd),
        _ => UnsupportedAction(cmd)
    };

    private ICommandResultMessage ExecuteSwitchDisplay(IncomingCommand cmd)
    {
        var cfg = _config.Current;
        var target = cmd.Target!;

        if (!cfg.Kvm.Enabled)
        {
            _log.Info($"KVM disabled; returning kvm_disabled for {cmd.RequestId}");
            return _codec.BuildFailureResult(
                cmd.RequestId,
                TargetToInput(target, cfg.Kvm),
                "kvm_disabled");
        }

        if (string.IsNullOrWhiteSpace(cfg.Kvm.DisplayId))
        {
            _log.Warning("No display selected.");
            return _codec.BuildFailureResult(cmd.RequestId, 0, "display_not_selected");
        }

        _discovery.Scan();
        var monitor = _discovery.FindById(cfg.Kvm.DisplayId);
        if (monitor == null)
        {
            _log.Warning($"Configured display '{cfg.Kvm.DisplayId}' not found.");
            return _codec.BuildFailureResult(cmd.RequestId, 0, "display_not_found");
        }

        var requestedInput = TargetToInput(target, cfg.Kvm);
        _log.Info($"Writing VCP 0x{cfg.Kvm.VcpCode:X2}={requestedInput} to monitor {monitor.IdSuffix}");

        var ddcResult = _ddc.WriteInput(
            cfg.Kvm.DisplayId,
            (byte)cfg.Kvm.VcpCode,
            (uint)requestedInput,
            cfg.Kvm.VerifyReadback,
            DdcTimeout);

        if (!ddcResult.WriteSucceeded)
        {
            _log.Warning($"DDC write failed: {ddcResult.ErrorCode}");
            return _codec.BuildFailureResult(
                cmd.RequestId,
                requestedInput,
                ddcResult.ErrorCode ?? "ddc_write_failed");
        }

        if (ddcResult.Confirmed)
            return _codec.BuildSuccessConfirmed(cmd.RequestId, requestedInput, ddcResult.ReadBackInput!.Value);

        return _codec.BuildSuccessUnconfirmed(cmd.RequestId, requestedInput);
    }

    private ICommandResultMessage ExecuteDisplayPower(IncomingCommand cmd)
    {
        var cfg = _config.Current;
        if (!cfg.DisplayPower.Enabled)
            return _codec.BuildDisplayPowerResult(
                cmd.RequestId, cmd.Action, false, "unchanged", "display_power_disabled");

        if (cmd.Action == "display_sleep" &&
            cfg.Connection.Enabled &&
            cfg.DisplayPower.PreventSystemSleepWhileEnabled &&
            _displayPower.AssertionState != SystemSleepAssertionState.Active)
        {
            return _codec.BuildDisplayPowerResult(
                cmd.RequestId, cmd.Action, false, "unchanged", "system_sleep_not_prevented");
        }

        var operation = cmd.Action == "display_sleep"
            ? _displayPower.RequestSleep(cfg.DisplayPower.CommandTimeoutMilliseconds)
            : _displayPower.RequestWake(cfg.DisplayPower.CommandTimeoutMilliseconds);

        return _codec.BuildDisplayPowerResult(
            cmd.RequestId,
            cmd.Action,
            operation.Success,
            operation.DisplayPowerState,
            operation.Error);
    }

    private ICommandResultMessage UnsupportedAction(IncomingCommand cmd)
    {
        _log.Warning($"unsupported_action: action={cmd.Action} request_id={cmd.RequestId}");
        return _codec.BuildDisplayPowerResult(
            cmd.RequestId, cmd.Action, false, "unchanged", "unsupported_action");
    }

    private static int TargetToInput(string target, KvmConfig kvm) =>
        target == "mac" ? kvm.MacInput : kvm.WindowsInput;

    private ICommandResultMessage? FindCached(string id)
    {
        lock (_cacheLock)
        {
            for (var node = _cache.Last; node != null; node = node.Previous)
                if (node.Value.Id == id)
                    return node.Value.Result;
        }
        return null;
    }

    private void CacheResult(string id, ICommandResultMessage result)
    {
        lock (_cacheLock)
        {
            _cache.AddLast((id, result));
            while (_cache.Count > MaxCacheSize)
                _cache.RemoveFirst();
        }
    }
}
