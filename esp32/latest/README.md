# ESP32 v0.5.0-dev.22 开发版

本目录是当前 ESP32 固件的唯一开发入口，目标芯片为 ESP32-S3-N16R8，显示屏为 ILI9341 320×240 横屏非触摸屏。

当前版本为 `v0.5.0-dev.22`。源码已经完成 ESP-IDF 编译，当前等待刷写和 ESP32、macOS、Windows 三端实机联调，因此仍属于开发版，不是正式发布版。

本版在保留 `legacy/v0.4.0-dev.3` 已验证的硬件与协议能力基础上，使用 LVGL 重构了 320×240 横屏 UI，并加入 SYSTEM、WEATHER、MARKETS 三页仪表盘、dashboard 数据协议、主备数据源切换和显示输出休眠/唤醒协同。

公开源码应包含 C 源码、头文件、资源、测试、文档、`sdkconfig.defaults` 和 `dependencies.lock`；`build/`、`managed_components/` 和本机生成的 `sdkconfig` 不属于源码发布内容。

当前开发总入口：`docs/implementation_contract.md`。修改固件前必须先阅读该文件，再按其引用阅读其余规范和 protocol v1。

正式发布前仍需完成：

1. 刷写 `v0.5.0-dev.22` 并完成屏幕、按键、继电器和网络实机验证。
2. 完成 ESP32、macOS、Windows 三端 dashboard、KVM 和显示输出休眠/唤醒联调。
3. 完成协议错误、数据过期、主备切换和断线恢复测试。
4. 完成长期运行、继电器安全默认状态和整机温升验证。

文档入口：

- `VERSION.md`
- `docs/implementation_contract.md`（实现合同与禁止猜测清单）
- `docs/architecture.md`
- `docs/hardware_interfaces.md`
- `docs/ui_spec.md`
- `docs/development_plan.md`

历史代码不得整体复制回来。每个底层模块应从冻结版按接口移植，并在新版架构中重新验证。
