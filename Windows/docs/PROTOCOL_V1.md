# ESP32 WebSocket 协议 v1

## 1. 连接参数

- 角色：ESP32 是服务端，Windows 是客户端。
- 默认 URL：`ws://192.168.1.50:81/statsforkvm`
- WebSocket 文本帧：只接受 UTF-8 JSON 对象。
- 单条消息上限：2048 字节。
- `protocol_version`：整数 `1`。
- 保活：连接成功后将 `ClientWebSocketOptions.KeepAliveInterval` 设为 15 秒。.NET 8 的公开实现会发送 unsolicited PONG 作为保活，不要声称它具备 .NET 9 才提供的 PING/PONG 超时检测；连接失效还要依靠收发错误和每秒遥测写入来发现。
- 重连退避：1、2、4、8、16、30 秒，之后一直以最多 30 秒重试。
- 重连成功后退避计数清零，并重新先发 `hello`。

未知 JSON 字段可以忽略，以便向后兼容；本文标为必填的字段不得缺少、改名或改变类型。

## 2. 标识与数值限制

- Windows `platform` 固定为 `"windows"`。
- Windows `app` 固定为 `"KVMBridgeWindows"`。
- `device_id`：1 到 128 个 UTF-8 字节，推荐 `win-` 加 GUID（GUID 使用小写、带连字符）。
- `request_id`：1 到 128 个 UTF-8 字节；视为不透明、区分大小写的字符串。
- `app`：1 到 32 个 UTF-8 字节。
- 时间戳、序号：非负整数，不超过 `9007199254740991`。
- CPU 温度有效范围：严格大于 0 且严格小于 110 摄氏度。
- 内存使用率有效范围：0 到 100，含边界。
- JSON 中禁止 `NaN`、`Infinity`、小数序号和小数时间戳。

## 3. Hello

每次建立新 WebSocket 连接后第一条业务消息必须是：

```json
{
  "type": "hello",
  "protocol_version": 1,
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "platform": "windows",
  "app": "KVMBridgeWindows"
}
```

规则：

- 同一条连接只能使用同一 `platform` 和 `device_id`。
- 后续遥测中的身份必须与 Hello 完全一致。
- 程序重启不能自动生成新的设备 ID。
- 新 Windows 连接会替换 ESP32 当前保存的旧 Windows 连接。

## 4. Telemetry

连接完成且 Hello 已成功发送后，每 1000 ms 生成一次最新快照：

```json
{
  "type": "telemetry",
  "protocol_version": 1,
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "platform": "windows",
  "sequence": 42,
  "timestamp_milliseconds": 1789234567890,
  "metrics": {
    "cpu.temperature.average": {
      "valid": true,
      "value": 57.4,
      "unit": "celsius",
      "source": "LibreHardwareMonitor CPU Package",
      "error": null,
      "sampled_at_milliseconds": 1789234567862
    },
    "memory.usage": {
      "valid": true,
      "value": 63.2,
      "unit": "percent",
      "source": "GlobalMemoryStatusEx",
      "error": null,
      "sampled_at_milliseconds": 1789234567865
    }
  }
}
```

### 4.1 序号

- `sequence` 在同一次连接会话中必须严格递增。
- 推荐使用 `UInt64`，每生成一份新快照加一。
- 不能因为发送拥塞而重复序号。
- 新连接发送 Hello 后可以从 1 重新开始。
- ESP32 会拒绝同一会话中小于或等于上一条的序号。

### 4.2 无效指标编码

某项采集失败时仍要保留该指标对象，并按以下形式发送：

```json
{
  "valid": false,
  "value": null,
  "unit": "celsius",
  "source": "LibreHardwareMonitor CPU Package",
  "error": "sensor_unavailable",
  "sampled_at_milliseconds": null
}
```

规则：

- `valid: false` 时 `value` 必须为 `null`，`error` 必须是非空字符串。
- `valid: true` 时 `value` 和采样时间必须有效，`error` 必须为 `null`。
- 一项传感器失败不能阻止另一项正常上报。
- `timestamp_milliseconds` 是生成整份遥测快照的 Unix Epoch 毫秒。
- `sampled_at_milliseconds` 是该项数据实际采集完成的 Unix Epoch 毫秒。

### 4.3 发送背压

- 任意时刻最多保留一条正在发送的遥测和一条“最新待发”遥测。
- 新快照到达时替换尚未发送的旧快照，绝不积压历史队列。
- 命令结果优先于遥测发送。
- 所有文本帧通过一个串行发送队列写入 `ClientWebSocket`，避免并发 Send。

## 5. ESP32 发出的切换命令

```json
{
  "type": "command",
  "protocol_version": 1,
  "request_id": "esp32-89abcdef-00000003",
  "target_device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "action": "switch_display",
  "target": "mac"
}
```

Windows 端执行前必须逐项满足：

- `type == "command"`
- `protocol_version == 1`
- `request_id` 非空且不超过 128 字节
- `target_device_id` 与本机持久化设备 ID 完全一致
- `action == "switch_display"`
- `target` 只能为 `"mac"` 或 `"windows"`

不满足结构或身份校验的报文只记录警告，不执行 DDC。对于结构完整、确实发给本机的命令，即使 KVM 关闭或 DDC 失败，也必须返回结果。

目标映射固定为：

| `target` | X41Q 物理接口 | VCP `0x60` 写入值 |
|---|---|---:|
| `mac` | HDMI 2 | 6 |
| `windows` | DP | 7 |

正常双机流程中，Windows 当前在线显示时通常只会收到 `target: "mac"`。仍需完整支持两种目标，供手动测试和未来扩展使用。

## 6. Windows 返回命令结果

### 6.1 写入成功、未读回确认（X41Q 默认）

```json
{
  "type": "command_result",
  "protocol_version": 1,
  "request_id": "esp32-89abcdef-00000003",
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "success": true,
  "write_succeeded": true,
  "confirmed": false,
  "requested_input": 6,
  "read_back_input": null,
  "error": "input_written_but_unconfirmed"
}
```

### 6.2 写入且读回确认成功

```json
{
  "type": "command_result",
  "protocol_version": 1,
  "request_id": "esp32-89abcdef-00000003",
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "success": true,
  "write_succeeded": true,
  "confirmed": true,
  "requested_input": 6,
  "read_back_input": 6,
  "error": null
}
```

### 6.3 写入失败

```json
{
  "type": "command_result",
  "protocol_version": 1,
  "request_id": "esp32-89abcdef-00000003",
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "success": false,
  "write_succeeded": false,
  "confirmed": false,
  "requested_input": 6,
  "read_back_input": null,
  "error": "ddc_write_failed"
}
```

结果约束：

- `success` 必须始终等于 `write_succeeded`。
- `confirmed: true` 时，`write_succeeded` 必须为 true、`read_back_input` 必须等于 `requested_input`、`error` 必须为 null。
- `confirmed: false` 时，`error` 必须是非空字符串。
- `requested_input` 必须是本次实际尝试写入的整数值。
- ESP32 只接受仍在等待、请求 ID 相同、连接身份相同、输入值与目标相符的结果。
- 从 ESP32 发出命令到收到结果最多只有 5 秒；Windows 端应在 4 秒内完成并发送结果。

## 7. 标准错误码

遥测：

- `sensor_unavailable`
- `sensor_read_failed`
- `memory_read_failed`
- `data_unavailable`
- `data_stale`

DDC 与命令：

- `kvm_disabled`
- `display_not_selected`
- `display_not_found`
- `ddc_transport_unavailable`
- `ddc_write_failed`
- `ddc_operation_timed_out`
- `invalid_ddc_response`
- `input_written_but_unconfirmed`

错误详情写入本地日志，不要把任意长度的异常文本直接放入协议。发送给 ESP32 的 `error` 必须使用上述稳定短码，长度不得超过 63 字节。

## 8. 断线与恢复

- 网络断开时停止发送，但继续以“只保留最新值”的方式更新本地遥测快照。
- 重连后先发送 Hello，再立即发送最新快照，然后恢复 1 秒周期。
- 不补发断线期间的历史遥测。
- 断线时尚未发送完成的命令结果记录为错误；不得在一个新连接上重新执行对应 DDC。
- 系统睡眠后暂停无意义重试；恢复后触发一次立即重连。
- 用户点击“立即重连”时取消当前连接并立刻开始一个新连接，不能启动第二套并行连接循环。
