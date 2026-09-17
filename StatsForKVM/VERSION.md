# StatsForKVM 版本说明

## 当前版本

- 版本：`v0.1.0-dev`
- Build：`1`
- 日期：`2026-09-17`
- 协同 ESP32：`v0.5.0-dev.22`
- 上游基线：Stats `v3.0.15`
- 上游提交：`60e65d1454c647d9592550447757e509b01d5ebb`
- 应用名称：`StatsForKVM`
- 主 Bundle ID：`com.wei.statsforkvm`
- 签名团队：`2P54P39YQ9`
- 目标机器：MacBook Air 13 英寸（Mac17,3）、Apple M5、16 GB 内存
- 实测系统：macOS 27.0 Developer Beta，Build `26A5388g`

这是开发版本，不表示 ESP32 通信、Windows 客户端、VPN 环境、dashboard 主备切换或显示器休眠/唤醒已经完成三端实机验收。

## 本版已经实现

- 保留原版 Stats 的菜单栏监控和设置结构。
- 新增“监视器（Monitor）”设置页。
- 直接复用 Stats 已有 Sensors 和 RAM Reader，不启动第二套硬件采集器。
- 基础 telemetry 继续发送 `Average CPU` 温度与 RAM 占用百分比。
- 新增 dashboard 数据服务，支持天气、BTC/USDT、DOGE/USDT、USD/CNY，并按 protocol v1 发送独立序号的 dashboard 快照。
- 使用稳定、带版本号的 JSON；无效温度为 `null` 并附错误原因，绝不伪装成 `0°C`。
- 当前线缆格式已单独记录在 [`PROTOCOL.md`](PROTOCOL.md)，可直接作为 ESP32 服务端实现依据。
- 使用 Apple Network.framework 建立 WebSocket 客户端，每秒发送最新快照。
- 支持心跳、指数退避重连、网络路径变化、唤醒后重连和最大消息限制。
- 关闭系统 HTTP 代理参与局域网连接，并提供“强制 Wi-Fi”选项及 VPN/虚拟网卡路由诊断。
- 使用 Bonjour 作为辅助发现；保存的 ESP32 固定 IP 仍是主连接地址。
- 新增“KVM”设置页。
- 内置 m1ddc 的显示器发现和 DDC 输入读写代码，并保留上游 MIT 许可证及固定提交记录。
- DDC 操作在后台串行执行；切换结果区分写入成功、读回确认和失败原因。
- WebSocket 控制命令带请求 ID 去重，并返回结构化 `command_result`。
- `hello` 已声明 `switch_display`、`display_sleep`、`display_wake` 和 `dashboard_data` 能力；已实现相应命令处理和 dashboard 发送。
- 已实现 `display_sleep` 和 `display_wake` 的 macOS 调用；该能力仍需与 ESP32、Windows 完成三端实机验收。
- 原版 Stats 自动更新入口已停用，防止定制版被上游发行版覆盖。

## 数据来源与权限

### Average CPU 温度

Stats 先从 Apple SMC 取得当前机器可用的键，再按 `SystemKit` 识别出的 M5 平台筛选传感器。`SensorsReader` 对所有 `group == CPU`、`type == temperature` 且 `average == true` 的可用读数求算术平均，形成计算项 `Average CPU`。Monitor 直接从同一回调分流这个计算结果。

当前 Stats M5 表包含 CPU super/performance thermal-zone 键；它们是芯片热区传感器名称，不应理解为每个逻辑 CPU 核心的精确结温。本机运行中 `Average CPU` 可持续取得有效读数，未弹出管理员授权，也未安装额外采集程序。

如果后续系统更新造成读数缺失、超范围或超过 5 秒未更新，JSON 会输出：

```json
{"value":null,"valid":false,"source":"Average CPU","error":"sensor_unavailable"}
```

过期数据使用 `data_stale`，不会保留旧温度冒充实时值。

### 内存占用百分比

Monitor 复用 Stats 的 `RAM_Usage.usage`，即 Stats 当前界面使用的 `(total - free) / total` 比例，再转换为 `0...100` 百分比。来源字段固定为 `Stats RAM_Usage`，因此 ESP32 与 Stats 菜单栏口径一致。

## m1ddc 来源与风险

- 上游：<https://github.com/waydabber/m1ddc>
- 固定提交：`04d949794102eb8df01ad3681afff6464a3eede2`
- 许可证：MIT
- 记录：[`ThirdParty/m1ddc/UPSTREAM.md`](ThirdParty/m1ddc/UPSTREAM.md)

m1ddc 使用 Apple 未公开的显示器接口。源码和桥接层可以在当前 macOS 27 SDK 下编译。2026-09-11 实机确认：更换 USB-C→HDMI 转接器后，X41Q 的 DDC 亮度写入和输入切换有效，输入值为 DP=`7`、HDMI1=`5`、HDMI2=`6`，双向切换链路可行。当前 DDC 读回仍异常，不能作为切换成功的唯一判据；重复性、重新插拔后的标识稳定性和连续切换可靠性仍需后续测试。

## 构建与运行

Debug 构建：

```bash
xcodebuild \
  -project Stats.xcodeproj \
  -scheme Stats \
  -configuration Debug \
  -derivedDataPath /tmp/statsforkvm-build \
  build
```

终端直接运行已构建应用：

```bash
<derived-data>/Build/Products/Debug/StatsForKVM.app/Contents/MacOS/StatsForKVM
```

当前已经构建并通过签名校验的 Release 应用：

```text
build/Release/StatsForKVM.app
```

关闭启动它的终端可能会影响前台调试进程。正常长期使用应将构建好的 `StatsForKVM.app` 放入“应用程序”，从 Finder 启动，并在 StatsForKVM 设置中启用“登录时启动”；这个登录项仍需人工确认一次。

## 版本冻结条件

完成以下人工项目后，才可把 `v0.1.0-dev` 冻结为正式 `v0.1.0`：

- 在 Xcode 中检查 Monitor/KVM 两个页面的完整显示和交互。
- 与真实 ESP32 同时连接并验证双客户端、断线与恢复。
- 在常用 VPN、系统代理和虚拟网卡组合下验证局域网连接。
- 对目标显示器完成每方向至少 20 次重复切换，并确定不依赖异常 DDC 读回的确认策略；显示器发现、输入值和两个方向的实际切换已确认。
- 验证登录自启、睡眠唤醒和至少 4 小时连续运行。
