using System;
using System.Diagnostics;
using KVMBridgeWindows.Logging;

namespace KVMBridgeWindows.DisplayPower;

public enum DisplayOutputState
{
    Active,
    SleepRequested,
    OutputSleeping,
    WakeRequested,
    Error
}

public enum SystemSleepAssertionState
{
    Disabled,
    Active,
    Warning
}

public readonly record struct DisplayPowerOperationResult(
    bool Success,
    string DisplayPowerState,
    string? Error);

public sealed class DisplayPowerService : IDisposable
{
    public const uint EsSystemRequired = 0x00000001;
    public const uint EsDisplayRequired = 0x00000002;
    public const uint EsContinuous = 0x80000000;

    private readonly IDisplayPowerPlatform _platform;
    private readonly AppLogger _log;
    private readonly object _gate = new();
    private DisplayOutputState _state = DisplayOutputState.Active;
    private SystemSleepAssertionState _assertionState = SystemSleepAssertionState.Disabled;
    private bool _disposed;

    public DisplayOutputState State { get { lock (_gate) return _state; } }
    public SystemSleepAssertionState AssertionState { get { lock (_gate) return _assertionState; } }

    public DisplayPowerService(IDisplayPowerPlatform platform, AppLogger log)
    {
        _platform = platform;
        _log = log;
    }

    public bool UpdateSystemSleepAssertion(
        bool connectionEnabled,
        bool displayPowerEnabled,
        bool preventSystemSleepWhileEnabled)
    {
        lock (_gate)
        {
            var shouldAssert = connectionEnabled && displayPowerEnabled && preventSystemSleepWhileEnabled;
            if (shouldAssert && _assertionState == SystemSleepAssertionState.Active)
                return true;
            if (!shouldAssert && _assertionState == SystemSleepAssertionState.Disabled)
                return true;

            var flags = shouldAssert ? EsContinuous | EsSystemRequired : EsContinuous;
            var result = _platform.SetExecutionState(flags);
            _log.Info($"SetThreadExecutionState(0x{flags:X8}) => {result.Succeeded} Win32Err={result.Win32Error}");

            if (shouldAssert)
            {
                _assertionState = result.Succeeded
                    ? SystemSleepAssertionState.Active
                    : SystemSleepAssertionState.Warning;
                if (!result.Succeeded)
                    _log.Warning("system_sleep_not_prevented: runtime system sleep assertion failed.");
                else
                    _log.Info("Runtime system sleep assertion established.");
                return result.Succeeded;
            }

            _assertionState = result.Succeeded
                ? SystemSleepAssertionState.Disabled
                : SystemSleepAssertionState.Warning;
            if (result.Succeeded)
                _log.Info("Runtime system sleep assertion released.");
            else
                _log.Warning("Runtime system sleep assertion release failed.");
            return result.Succeeded;
        }
    }

    public DisplayPowerOperationResult RequestSleep(int timeoutMilliseconds)
    {
        lock (_gate)
        {
            if (_state is DisplayOutputState.OutputSleeping or DisplayOutputState.SleepRequested)
            {
                _log.Info("display_sleep is already satisfied; returning idempotent success.");
                return Success("sleep_requested");
            }

            _state = DisplayOutputState.SleepRequested;
            var sw = Stopwatch.StartNew();
            var call = _platform.SendMonitorPower(2, (uint)timeoutMilliseconds);
            sw.Stop();
            _log.Info($"SendMessageTimeout(SC_MONITORPOWER, 2, {timeoutMilliseconds}ms) => {call.Succeeded} in {sw.ElapsedMilliseconds}ms Win32Err={call.Win32Error}");

            if (call.Succeeded)
            {
                _state = DisplayOutputState.OutputSleeping;
                return Success("sleep_requested");
            }

            _state = DisplayOutputState.Error;
            return Failure(call.TimedOut ? "display_power_timeout" : "display_sleep_failed");
        }
    }

    public DisplayPowerOperationResult RequestWake(int timeoutMilliseconds)
    {
        lock (_gate)
        {
            if (_state == DisplayOutputState.Active)
            {
                _log.Info("display_wake is already satisfied; returning idempotent success.");
                return Success("wake_requested");
            }

            _state = DisplayOutputState.WakeRequested;
            var sw = Stopwatch.StartNew();
            var monitorCall = _platform.SendMonitorPower(-1, (uint)timeoutMilliseconds);
            _log.Info($"SendMessageTimeout(SC_MONITORPOWER, -1, {timeoutMilliseconds}ms) => {monitorCall.Succeeded} Win32Err={monitorCall.Win32Error}");

            if (!monitorCall.Succeeded)
            {
                sw.Stop();
                _state = DisplayOutputState.Error;
                return Failure(monitorCall.TimedOut ? "display_power_timeout" : "display_wake_failed");
            }

            var idleReset = _platform.SetExecutionState(EsDisplayRequired);
            sw.Stop();
            _log.Info($"SetThreadExecutionState(ES_DISPLAY_REQUIRED) => {idleReset.Succeeded} in {sw.ElapsedMilliseconds}ms Win32Err={idleReset.Win32Error}");
            if (!idleReset.Succeeded)
            {
                _state = DisplayOutputState.Error;
                return Failure("display_wake_failed");
            }

            _state = DisplayOutputState.Active;
            return Success("wake_requested");
        }
    }

    private static DisplayPowerOperationResult Success(string state) => new(true, state, null);
    private static DisplayPowerOperationResult Failure(string error) => new(false, "unchanged", error);

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed) return;
            _disposed = true;
            var result = _platform.SetExecutionState(EsContinuous);
            _assertionState = result.Succeeded
                ? SystemSleepAssertionState.Disabled
                : SystemSleepAssertionState.Warning;
            _log.Info($"SetThreadExecutionState(ES_CONTINUOUS) on exit => {result.Succeeded} Win32Err={result.Win32Error}");
        }
    }
}
