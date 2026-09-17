# 项目总逻辑与 Windows 端需求

文档基准日期：2026-09-13  
对应 ESP32 固件：`0.5.0-dev.2`

## 1. 系统目标

一台 X41Q 显示器、一套 USB 键鼠和两台主机组成桌面 KVM：

- Mac 通过显示器 HDMI 2 输入，DDC 输入值为 `6`。
- Windows 通过显示器 DP 输入，DDC 输入值为 `7`。
- USB 共享器的实体按钮由 ESP32 GPIO 7 继电器模拟按下，脉冲时间为 250 ms。
- 散热器按钮由 ESP32 GPIO 8 第二块继电器模拟；它与本次 Windows v1 开发无关。
- ESP32 是唯一协调者，负责按键、状态机、显示器切换命令、USB 继电器时序和屏幕状态显示。
- Mac 与 Windows 客户端分别负责采集本机数据，并在自己当前掌握显示器 DDC 通路时执行显示输入切换。

## 2. 为什么命令发给“当前正在显示的主机”

显示器当前显示 Mac 时，可靠的 DDC 通路位于 Mac 一侧；因此 Mac 客户端收到 `target: "windows"`，先把显示器切到 DP 7。ESP32 收到成功结果后，才脉冲 USB 共享器。

显示器当前显示 Windows 时，可靠的 DDC 通路位于 Windows 一侧；因此 Windows 客户端收到 `target: "mac"`，先把显示器切到 HDMI 2（值 6）。ESP32 收到成功结果后，才脉冲 USB 共享器。

流程固定如下：

1. 用户按 ESP32 左键单击。
2. ESP32 判断当前主机、目标主机、连接状态、遥测新鲜度、冷却时间和当前是否已有切换任务。
3. ESP32 向当前主机发送 `switch_display` 命令。
4. 当前主机通过 DDC 写入目标输入值。
5. 客户端向 ESP32 返回与请求 ID 对应的 `command_result`。
6. 只有 `success == true` 且 `write_succeeded == true` 时，ESP32 才允许 GPIO 7 发出 250 ms USB 按钮脉冲。
7. 完成后进入 5 秒冷却期；冷却期内不排队、不重复执行切换请求。

严禁改变第 4、5、6 步的顺序。DDC 失败、命令超时、连接断开或结果不匹配时，USB 共享器必须保持原状态。

## 3. Windows 客户端的职责

Windows v1 必须实现：

1. 作为普通用户会话中的托盘程序运行。
2. 保持稳定的 Windows 设备 ID，并通过 WebSocket 连接 ESP32。
3. 连接后先发送 `hello`，再发送任何遥测或命令结果。
4. 每 1 秒采集并上报 CPU 平均温度与内存使用率。
5. 把 WebSocket 传输保活间隔设为 15 秒，掉线后按规定退避重连。
6. 接收只发给本设备的 `switch_display` 命令。
7. 使用 DDC/CI 的 VCP `0x60` 切换 X41Q 输入。
8. 对每个合法命令返回一次结构完整的 `command_result`。
9. 通过托盘菜单提供连接、KVM、开机启动、显示器选择、手动 DDC 测试、日志与退出功能。
10. 保存配置、状态和必要日志；崩溃后不得丢失设备 ID 或把 KVM 自动启用到错误显示器。

## 4. Windows 客户端明确不负责的内容

- 不直接操作 ESP32 GPIO、USB 共享器或散热器继电器。
- 不自行判断何时需要联动切换 USB；这是 ESP32 的职责。
- 不在收到命令时模拟键盘、鼠标或调用 USB 软件。
- 不把 CPU 使用率冒充 CPU 温度。
- 不因为找不到配置显示器而选择“第一台显示器”。
- 不在程序启动、连接成功、断线重连或系统唤醒时自动切换显示输入。
- 不创建 Web 服务、远程控制端口或 Windows 服务。
- v1 不制作主窗口、设置窗口、仪表盘或悬浮窗。

## 5. 当前 ESP32 面板与硬件逻辑

当前正式开发固件的按键扫描周期为 10 ms，消抖 30 ms，双击窗口 300 ms，长按阈值 800 ms。三个按键的实际映射是：

| 按键手势 | 当前行为 |
|---|---|
| 左键单击 | 发起一次 KVM 切换 |
| 左键双击 | 只校准 ESP32 内部记录的显示器目标与 USB 归属；不操作硬件 |
| 左键长按 | 切换 LCD 背光 |
| 中键单击 | 切换 Live Data 数据页；v1 目前只有 CPU 温度和内存这一页 |
| 中键双击/长按 | 未分配 |
| 右键单击 | 只切换散热器 UI 模式；当前不驱动第二块继电器 |
| 右键双击/长按 | 未分配 |

此前用于验证散热器的测试固件曾让右键直接跟随按下/松开控制 GPIO 8，但那不是当前 `0.5.0-dev.2` 正式开发固件的功能，Windows Agent 不得据此实现散热器逻辑。

与系统有关的固定 GPIO：

| 用途 | ESP32-S3 GPIO |
|---|---:|
| 左 / 中 / 右按键 | 4 / 5 / 6 |
| USB 共享器继电器 | 7 |
| 散热器继电器 | 8 |
| LCD DC / CS / MOSI / SCLK / MISO / RST / BLK | 9 / 10 / 11 / 12 / 13 / 14 / 15 |

GPIO 7 和 8 启动时都被安全设置为低电平。LCD 背光 GPIO 15 使用开漏方式，不是普通推挽高电平。Windows 程序不操作这些引脚，此表用于防止移植 Agent 误判系统职责。

ESP32 启动时把内部显示器目标和 USB 归属初始化为 Mac，但置信度为 unknown；左键双击可在实物状态与内部记录不一致时做人工校准。正常成功切换后，ESP32 才同时更新两者为目标主机。

## 6. 当前 ESP32 侧的事实

- 固件版本：`0.5.0-dev.2`。
- WebSocket 最多维护 Mac、Windows 两个逻辑主机槽位。
- 同一平台只保留一个连接；新的同平台 `hello` 会替换旧连接。
- 未发送 `hello` 的连接不能发送遥测或命令结果。
- 主机在线但超过 5 秒未收到有效遥测，会被视为 stale。
- 命令结果等待上限为 5 秒。
- 请求 ID 格式当前为 `esp32-%08x-%08x`，客户端只应把它当不透明字符串。
- 消息最大 2048 字节，严格 UTF-8、JSON 对象、协议版本 1。
- 当前临时宏 `APP_MAC_TO_WINDOWS_TEST_MODE` 为 `1`，所以只允许 Mac → Windows 自动切换。

## 7. ESP32 接入前置修改

Windows 程序开发完成后、开始双向 KVM 验收前，由 ESP32 项目完成以下改动：

1. 在遥测解析中按 `platform` 校验 `source`：
   - macOS CPU：`Average CPU`
   - macOS 内存：`Stats RAM_Usage`
   - Windows CPU：`LibreHardwareMonitor CPU Package`
   - Windows 内存：`GlobalMemoryStatusEx`
2. 建议把 Windows `hello.app` 固定校验为 `KVMBridgeWindows`；当前固件只要求它非空且不超过 32 字符。
3. 先保持 `APP_MAC_TO_WINDOWS_TEST_MODE = 1`，验证 Windows 能稳定上线、发遥测、手动 DDC 切换和正确回包。
4. 完成上述验证后，再设置 `APP_MAC_TO_WINDOWS_TEST_MODE = 0` 并重新编译刷写。
5. 关闭测试模式后，按 [TEST_PLAN.md](TEST_PLAN.md) 验证 Windows → Mac，再验证双向循环。

这五项没有完成前，不应宣称“双机 KVM 已完成”。

## 8. 失败与安全规则

- KVM 默认关闭。首次运行必须先选择唯一目标显示器并通过手动 DDC 测试，用户再从托盘菜单启用 KVM。
- `connection.enabled = false` 时不连接 ESP32，也不能接收远程命令。
- `kvm.enabled = false` 时仍可连接和上报数据，但收到合法切换命令必须返回 `kvm_disabled`，不能静默丢弃。
- 配置显示器不存在时返回 `display_not_found`；未选择时返回 `display_not_selected`。
- DDC 写入失败时返回 `success: false`、`write_succeeded: false`，ESP32 不得切 USB。
- X41Q 的输入读回不可靠。写入调用成功即可返回 `success: true`、`write_succeeded: true`、`confirmed: false`、`error: "input_written_but_unconfirmed"`。这不是切换失败。
- 客户端只接受 `target_device_id` 与自身设备 ID 完全一致的命令。
- 重复 `request_id` 不得再次执行 DDC。应缓存最近 64 个请求的结果，并可原样重发缓存结果。
- DDC 操作必须串行；同时收到的命令不得并行写显示器。
- 退出或关闭连接时必须取消计时器、网络循环与待处理任务，不得遗留后台线程。

## 9. 未来功能边界

下列内容可以以后加入，但不能妨碍 v1：

- 完整 Windows UI、历史曲线、更多传感器。
- 托盘图标主题与更丰富通知。
- 散热器控制策略。
- 协议认证或加密。
- 多显示器、多 KVM 配置。

任何新增协议字段都应采用新文档版本或向后兼容的可选字段；不得悄悄改变 v1 必填字段的含义。
