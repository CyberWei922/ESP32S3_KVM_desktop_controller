namespace KVMBridgeWindows.Ddc;

public sealed class MonitorInfo
{
    /// <summary>Stable ID used in config (combination of device path + description).</summary>
    public string StableId { get; init; } = "";
    /// <summary>Friendly display name shown in tray menu.</summary>
    public string FriendlyName { get; init; } = "";
    /// <summary>GDI device name e.g. \\.\DISPLAY1</summary>
    public string DeviceName { get; init; } = "";
    /// <summary>Physical monitor description from Dxva2.</summary>
    public string Description { get; init; } = "";
    /// <summary>HMONITOR handle value (not persisted).</summary>
    public nint HMonitor { get; init; }
    /// <summary>Index of physical monitor within this HMONITOR.</summary>
    public uint PhysicalIndex { get; init; }
    /// <summary>Whether DDC channel could be opened during last scan.</summary>
    public bool DdcAvailable { get; set; }
    /// <summary>Short suffix shown in menu (last 8 chars of StableId).</summary>
    public string IdSuffix => StableId.Length > 8 ? StableId[^8..] : StableId;
}
