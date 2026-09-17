# Windows v1 实现规格

## 1. 技术与项目结构

使用：

- C# / .NET 8
- Windows Forms，仅使用 `ApplicationContext`、`NotifyIcon`、`ContextMenuStrip`
- `ClientWebSocket` 作为 WebSocket 客户端
- xUnit 作为自动测试框架
- `LibreHardwareMonitorLib` 读取 CPU 温度
- Windows `Dxva2.dll` 原生 API 执行 DDC

建议解决方案结构：

```text
Windows/
  KVMBridgeWindows.sln
  src/KVMBridgeWindows/
    Program.cs
    TrayApplicationContext.cs
    Configuration/
    Protocol/
    Networking/
    Metrics/
    Ddc/
    Startup/
    Logging/
  tests/KVMBridgeWindows.Tests/
  docs/（可选；本目录现有文档不得删除）
```

项目输出类型是 Windows GUI 应用，因此运行时不出现控制台窗口。不得创建或隐藏一个空白主窗体来维持生命周期，应由自定义 `ApplicationContext` 管理。

## 2. 单实例与生命周期

- 使用命名互斥体 `Local\KVMBridgeWindows.Singleton` 保证同一用户会话只有一个实例。
- 第二次启动不得再创建连接或托盘图标；可以提示“程序已在运行”后退出。
- 启动顺序：载入或创建状态 → 载入并校验配置 → 创建服务 → 创建托盘图标 → 按配置启动采集和连接。
- 退出顺序：禁止新命令 → 取消重连和采集计时器 → 关闭 WebSocket → 等待有界后台任务 → 销毁托盘图标 → 释放 DDC 和传感器资源。
- 不要求管理员权限。CPU 温度在无管理员权限时不可读，必须降级成无效指标，不能让整个程序退出。

## 3. 文件位置与写入规则

根目录：`%LOCALAPPDATA%\KVMBridgeWindows\`

```text
config.json     用户配置
state.json      稳定 device_id 与内部状态
logs/           滚动日志
```

- 首次运行若无配置，复制内置默认值创建 `config.json`。
- 首次运行若无状态，生成 `win-<guid>` 写入 `state.json`。
- 配置和状态使用“写临时文件 → 原子替换”方式，避免断电产生半个 JSON。
- JSON 损坏时保留原文件副本，在日志中记录，并回到安全默认值：KVM 关闭、不开机启动、不能自动选显示器。
- 当前协议没有密码或令牌，但日志仍不得记录完整系统环境变量、用户文件内容或无关隐私数据。

## 4. 配置字段

正式模板见 [config.example.json](config.example.json)。语义如下：

- `schema_version`：当前为 1；未知更高版本应拒绝加载并保持安全关闭。
- `connection.enabled`：是否连接 ESP32。关闭时不接收命令、不上传数据。
- `connection.host/port/path`：组合成 `ws://host:port/path`。
- `connection.telemetry_interval_ms`：正式值 1000，不允许低于 500。
- `connection.keepalive_interval_ms`：正式值 15000，不允许低于 5000；映射到 .NET 8 的 `ClientWebSocketOptions.KeepAliveInterval`。
- `telemetry.enabled`：只控制遥测发送；关闭后仍可在 KVM 已启用时维持连接接命令。
- `kvm.enabled`：控制是否执行远程 DDC 命令，默认 false。
- `kvm.display_id`：用户明确选择的稳定物理显示器标识，默认空。
- `kvm.vcp_code`：固定 96，即十六进制 `0x60`。
- `kvm.mac_input/windows_input`：默认 6 和 7。
- `kvm.verify_readback`：X41Q 正式值 false。
- `kvm.ddc_backend`：正式值 `native`；可选诊断回退值 `control_my_monitor`。
- `kvm.control_my_monitor_*`：只在用户主动配置回退方案时使用。
- `startup.start_with_windows`：是否写入当前用户开机启动项。
- `logging.level`：`debug/info/warning/error` 之一。
- `logging.retention_days`：日志保留天数。

配置热更新只要求支持托盘菜单修改的布尔项和显示器选择。手工编辑其他字段后，用户可以点击“立即重连”或重启程序使其生效。

## 5. 托盘图标与右键菜单

没有主窗口。右键菜单从上到下固定为：

1. `ESP32：已连接 / 正在连接 / 等待重连 / 已禁用 / 故障`（只读）。
2. `Windows 数据：正常 / CPU 温度不可用 / 内存读取失败`（只读）。
3. 分隔线。
4. `启用 ESP32 连接`（可勾选）。
5. `发送系统数据`（可勾选）。
6. `启用 KVM / DDC`（可勾选；未选择显示器时禁止勾选并提示原因）。
7. `随 Windows 启动`（可勾选）。
8. 分隔线。
9. `目标显示器` 子菜单：列出当前发现的物理显示器，显示友好名与稳定 ID 的短后缀；已选项打勾；另有“重新扫描”。
10. `测试：切换显示器到 Mac（HDMI 2 / 6）`。
11. `测试：切换显示器到 Windows（DP / 7）`。
12. `立即重连 ESP32`。
13. 分隔线。
14. `打开配置文件`。
15. `打开日志文件夹`。
16. `版本 1.x.x`（只读）。
17. `退出`。

手动 DDC 测试规则：

- 仅操作显示输入，不触发 ESP32，也不切 USB 共享器。
- 显示器未选择、已消失或 DDC 不可用时不执行，并显示托盘通知。
- 手动测试结果写日志，并以一次简短托盘通知告诉用户成功或失败。

图标状态建议：灰色=禁用，黄色=连接中/重连，绿色=连接且数据正常，红色=故障。颜色只是本地提示，不改变协议行为。

## 6. 开机启动

- 使用当前用户注册表：`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`。
- 值名：`KVMBridgeWindows`。
- 值内容：正确引用完整 EXE 路径。
- 不建立计划任务，不安装 Windows 服务，不要求管理员权限。
- 程序每次启动读取实际注册表状态同步菜单，不能只相信配置文件。
- 用户关闭选项时只删除本程序自己的值。

## 7. 服务划分

- `ConfigurationService`：读取、校验、原子保存配置。
- `DeviceIdentityService`：创建和持久化设备 ID。
- `TelemetryService`：每秒产生一份最新快照。
- `WebSocketService`：单一连接、Hello、Ping、重连、收发循环。
- `ProtocolCodec`：严格编码本机消息、验证服务端命令。
- `CommandDispatcher`：串行、去重、限时执行命令。
- `MonitorDiscoveryService`：枚举逻辑和物理显示器并生成稳定 ID。
- `DdcService`：在选定显示器上读取/写入 VCP。
- `StartupService`：管理 HKCU 启动项。
- `TrayApplicationContext`：只负责展示状态并调用服务，不承载业务规则。

禁止让托盘菜单事件直接调用 P/Invoke 或直接写 WebSocket。

## 8. 并发与队列

- 整个程序只有一个有效 WebSocket 生命周期控制器。
- `ClientWebSocket` 保持最多一个 Receive 和一个 Send；所有出站消息进入单写者通道。
- 命令结果优先级高于遥测。
- 遥测队列不超过“正在发送 1 条 + 最新待发 1 条”。
- DDC 命令使用单消费者队列，禁止并行调用 Dxva2。
- 最近 64 个 `request_id` 保存对应结果。重复 ID 不再写 DDC，可以重发完全相同的缓存结果。
- 缓存只需存在于当前进程，不要求跨重启持久化。
- 使用统一 `CancellationToken` 树处理退出、重连和系统休眠。

## 9. 日志

默认 `info`，按天滚动，默认保留 7 天。至少记录：

- 程序版本、启动与正常退出。
- 配置载入结果，但不重复输出完整配置。
- 连接状态变化、重连原因和退避秒数。
- Hello 成功发送。
- 收到命令的 request ID、target、去重情况。
- 选择的显示器稳定 ID、DDC 写入值、耗时和结果。
- 命令结果是否成功发送。
- 传感器状态从正常变异常或从异常恢复。
- 配置、注册表、显示器枚举和未处理异常。

不要在 `info` 每秒记录一条遥测；只有 `debug` 可以采样记录，仍需限流。

## 10. 发布

首个正式包目标 `win-x64`，发布为自包含单文件：

```powershell
dotnet restore
dotnet test -c Release
dotnet publish .\src\KVMBridgeWindows\KVMBridgeWindows.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

发布包至少包含 EXE、默认配置说明、版本号和许可证清单。是否需要 `win-arm64` 在 x64 真机验收后再决定。
