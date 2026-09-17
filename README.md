# ESP32-S3 KVM Desktop Controller

一个面向双主机桌面的硬件控制系统，用 ESP32-S3 把 Mac、Windows、外接显示器、USB 共享器、状态屏和外置散热器连接起来。

它的目标是：只用一套显示器、键盘和鼠标，在 Mac 与 Windows 之间快速切换，同时在状态屏上看到两台电脑和桌面设备的运行状态。

## 它能做什么

- 一键在 Mac 和 Windows 之间切换显示器输入和 USB 共享器归属。
- 在 ESP32-S3 状态屏上显示网络状态、主机连接状态、KVM 状态、错误提示和设备信息。
- 提供 `SYSTEM`、`WEATHER`、`MARKETS` 三个状态页面。
- 显示天气、未来天气、BTC/USDT、DOGE/USDT 和 USD/CNY 等信息。
- 支持 Mac 与 Windows 两个数据源，并在主数据源不可用时使用备用数据源。
- 通过 DDC/CI 切换外接显示器输入，并在确认成功后控制 USB 共享器继电器。
- 支持显示输出休眠与唤醒：电脑继续运行，只停止或恢复视频输出。
- 控制外置散热器的 Manual、Standard、Silent、Turbo 四种模式。
- 通过实体按键操作，不依赖触摸屏或额外控制面板。

## 当前状态

当前公开开发版本为 `v0.5.0-dev.22`。

- ESP32-S3 固件已经完成 ESP-IDF 编译。
- macOS 客户端当前版本为 `v0.1.0-dev`，已经支持基础遥测、dashboard、KVM 和显示输出休眠/唤醒协议。
- Windows 客户端正在独立 Windows 主机上开发，当前仓库先提供协议、功能规范和验收文档。
- 当前版本仍需要刷写硬件并完成 Mac、Windows、ESP32 三端实机联调，因此不应视为正式发行版。

## 使用对象

本项目适合希望搭建类似桌面 KVM 控制器的开发者和硬件爱好者。它涉及 ESP32-S3、ILI9341 LCD、继电器、DDC/CI、USB 共享器以及 macOS/Windows 客户端，不是即插即用的消费级产品。

使用前需要准备：

- ESP32-S3-N16R8 开发板。
- ILI9341 320×240 横屏显示模块。
- 支持 DDC/CI 的外接显示器。
- USB 共享器和继电器控制电路。
- Mac 与 Windows 主机。
- 按项目硬件文档制作并确认安全的外壳和接线。

## 项目内容

```text
esp32/          ESP32-S3 固件、当前版本、历史版本和验证工程
StatsForKVM/    macOS 菜单栏客户端、KVM 功能和 protocol v1
Windows/        Windows 客户端的协议、功能规范和测试文档
cad_enclosure/  当前外壳 STL、生成脚本和设计资料
m1ddc/          DDC 控制的第三方参考源码
```

## 重要说明

- 当前版本仍处于开发和实机验证阶段，不能保证所有硬件组合都能直接使用。
- Wi-Fi 配置、局域网地址、显示器型号和 DDC 能力会影响实际效果。
- ESP32 不直接访问互联网，天气和行情数据由客户端采集后发送到状态屏。
- 不要把本地 Wi-Fi 配置、构建产物或调试文件提交到公开仓库。
- 外壳、继电器和 USB 接线必须在断电状态下检查，避免损坏主机或设备。

## 第三方项目

macOS 客户端基于 [Stats](https://github.com/exelban/stats) 的 MIT 许可代码，并集成了 [m1ddc](https://github.com/waydabber/m1ddc) 的 MIT 许可代码。对应的版权和许可证文件会保留在仓库中。

## macOS 客户端

`StatsForKVM/` 中包含完整的 Xcode 工程。当前开发版的 macOS Release App 已打包在 [`releases/macos/`](releases/macos/) 中，适合用于体验当前功能；正式使用前请注意当前版本仍处于开发阶段。

## 文档入口

- [当前项目需求](project_requirements.md)
- [开发路线](development_roadmap.md)
- [ESP32 当前版本说明](esp32/latest/VERSION.md)
- [通信协议](StatsForKVM/PROTOCOL.md)
- [Windows 功能说明](Windows/README.md)
- [外壳资料](cad_enclosure/README.md)
- [第三方组件声明](THIRD_PARTY_NOTICES.md)
