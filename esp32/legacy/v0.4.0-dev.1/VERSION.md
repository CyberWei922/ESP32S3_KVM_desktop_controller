# 当前版本

- 版本：`v0.4.0-dev`
- 日期：2026-09-08
- 状态：Wi-Fi / StatsForKVM protocol v1 开发版；代码与本机构建验证完成，待真机联网和显示验证
- ESP-IDF：v6.1
- 目标芯片：ESP32-S3-N16R8

## 版本保护

- 上一实机冻结版 `v0.3.0` 已完整保存在 `esp32/legacy/v0.3.0/`。
- 历史副本包含原源码、配置、文档和原有 `build/`，未在本轮修改。
- 本版在真机联网、WebSocket 和 LCD 画面验收前不得改名为 `v0.4.0`。

## 本版代码完成

- Wi-Fi Station、DHCP、断线指数退避重连和联网状态模型。
- 端口 `81`、路径 `/statsforkvm` 的 ESP-IDF WebSocket 服务端。
- 固定 macOS / Windows 两个连接槽；同平台新连接安全替换旧连接。
- protocol v1 的 UTF-8、2048-byte 限长、`hello`、`telemetry`、`command_result` 严格解析。
- 每连接递增序号校验，以及 CPU 温度、内存百分比和远端采样时间校验。
- 线程安全状态快照和整条 telemetry 原子提交。
- 以 ESP32 单调时钟计算 5 秒数据过期；断开立即离线，下一份合法 telemetry 自动恢复。
- LCD 继续以 2 FPS 局部刷新，显示真实 CPU 摄氏温度、内存百分比、`OFFLINE`、`STALE` 和 `N/A`。
- 有限 pending 表的手动显示切换底层 API；仅可向已完成 hello 的 macOS 连接发送，启动时不会自动调用。
- 本地 Wi-Fi 配置示例；未创建私密配置时仍能编译并运行 LCD，画面和日志显示 `wifi_not_configured`。

## 自动验证结果

- `idf.py build`：通过。
- 固定协议向量：通过，覆盖合法 hello/telemetry、CPU null、`valid:false`、内存越界、错误版本、超长消息、hello 前 telemetry、重复 sequence，以及确认成功、写入成功但未确认、不完整 command_result。
- 当前含本机 Wi-Fi 配置的应用二进制：`0xdb1a0` bytes（897,440 bytes；ESP-IDF 检查应用分区剩余 `0x24e60` bytes，约 14%）。
- `idf.py size`：镜像内容 897,316 bytes；DIRAM 123,530 / 341,760 bytes（36.15%，剩余 218,230 bytes）；独立 IRAM 区 16,384 / 16,384 bytes；RTC SLOW 剩余 8,156 bytes，RTC FAST 剩余 8,168 bytes。
- 可提交源码和文档不包含真实凭据；本机 `kvm_config.local.h` 已被忽略。当前构建产物会按正常固件行为包含联网凭据，不得分发或归档该私密构建产物。

## 待实机验证

- Wi-Fi 连接、认证失败提示、DHCP 地址和断线重连。
- macOS 本地网络权限，以及 StatsForKVM 到 ESP32 的真实长连接。
- LCD 上真实温度、内存、断线、过期和恢复画面。
- 同平台连接替换、Mac/Windows 双客户端、VPN/虚拟网卡环境。
- 手动触发命令和真实显示器 DDC 回执；本轮不会自动切换显示器。
- 睡眠/唤醒、连续 4 小时及更长稳定性。

## 相关说明

- [protocol v1 实现说明](docs/protocol_v1.md)
- [Wi-Fi 本地配置](docs/wifi_configuration.md)
- [LCD 接线](docs/lcd_wiring.md)
