namespace KVMBridgeWindows.DisplayPower;

public readonly record struct PlatformCallResult(bool Succeeded, int Win32Error, bool TimedOut = false)
{
    public static PlatformCallResult Success() => new(true, 0);
    public static PlatformCallResult Failure(int win32Error, bool timedOut = false) =>
        new(false, win32Error, timedOut);
}

public interface IDisplayPowerPlatform
{
    PlatformCallResult SendMonitorPower(int powerParameter, uint timeoutMilliseconds);
    PlatformCallResult SetExecutionState(uint flags);
}
