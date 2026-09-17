# ESP32 端 StatsForKVM protocol v1 实现

## 权威协议

线缆字段和语义以项目根目录的 `StatsForKVM/PROTOCOL.md` 为唯一权威来源。本文件只说明 ESP32 端如何落实该协议，不另行定义简化格式。

服务地址：

```text
ws://<ESP32 的 DHCP 地址>:81/statsforkvm
```

只接受 UTF-8、未分片的 WebSocket 文本消息。ESP-IDF HTTP Server 保持默认控制帧处理，因此收到客户端 ping 时由框架回复 pong。单条应用消息在分配 JSON 缓冲区前检查长度，最大为 2048 bytes。

## 会话规则

- 固件只维护 `macos` 和 `windows` 两个固定槽位，不创建无界连接表。
- 每个连接必须先发送合法 `hello`；在此之前的 telemetry 或 command_result 不会进入状态模型。
- `protocol_version` 必须是整数 `1`，`device_id` 必须非空且不超过 128 bytes。
- 同一平台的新连接替换旧连接。旧 socket 随后关闭时会以文件描述符重新核对槽位，不会把新连接误标成离线。
- sequence 在单个连接内必须严格递增；新连接/新 hello 会重置序号基线。
- socket 断开时对应主机立即 `OFFLINE`。
- hello 后或上一份合法 telemetry 后超过 5 秒仍没有下一份合法 telemetry，主机进入 `STALE`；判断使用 `esp_timer_get_time()`，不依赖 NTP。

## 遥测校验

CPU 必须来自 `metrics["cpu.temperature.average"]`，`unit` 为 `celsius`，`source` 为 `Average CPU`。只有 `valid:true`、有限数值且 `0 < value < 110` 才进入有效温度。

内存必须来自 `metrics["memory.usage"]`，`unit` 为 `percent`，`source` 为 `Stats RAM_Usage`。只有 `valid:true`、有限数值且 `0 <= value <= 100` 才进入有效百分比。v1 不包含 used/total 字节数，ESP32 不会伪造这些值。

`valid:false` 必须使用 `value:null` 并携带非空错误。`valid:true` 但值为 null、缺少采样时间或数值越界时，会保留明确的本地错误并显示 `N/A`，不会以零代替。字段缺失、类型错误、错误版本或错误身份会拒绝整条消息，不会产生半更新状态。

远端 `timestamp_milliseconds` 与两个 `sampled_at_milliseconds` 只用于记录。解析通过后，两项指标、采样时间、sequence 和 ESP32 接收时间通过同一把状态锁一次提交；LCD 每帧只复制一次完整快照。

## 命令通道

`websocket_server_send_switch_display(HOST_MAC, target, ...)` 是为后续实体按钮保留的底层入口：

- 仅能发送到已完成 hello 的在线 macOS 连接。
- `target` 只允许 `mac` 或 `windows`。
- request ID 由 ESP32 自动生成，长度不超过 128 bytes。
- `target_device_id` 取自 hello 保存的真实 ID。
- 最多同时保存 4 个 pending request，每项 5 秒超时。
- command_result 会分别保存 `success`、`write_succeeded`、`confirmed` 和错误；断线、发送失败和超时也有独立状态。

当前没有任何代码自动调用该函数，启动和接收遥测均不会触发真实 DDC 操作。

## 自动测试

在 `esp32/latest/` 运行：

```bash
test/run_protocol_tests.sh
```

测试使用与固件相同的 `protocol.c` 和锁定版本 cJSON，覆盖任务要求中的全部固定边界。网络生命周期、真实 ping/pong、双客户端和 LCD 仍需真机验证。

## 暂缓项

当前 ESP-IDF 工程没有预置 mDNS 组件，因此本开发版不发布 `_statsforkvm._tcp`。首版使用 DHCP 地址（建议在路由器中建立地址保留）；mDNS 作为后续可选功能，不阻塞固定地址联调。
