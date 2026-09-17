# DDC 与 Windows 系统指标

## 1. DDC 首选方案

Windows v1 首选系统原生 Monitor Configuration API，不依赖外部 EXE。

需要 P/Invoke 的核心 API：

- User32：`EnumDisplayMonitors`、`GetMonitorInfo`
- Dxva2：`GetNumberOfPhysicalMonitorsFromHMONITOR`
- Dxva2：`GetPhysicalMonitorsFromHMONITOR`
- Dxva2：`SetVCPFeature`
- Dxva2：`GetVCPFeatureAndVCPFeatureReply`（仅在开启读回时）
- Dxva2：`DestroyPhysicalMonitors`

输入选择 VCP Feature Code 为 `0x60`（十进制 96）。X41Q 当前实测配置：Mac HDMI 2 = 6，Windows DP = 7。

所有原生句柄必须使用 `try/finally` 或 SafeHandle 等价机制释放。每个成功取得的物理显示器句柄最终必须调用 Destroy，不能依赖垃圾回收器。

## 2. 显示器识别

Windows 的一个 `HMONITOR` 可能对应多个物理显示器对象。发现流程必须保留映射关系，并为每个物理显示器生成稳定 ID。

稳定 ID 优先组合：

1. DisplayConfig/显示设备路径；
2. EDID 制造商、产品代码和序列号；
3. 物理显示器描述；
4. 仅作为展示信息的逻辑设备名。

友好名称不是唯一 ID。重新插拔、改变主副屏后，只要硬件身份未变，应尽量匹配回原显示器。

安全要求：

- 用户没有明确选择显示器时，KVM 保持关闭。
- 保存的 ID 找不到时返回 `display_not_found`，不得回退到第一台显示器。
- 找到多个同样候选时视为歧义，要求重新选择，不能猜测。
- 枚举列表应标明哪些物理显示器可以成功打开 DDC 通路。

## 3. DDC 命令执行

对一个合法命令：

1. 检查 KVM 已启用。
2. 检查显示器已选择且当前可发现。
3. 把 `target` 映射为输入值 6 或 7。
4. 在专用串行工作线程调用 `SetVCPFeature(handle, 0x60, value)`。
5. 记录调用耗时和 Win32 错误。
6. 默认不读回，立即生成 `input_written_but_unconfirmed` 成功结果。
7. 把结果放入高优先级出站队列。

完整处理与回包必须尽量在 4 秒内完成，因为 ESP32 在 5 秒时超时。逻辑超时使用 `ddc_operation_timed_out`。一次已超时的原生调用完成后，不得再触发第二次 DDC 或发送相互矛盾的第二个结果。

## 4. 为什么默认不做读回确认

X41Q 切换输入后 DDC 通路可能短暂消失，而且已有 Mac 端验证表明输入读回不可靠。因此：

- `SetVCPFeature` 返回成功代表写入成功。
- `confirmed` 保持 false。
- `read_back_input` 为 null。
- `error` 固定为 `input_written_but_unconfirmed`。
- `success` 与 `write_succeeded` 都为 true，ESP32 可以继续切 USB。

这里的 `error` 是“未确认状态码”，不是失败。将来只有真机确认 Windows 读回稳定，才能打开 `verify_readback`。

## 5. ControlMyMonitor 回退

ControlMyMonitor 不是默认依赖，只保留为诊断回退：

- 仅当 `ddc_backend` 明确设置为 `control_my_monitor` 时启用。
- EXE 路径和显示器 ID 必须由用户配置并验证存在。
- 使用直接进程参数，不通过 `cmd.exe` 或 PowerShell 拼接字符串。
- 命令形式：`ControlMyMonitor.exe /SetValue "<monitor-id>" 60 <value>`。
- 设置有限等待时间并检查退出码。
- 路径无效返回 `ddc_transport_unavailable`；非零退出或超时返回相应失败码。
- 发布包默认不捆绑该第三方 EXE。

如果原生 API 能稳定控制 X41Q，就不启用此回退。

## 6. CPU 温度

使用 `LibreHardwareMonitorLib`，不是 WMI `MSAcpi_ThermalZoneTemperature`，因为后者通常不是可靠的 CPU 封装温度。

采集规则：

- 打开 CPU 硬件监控。
- 更新 CPU 节点。
- 查找 `SensorType.Temperature`。
- 首选名称为 `CPU Package` 的有效传感器。
- 如果没有精确名称，从 CPU 温度传感器中按固定、可测试的优先顺序选择封装/平均温度；不能每次随机换传感器。
- 对当前选择的多颗 CPU 封装取算术平均，得到 `cpu.temperature.average`。
- 有效值必须大于 0 且小于 110。
- 来源字符串固定为 `LibreHardwareMonitor CPU Package`。

失败编码：没有可用传感器用 `sensor_unavailable`；读取异常用 `sensor_read_failed`。此时 `valid=false`、`value=null`、采样时间 null。

不要用 CPU 利用率代替温度。CPU 利用率属于未来的新指标。

## 7. 内存使用率

调用 Kernel32 `GlobalMemoryStatusEx`：

- 正常时可以使用 `dwMemoryLoad`，或按 `(total - available) / total * 100` 计算；项目实现需固定一种方法并测试边界。
- 本规格优先使用双精度公式，以保留一位小数显示：`100 * (ullTotalPhys - ullAvailPhys) / ullTotalPhys`。
- 结果限制到 0..100。
- 来源字符串固定为 `GlobalMemoryStatusEx`。
- API 失败、总内存为 0 或结果非有限数时使用 `memory_read_failed`。

## 8. 资源与异常隔离

- LibreHardwareMonitor 初始化失败不能影响内存上报、托盘或 WebSocket。
- 内存读取失败不能影响 CPU 温度上报。
- DDC 失败不能停止遥测。
- 遥测失败不能绕过 KVM 的设备 ID、显示器选择或去重校验。
- 传感器资源在退出时释放；系统恢复后若句柄失效，应重新发现硬件。

## 9. 官方实现参考

- Microsoft SetVCPFeature：https://learn.microsoft.com/windows/win32/api/lowlevelmonitorconfigurationapi/nf-lowlevelmonitorconfigurationapi-setvcpfeature
- Microsoft Physical Monitor Enumeration：https://learn.microsoft.com/windows/win32/monitor/using-the-low-level-monitor-configuration-functions
- Microsoft GlobalMemoryStatusEx：https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex
- .NET ClientWebSocket：https://learn.microsoft.com/dotnet/api/system.net.websockets.clientwebsocket
- .NET WebSocket 保活策略：https://learn.microsoft.com/dotnet/fundamentals/networking/websockets
- Windows Forms NotifyIcon：https://learn.microsoft.com/dotnet/api/system.windows.forms.notifyicon
- LibreHardwareMonitor：https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
