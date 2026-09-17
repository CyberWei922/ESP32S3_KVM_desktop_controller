# KVMBridgeWindows

这是 Windows 端常驻程序的独立交接目录。Windows 客户端源码目前维护在独立 Windows 主机；本目录负责保存让 Windows 开发 Agent 能够实现客户端的需求、协议、实现边界、配置和验收合同。

本目录当前只定义需求、协议、实现边界、配置和验收标准，不包含尚未验证的 Windows 可执行程序。

## 阅读顺序

1. [PROJECT_SPEC.md](PROJECT_SPEC.md)：整个系统的工作原理、Windows 端职责和不可改动的规则。
2. [PROTOCOL_V1.md](PROTOCOL_V1.md)：WebSocket 消息的逐字段定义、范例和校验要求。
3. [WINDOWS_IMPLEMENTATION.md](WINDOWS_IMPLEMENTATION.md)：托盘程序、进程结构、配置、日志、并发和发布方式。
4. [DDC_AND_METRICS.md](DDC_AND_METRICS.md)：显示器 DDC、CPU 温度和内存占用的 Windows 实现。
5. [TEST_PLAN.md](TEST_PLAN.md)：从单元测试到双机真机切换的验收清单。
6. [AGENT_HANDOFF_PROMPT.md](AGENT_HANDOFF_PROMPT.md)：转移到 Windows 后，可直接交给开发 Agent 的任务提示词。
7. [config.example.json](config.example.json)：正式配置文件的初始模板。
8. [DASHBOARD_PAGES_UPGRADE.md](DASHBOARD_PAGES_UPGRADE.md)：天气、加密货币、汇率、城市设置及 Windows 主 / Mac 备数据协议。本次 Windows 升级以此文件为准。

## 已冻结的核心决定

- 技术栈：C#、.NET 8、Windows Forms 基础设施。
- 形态：只有系统托盘图标和右键菜单；没有主窗口、设置窗口或任务栏窗口。
- 程序名与协议 `app` 字段：`KVMBridgeWindows`。
- 传输：Windows 主动连接 ESP32 的 WebSocket 服务。
- 默认地址：`ws://192.168.1.50:81/statsforkvm`。
- 数据：每秒上报一次 CPU 平均温度与内存占用率。
- 显示器切换：优先直接调用 Windows Monitor Configuration API（Dxva2），VCP 代码 `0x60`。
- X41Q 输入值：Mac 为 HDMI 2，即 `6`；Windows 为 DP，即 `7`。
- USB 共享器：只能由 ESP32 的 GPIO 7 继电器控制，Windows 程序不得直接控制。
- 散热器：由 ESP32 的 GPIO 8 第二块继电器控制，不属于 Windows v1 的职责。
- 当前协同目标为 ESP32 `0.5.0-dev.22`，已开放 Mac ↔ Windows 双向 KVM，并加入天气、行情、状态三页仪表盘。

## 当前兼容状态

ESP32 `0.5.0-dev.22` 已经按平台分别校验遥测来源，并新增独立的 dashboard 序号校验。Windows 正式客户端必须如实发送：

- CPU：`LibreHardwareMonitor CPU Package`
- 内存：`GlobalMemoryStatusEx`

不得让 Windows 客户端伪装成 macOS，也不得发送虚假的 Mac 来源名称。完整双向切换要求 Mac 与 Windows 都已连接，并且最近 5 秒内有有效遥测。

## 权威性

本目录记录的是 Windows v1 面向 ESP32 `0.5.0-dev.22` 的实现合同。若旧文件与本目录冲突：

- Windows 行为以本目录为准；
- 线上报文最终仍必须同时满足 ESP32 实际解析器；
- 遇到冲突时先修正并同步文档与 ESP32，不允许 Windows 客户端自行发明兼容字段。
