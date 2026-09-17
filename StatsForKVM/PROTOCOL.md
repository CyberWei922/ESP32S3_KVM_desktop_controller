# StatsForKVM WebSocket Protocol v1

本文档描述 StatsForKVM 当前代码已经实现的线缆协议，供后续 ESP32 和 Windows 客户端共同使用。协议版本为整数 `1`。

## 1. 连接模型

- ESP32 是 WebSocket 服务器。
- macOS StatsForKVM 和未来 Windows 程序分别作为客户端主动连接。
- 默认地址：`ws://192.168.1.50:81/statsforkvm`，可在 Monitor 页面修改。
- ESP32 必须允许 Mac 和 Windows 同时保持独立连接。
- 每个客户端连接成功后先发送一条 `hello`，随后发送 `telemetry`。
- ESP32 应按 `device_id` 保存每个客户端的最新快照和最后接收时间，不累计历史帧。
- 所有应用消息均为 UTF-8 JSON 文本帧，单条不得超过 2048 bytes。
- StatsForKVM 每 15 秒发送 WebSocket ping，支持服务器 pong。

## 2. 通用规则

- 字段名使用 `snake_case`。
- 时间使用 Unix epoch 毫秒整数。
- 当前允许的 `platform`：`macos`、`windows`。
- 未知 `protocol_version` 必须拒绝，不能按近似版本解释。
- JSON 数值必须是有限数；无效采样使用 `null`，不能用 `0` 伪装。
- ESP32 收到新的同设备遥测后覆盖旧快照。
- 连接断开后，ESP32 应将该设备标记离线；过期阈值由 ESP32 端协议实现时确定并记录。

## 3. hello：客户端到 ESP32

Mac 每次连接或重连成功后发送：

```json
{
  "app": "StatsForKVM",
  "device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "platform": "macos",
  "capabilities": ["switch_display", "display_sleep", "display_wake", "dashboard_data"],
  "protocol_version": 1,
  "type": "hello"
}
```

字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `type` | string | 固定为 `hello` |
| `protocol_version` | integer | 固定为 `1` |
| `device_id` | string | 首次生成后持久保存的客户端身份 |
| `platform` | string | Mac 固定为 `macos` |
| `app` | string | Mac 固定为 `StatsForKVM` |
| `capabilities` | string array，可选 | 新客户端声明支持的命令；旧客户端省略时仍可连接 |

## 4. telemetry：客户端到 ESP32

首版默认每秒发送一次，只包含两个指标：

```json
{
  "device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "metrics": {
    "cpu.temperature.average": {
      "error": null,
      "sampled_at_milliseconds": 1788803532171,
      "source": "Average CPU",
      "unit": "celsius",
      "valid": true,
      "value": 42.5
    },
    "memory.usage": {
      "error": null,
      "sampled_at_milliseconds": 1788803532695,
      "source": "Stats RAM_Usage",
      "unit": "percent",
      "valid": true,
      "value": 63.2
    }
  },
  "platform": "macos",
  "protocol_version": 1,
  "sequence": 1,
  "timestamp_milliseconds": 1788803532719,
  "type": "telemetry"
}
```

顶层字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `type` | string | 固定为 `telemetry` |
| `protocol_version` | integer | 固定为 `1` |
| `device_id` | string | 与本连接 `hello` 一致 |
| `platform` | string | Mac 固定为 `macos` |
| `sequence` | unsigned integer | 当前应用进程内单调递增；重启后允许重新开始 |
| `timestamp_milliseconds` | integer | JSON 快照编码时间 |
| `metrics` | object | 指标 ID 到指标对象的映射 |

指标对象字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `value` | number 或 null | 有效数值；无效时必须为 `null` |
| `unit` | string | `celsius` 或 `percent` |
| `valid` | boolean | 当前值是否可用且未过期 |
| `source` | string | `Average CPU` 或 `Stats RAM_Usage` |
| `error` | string 或 null | 有效时为 `null`，无效时为稳定错误码 |
| `sampled_at_milliseconds` | integer 或 null | 原始 Reader 最近有效采样时间 |

当前错误码：

- `sensor_unavailable`
- `sensor_read_failed`
- `memory_read_failed`
- `data_unavailable`
- `data_stale`

StatsForKVM 将超过 5 秒未更新的有效值转成 `value: null`、`valid: false`、`error: "data_stale"`。`sampled_at_milliseconds` 保留最后采样时间，方便接收端诊断，但旧数值不再发送。

## 5. dashboard：客户端到 ESP32

Mac与Windows每10秒发送一次天气和行情快照。它使用独立于telemetry的`sequence`；天气每15分钟抓取，行情每10秒抓取，但发送帧始终保持10秒心跳。Windows为主源，Mac为备用源。

```json
{
  "type":"dashboard",
  "protocol_version":1,
  "platform":"macos",
  "device_id":"mac-xxxxxxxx",
  "sequence":1,
  "timestamp_milliseconds":1789574400000,
  "weather":{
    "provider":"open_meteo",
    "city_code":"1796236",
    "city_name":"上海",
    "updated_at_milliseconds":1789574380000,
    "current":{"temperature_c":27,"high_c":31,"low_c":24,"weather_code":1,"is_day":true},
    "daily":[
      {"day":"FRI","high_c":30,"low_c":24,"weather_code":61},
      {"day":"SAT","high_c":29,"low_c":23,"weather_code":3},
      {"day":"SUN","high_c":31,"low_c":24,"weather_code":0}
    ],
    "hourly":[
      {"hour":14,"temperature_c":28,"weather_code":1,"is_day":true},
      {"hour":15,"temperature_c":29,"weather_code":1,"is_day":true},
      {"hour":16,"temperature_c":29,"weather_code":61,"is_day":true},
      {"hour":17,"temperature_c":28,"weather_code":61,"is_day":true},
      {"hour":18,"temperature_c":27,"weather_code":3,"is_day":true}
    ]
  },
  "markets":{
    "updated_at_milliseconds":1789574399000,
    "btc_usdt":{"price":65432.1,"change_percent":1.23},
    "doge_usdt":{"price":0.12345,"change_percent":-0.67},
    "usd_cny":{"price":7.1234,"change_percent":0.0}
  },
  "error":null
}
```

`daily`固定3项且不含今天；`hourly`固定5项；无效分区发送`null`，禁止发送假零值。两个分区同时为`null`时`error`必须为非空稳定错误码。完整字段范围、数据源和主备时序以本文协议为准；Windows 客户端实现将在后续版本同步。

## 6. command：ESP32 到客户端

protocol v1使用能力协商支持显示器输入切换与显示输出休眠/唤醒。旧客户端没有声明新能力时，ESP32不得向它发送新命令。

```json
{
  "action": "switch_display",
  "protocol_version": 1,
  "request_id": "esp32-000001",
  "target": "windows",
  "target_device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "type": "command"
}
```

校验规则：

- `type` 必须为 `command`。
- `protocol_version` 必须为 `1`。
- `request_id` 必须非空且 UTF-8 长度不超过 128 bytes。
- `target_device_id` 必须与接收客户端的持久设备 ID 完全一致，长度不超过 128 bytes。
- `action=switch_display`时，`target`只允许`mac`或`windows`。
- `action=display_sleep`或`display_wake`时不携带`target`，并且客户端必须在Hello中声明对应能力。
- 实际 VCP 输入编号只从 KVM 页面保存的配置读取，ESP32 不能在消息中注入任意 VCP 值。
- 客户端在内存中保留最近 64 个请求 ID，重复请求不得重复触发物理切换。

显示电源命令示例：

```json
{
  "action": "display_sleep",
  "protocol_version": 1,
  "request_id": "esp32-sleep-00000001",
  "target_device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "type": "command"
}
```

`display_wake`结构相同，只替换`action`。两者均只请求操作系统停止或恢复视频输出，不代表ESP32已经物理确认X41Q状态。

## 7. command_result：客户端到 ESP32

```json
{
  "confirmed": true,
  "device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "error": null,
  "protocol_version": 1,
  "read_back_input": 15,
  "request_id": "esp32-000001",
  "requested_input": 15,
  "success": true,
  "type": "command_result",
  "write_succeeded": true
}
```

结果语义：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `success` | boolean | 当前版本等同于 DDC 写入是否返回成功 |
| `write_succeeded` | boolean | DDC 写命令是否成功提交 |
| `confirmed` | boolean | 延迟读回值是否与请求输入一致 |
| `requested_input` | integer | KVM 设置中保存的目标输入值 |
| `read_back_input` | integer 或 null | 成功读回时的当前输入；无法读回为 `null` |
| `error` | string 或 null | 完全确认时为 `null`；失败或未确认时给出原因 |

显示器切换时可能出现“写入成功但显示器在换源期间暂时不响应读回”。此时：

```json
{
  "success": true,
  "write_succeeded": true,
  "confirmed": false,
  "read_back_input": null,
  "error": "input_written_but_unconfirmed"
}
```

ESP32 UI 必须区分这个状态与完全确认，不能只看 `success` 就宣称画面已经切换。

常见 KVM 错误码：

- `kvm_disabled`
- `display_not_selected`
- `display_not_found`
- `ddc_transport_unavailable`
- `ddc_write_failed`
- `invalid_ddc_response`
- `input_written_but_unconfirmed`

显示电源结果使用独立字段，不能混入DDC字段：

```json
{
  "action": "display_wake",
  "device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "display_power_state": "wake_requested",
  "error": null,
  "protocol_version": 1,
  "request_id": "esp32-wake-00000002",
  "success": true,
  "type": "command_result"
}
```

`display_power_state`只允许`sleep_requested`、`wake_requested`、`unchanged`。失败必须使用`unchanged`并带稳定错误码。重复的`request_id`必须返回缓存结果，不能重复执行。

## 8. 重连与流控

- Mac 重连退避为 1、2、4、8、16、30 秒，之后维持 30 秒上限。
- 断线或网络路径变化后丢弃待发送遥测；恢复后只发送最新快照。
- Mac 端最多保留一条正在发送和一条最新待发，不构成历史队列。
- ESP32 同样只能保存每台设备的最新状态，不能把持续遥测堆入无界容器。
- 开启 VPN 时，固定局域网 IP 仍是主目标；Bonjour 只提供辅助发现信息，不自动改写保存地址。

## 9. v1 尚待实机确定的参数

- ESP32 最大客户端数及内存预算。
- ESP32 离线/过期显示阈值。
- 在真实 ESP32 库中可接受的最大帧是否维持 2048 bytes 或进一步收紧。
- Windows 客户端的 `app` 名称、设备 ID 保存方式和温度来源。
- 是否在可信局域网之外增加配对令牌；v1 当前不含鉴权。

以上项目实测前不得宣称端到端协议验收完成。
