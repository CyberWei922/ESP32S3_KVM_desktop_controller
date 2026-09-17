# 交给 Windows 开发 Agent 的提示词

把整个 `Windows` 文件夹复制到 Windows 电脑后，将下面内容交给开发 Agent。不要只复制本页，必须让 Agent 能读取本目录其余文件。

---

你要在当前 `Windows` 目录中完整开发 `KVMBridgeWindows` v1。开始写代码前，必须按顺序完整阅读：

1. `README.md`
2. `PROJECT_SPEC.md`
3. `PROTOCOL_V1.md`
4. `WINDOWS_IMPLEMENTATION.md`
5. `DDC_AND_METRICS.md`
6. `TEST_PLAN.md`
7. `config.example.json`

这些文件是实现合同。不得自行改协议字段、输入值、设备身份规则、失败规则或 ESP32 的 DDC→结果→USB 安全顺序。

任务：

- 使用 C#、.NET 8 和 Windows Forms 基础设施创建可编译解决方案。
- 软件必须只有系统托盘图标与右键菜单，不创建主窗口、设置窗口、任务栏窗口或控制台窗口。
- 使用自定义 `ApplicationContext` 管理程序生命周期，使用命名互斥体保证单实例。
- 使用 `ClientWebSocket` 连接默认 `ws://192.168.1.50:81/statsforkvm`，实现 Hello、每秒遥测、15 秒传输保活、1/2/4/8/16/30 秒重连、串行发送、遥测最新值覆盖和命令结果优先。目标是 .NET 8，因此用 `KeepAliveInterval` 的 unsolicited PONG，不得误用 .NET 9 才有的 `KeepAliveTimeout`。
- 持久化 `win-<guid>` 设备 ID。程序重启、网络重连后 ID 不得改变。
- 用 `LibreHardwareMonitorLib` 获取 CPU Package 温度，用 `GlobalMemoryStatusEx` 获取物理内存使用率。CPU 不可用时只把该指标标为无效，不能影响内存、网络或托盘。
- 使用 Windows 原生 Dxva2 Monitor Configuration API 实现 DDC VCP `0x60`。必须安全释放所有物理显示器句柄。
- 目标输入固定为 Mac HDMI 2 = 6，Windows DP = 7。
- 必须让用户从托盘菜单明确选择稳定的目标显示器。找不到时不得回退到第一台显示器。
- KVM 默认关闭。收到合法但无法执行的命令时也必须返回精确失败结果。
- X41Q 默认不读回确认：SetVCPFeature 成功时返回 success/write_succeeded=true、confirmed=false、read_back_input=null、error=input_written_but_unconfirmed。
- 串行执行 DDC，缓存最近 64 个 request ID 的结果；重复命令不能再次切屏。
- 实现配置、状态原子保存、滚动日志、HKCU 当前用户开机启动和文档规定的完整托盘菜单。
- ControlMyMonitor 只能作为可选诊断后端，不能成为默认或强制依赖。
- 为协议、配置、队列、去重、错误映射、DDC 句柄释放和指标计算编写自动测试。
- 完成后运行 Release 测试与 `win-x64` 自包含单文件发布。

项目结构至少包含：

```text
KVMBridgeWindows.sln
src/KVMBridgeWindows/
tests/KVMBridgeWindows.Tests/
```

工作边界：

- 只修改当前 Windows 目录，不修改 Mac 或 ESP32 代码。
- 不直接控制 ESP32 继电器、USB 共享器或散热器。
- 不把 CPU 使用率当作 CPU 温度。
- 不在启动、连接、重连或唤醒时自动切换显示器。
- 不用隐藏空窗体冒充无 UI，不安装 Windows 服务，不默认要求管理员权限。
- 不为了“先跑起来”而伪造 macOS 的遥测 source。ESP32 `0.5.0-dev.22` 已支持规定的 Windows 遥测来源、dashboard 消息和显示输出休眠/唤醒能力协商。

执行方式：先检查 Windows 与显示器环境，再搭建项目，分层实现，运行自动测试，最后进行文档中 Windows 单机真机项目。不要过度重复测试。遇到无法在当前机器验证的硬件步骤，保留安全默认值并清楚列出待用户验证项，不能猜测成功。

最终报告必须包含：

- 创建和修改了哪些文件；
- 已实现功能；
- 自动测试与 Release 发布结果；
- 实际检测到的显示器和稳定 ID；
- CPU 温度来源是否可用；
- 哪些真机步骤需要用户操作；
- 所有未完成项和明确原因。

---
