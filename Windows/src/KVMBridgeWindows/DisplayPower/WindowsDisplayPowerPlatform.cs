using System;
using System.Runtime.InteropServices;

namespace KVMBridgeWindows.DisplayPower;

public sealed class WindowsDisplayPowerPlatform : IDisplayPowerPlatform
{
    private static readonly IntPtr HwndBroadcast = new(0xffff);
    private const uint WmSysCommand = 0x0112;
    private const uint ScMonitorPower = 0xF170;
    private const uint SmtoBlock = 0x0001;
    private const uint SmtoAbortIfHung = 0x0002;
    private const int ErrorTimeout = 1460;

    public PlatformCallResult SendMonitorPower(int powerParameter, uint timeoutMilliseconds)
    {
        var result = SendMessageTimeout(
            HwndBroadcast,
            WmSysCommand,
            new UIntPtr(ScMonitorPower),
            new IntPtr(powerParameter),
            SmtoBlock | SmtoAbortIfHung,
            timeoutMilliseconds,
            out _);

        if (result != IntPtr.Zero)
            return PlatformCallResult.Success();

        var error = Marshal.GetLastWin32Error();
        return PlatformCallResult.Failure(error, error == ErrorTimeout);
    }

    public PlatformCallResult SetExecutionState(uint flags)
    {
        var result = NativeSetThreadExecutionState(flags);
        if (result != 0)
            return PlatformCallResult.Success();

        return PlatformCallResult.Failure(Marshal.GetLastWin32Error());
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SendMessageTimeout(
        IntPtr hWnd,
        uint msg,
        UIntPtr wParam,
        IntPtr lParam,
        uint flags,
        uint timeout,
        out UIntPtr result);

    [DllImport("kernel32.dll", EntryPoint = "SetThreadExecutionState", SetLastError = true)]
    private static extern uint NativeSetThreadExecutionState(uint flags);
}
