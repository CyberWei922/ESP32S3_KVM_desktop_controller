# Windows 端仪表盘数据升级指南（天气 / 行情 / 状态页）

本文是现有 `KVMBridgeWindows` 的增量开发合同。Windows 上一版如果已经按本目录其余文档完成，不要重写连接、遥测、DDC、托盘或配置基础设施；只在现有项目中增加本文件定义的数据采集和 `dashboard` 消息。

## 1. 本次目标

Windows 是外部数据的主提供端，macOS `StatsForKVM` 是备用提供端，ESP32 只负责校验、缓存和显示。

Windows 必须提供：

- 天气：当前天气、未来三天、未来五个整点；
- 行情：BTC/USDT、DOGE/USDT、USD/CNY；
- 城市配置：用户选择天气源并输入城市代码或城市名，程序自动解析规范城市名；
- 每 10 秒向 ESP32 发送一帧完整 `dashboard` 快照；
- 保留已有每秒一次的 CPU 温度和内存遥测；
- 保留已有 DDC、休眠/唤醒、托盘菜单和 WebSocket 重连功能。

ESP32 页面顺序已经固定：

1. `SYSTEM`：ESP32 IP、运行状态、数据来源与错误；
2. `WEATHER`：左侧当前天气与未来三天，右侧未来五个整点；
3. `MARKETS`：BTC、DOGE、USD/CNY。

ESP32 中键单击翻到下一页，中键双击翻到上一页。Windows 不发送翻页命令。

## 2. 主备规则

- Windows 是主数据源。
- ESP32 收到 Windows 的有效 `dashboard` 后使用 Windows 数据。
- Windows 连续 30 秒未发送有效 `dashboard`，数据标为 stale；随后收到的 Mac 快照可接管。
- Mac 接管后，Windows 恢复时必须连续稳定发送 60 秒，ESP32 才切回 Windows。
- “连续稳定”的定义：相邻 Windows 快照间隔不得超过 20 秒，且 `sequence` 严格递增。
- Mac 和 Windows 都离线时，ESP32 保留最后一份数据显示 stale，不自行访问互联网。

因此 Windows 端必须每 10 秒发送一次快照，即使天气在这 10 秒内没有变化。天气和行情的抓取周期与发送周期不是一回事。

## 3. WebSocket 与握手

沿用现有地址：

```text
ws://<ESP32-IP>:81/statsforkvm
```

连接建立后第一帧仍必须是现有 `hello`：

```json
{
  "type": "hello",
  "protocol_version": 1,
  "device_id": "windows-稳定且持久化的ID",
  "platform": "windows",
  "app": "KVMBridgeWindows",
  "capabilities": ["switch_display", "display_sleep", "display_wake", "dashboard_data"]
}
```

之后同时运行两个互不共用序号的发送循环：

- `telemetry.sequence`：每秒递增；
- `dashboard.sequence`：每 10 秒递增。

重连后两个序号都可以从 1 重新开始，因为 ESP32 会在新 `hello` 时重置会话状态。

## 4. dashboard 消息的完整合同

消息必须是 UTF-8、单个未分片 WebSocket text frame，压缩前不得超过 2048 字节。字段名必须完全一致，数值必须发送 JSON number，禁止把数值写成字符串。

```json
{
  "type": "dashboard",
  "protocol_version": 1,
  "platform": "windows",
  "device_id": "windows-稳定且持久化的ID",
  "sequence": 42,
  "timestamp_milliseconds": 1789574400000,
  "weather": {
    "provider": "qweather",
    "city_code": "101020100",
    "city_name": "上海",
    "updated_at_milliseconds": 1789574380000,
    "current": {
      "temperature_c": 27,
      "high_c": 31,
      "low_c": 24,
      "weather_code": 101,
      "is_day": true
    },
    "daily": [
      {"day":"FRI","high_c":30,"low_c":24,"weather_code":305},
      {"day":"SAT","high_c":29,"low_c":23,"weather_code":104},
      {"day":"SUN","high_c":31,"low_c":24,"weather_code":100}
    ],
    "hourly": [
      {"hour":14,"temperature_c":28,"weather_code":101,"is_day":true},
      {"hour":15,"temperature_c":29,"weather_code":101,"is_day":true},
      {"hour":16,"temperature_c":29,"weather_code":305,"is_day":true},
      {"hour":17,"temperature_c":28,"weather_code":305,"is_day":true},
      {"hour":18,"temperature_c":27,"weather_code":104,"is_day":true}
    ]
  },
  "markets": {
    "updated_at_milliseconds": 1789574399000,
    "btc_usdt": {"price": 65432.1, "change_percent": 1.23},
    "doge_usdt": {"price": 0.12345, "change_percent": -0.67},
    "usd_cny": {"price": 7.1234, "change_percent": 0.0}
  },
  "error": null
}
```

### 4.1 顶层字段

| 字段 | 规则 |
|---|---|
| `type` | 固定 `dashboard` |
| `protocol_version` | 固定整数 `1` |
| `platform` | Windows 固定 `windows` |
| `device_id` | 必须与本连接 `hello.device_id` 完全相同，1–128 字节 |
| `sequence` | 0 到 2^53-1，当前 WebSocket 会话内严格递增 |
| `timestamp_milliseconds` | Unix 毫秒时间戳 |
| `weather` | 有有效天气时为对象；暂时无天气时必须是 `null` |
| `markets` | 有完整三项行情时为对象；暂时无行情时必须是 `null` |
| `error` | 正常为 `null`；异常时为 1–63 字节的稳定机器码 |

`weather` 和 `markets` 可以有一个为 `null`。如果两者同时为 `null`，`error` 必须非空，例如 `dashboard_loading`。

### 4.2 天气字段

- `provider`：只允许实现方定义的短标识，当前使用 `open_meteo`、`qweather`、`amap`；不超过 15 字节。
- `city_code`、`city_name`：非空，各自少于 32 个 UTF-8 字节。
- 温度全部四舍五入成整数摄氏度，有效范围 -80 到 70。
- `weather_code` 为 0–999 的整数。
- `daily` 必须恰好 3 项，表示明天、后天、大后天，不包含今天。
- `daily.day` 必须是 3 个大写 ASCII 字母：`MON` 到 `SUN`。
- `hourly` 必须恰好 5 项，从下一个可用整点开始，按时间升序。
- `hour` 为当地时间 0–23。
- `is_day` 必须是 JSON boolean。

ESP32 图标分类支持两套代码：

- WMO/Open-Meteo：晴 0–1，多云 2–3，雾 45–48，雨 51–82，雪 71–86，雷暴 95–99；
- 和风：晴 100，多云 101–104，雨 300–399，雪 400–499，雾霾 500–515。

高德只返回中文天气文本时，Windows 适配器必须先映射到 WMO 代码：晴→0，多云/阴→3，雾/霾→45，雨→61，雪/雨夹雪→71，雷阵雨→95。禁止把字符串哈希或高德内部无文档数值直接当成天气码。

### 4.3 行情字段

- 三项必须同时存在，缺任意一项时整段 `markets` 发送 `null`，继续保留并重试本地缓存。
- `price` 必须大于 0。
- `change_percent` 是百分数，不是比例；`1.23` 表示 `+1.23%`。
- Binance 的字符串数字在发送前必须解析为 `decimal/double`。
- USD/CNY 数据源如果没有日涨跌字段，`change_percent` 固定发送 `0.0`。

## 5. 天气源实现

托盘菜单增加“天气设置”子菜单或一个轻量配置对话框。主程序仍不需要完整 UI。至少支持以下三种选择：

### 5.1 Open-Meteo（默认，无密钥）

城市解析：

- 输入纯数字 GeoNames ID：`GET https://geocoding-api.open-meteo.com/v1/get?id={id}`；
- 输入城市名：`GET https://geocoding-api.open-meteo.com/v1/search?name={name}&count=1&language=zh&countryCode=CN`。

预报：

```text
GET https://api.open-meteo.com/v1/forecast
  ?latitude={lat}
  &longitude={lon}
  &current=temperature_2m,weather_code,is_day
  &hourly=temperature_2m,weather_code,is_day
  &daily=weather_code,temperature_2m_max,temperature_2m_min
  &timezone=auto
  &forecast_days=4
```

### 5.2 和风天气（大陆优先源）

设置项：

- 用户专属 API Host，例如 `abcxyz.qweatherapi.com`；
- API Key；
- Location ID，例如上海 `101020100`。

认证使用请求头：

```text
X-QW-Api-Key: <用户密钥>
```

解析城市：

```text
GET https://{api-host}/geo/v2/city/lookup?location={city-code}&range=cn&number=1&lang=zh
```

从结果保存 `id`、`name`、`lat`、`lon`，然后调用：

```text
GET /weather/v1/current/{lat}/{lon}?lang=zh
GET /weather/v1/daily/{lat}/{lon}?days=4&localTime=true&lang=zh
GET /weather/v1/hourly/{lat}/{lon}?hours=6&localTime=true&lang=zh
```

API Host 和 API Key 必须是用户设置，禁止写死或提交到 Git。

### 5.3 高德天气（大陆备用源）

设置项为 Web 服务 Key 和 6 位 `adcode`。天气接口：

```text
GET https://restapi.amap.com/v3/weather/weatherInfo?city={adcode}&extensions=base&key={key}
GET https://restapi.amap.com/v3/weather/weatherInfo?city={adcode}&extensions=all&key={key}
```

高德标准天气接口没有完整逐小时预报，因此本项目定义为组合适配器：

1. 当前天气和未来日预报以高德为准；
2. 使用高德地理编码获得城市中心经纬度；
3. 五小时预报由 Open-Meteo 按同一经纬度补全；
4. `provider` 仍写 `amap`，日志中记录 hourly fallback；
5. 任一关键步骤失败时不要伪造数据，保留上次缓存并设置错误码。

## 6. 城市设置与名称同步

托盘菜单建议结构：

```text
天气设置
  数据源 > Open-Meteo / 和风天气 / 高德天气
  城市代码…
  API Host…（仅和风）
  API Key…（和风或高德）
  当前城市：上海
  立即刷新
```

保存城市代码时必须立即执行解析：

1. 去除首尾空格；
2. 调用选定数据源的城市解析接口；
3. 成功后持久化规范代码、规范名称、经纬度和时区；
4. 托盘菜单把“当前城市”更新为规范名称；
5. 下一帧 `dashboard.weather.city_name` 自动同步到 ESP32；
6. 解析失败时保留旧城市，不覆盖旧缓存，并提示 `city_not_found`。

API Key 建议存 Windows Credential Manager；不要明文写日志。若当前项目暂时只能写 JSON 配置，必须在日志中脱敏，并在后续迁移到 Credential Manager。

## 7. 行情源

每 10 秒抓取一次 Binance 24 小时 ticker：

```text
GET https://api.binance.com/api/v3/ticker/24hr?symbols=["BTCUSDT","DOGEUSDT"]
```

必须对 `symbols` 查询参数进行 URL 编码。读取：

- `lastPrice` → `price`；
- `priceChangePercent` → `change_percent`。

USD/CNY 使用：

```text
GET https://api.frankfurter.app/latest?from=USD&to=CNY
```

读取 `rates.CNY`，涨跌字段先固定 0。三个请求均设置 10–12 秒超时，HTTP 非 2xx、JSON 错误或数值越界均视为失败。

## 8. 推荐代码结构

在现有解决方案中增加，不要破坏已有类：

```text
Dashboard/
  DashboardModels.cs
  DashboardCoordinator.cs
  DashboardMessageFactory.cs
  Weather/
    IWeatherProvider.cs
    OpenMeteoWeatherProvider.cs
    QWeatherProvider.cs
    AMapWeatherProvider.cs
    WeatherCodeMapper.cs
  Markets/
    BinanceMarketProvider.cs
    ExchangeRateProvider.cs
Settings/
  DashboardSettings.cs
  DashboardSettingsStore.cs
```

职责：

- `DashboardCoordinator`：定时抓取、缓存、错误状态、生成快照；
- `IWeatherProvider.ResolveCityAsync`：代码/名称→规范城市；
- `IWeatherProvider.GetForecastAsync`：输出统一天气模型；
- `DashboardMessageFactory`：只做协议序列化与 2048 字节检查；
- WebSocket 连接服务：只负责每 10 秒取当前缓存并发送，不直接访问互联网。

并发要求：

- 禁止每次发送时同步等待 HTTP；
- 天气刷新 15 分钟一次；
- 行情刷新 10 秒一次；
- 同类请求只允许一个在途任务；
- 新配置应用后立即刷新一次；
- 网络失败使用旧缓存，后台按 30、60、120、300 秒退避，成功后恢复正常周期；
- 应用关闭时正确取消所有 `CancellationToken`。

## 9. 配置文件增量

在现有配置模型中增加：

```json
{
  "dashboard": {
    "enabled": true,
    "send_interval_seconds": 10,
    "weather_refresh_minutes": 15,
    "weather_provider": "qweather",
    "city_code": "101020100",
    "city_name": "上海",
    "latitude": 31.2304,
    "longitude": 121.4737,
    "qweather_api_host": "",
    "qweather_api_key_credential_name": "KVMBridgeWindows.QWeather",
    "amap_api_key_credential_name": "KVMBridgeWindows.AMap"
  }
}
```

`city_name` 和经纬度是最近一次成功解析的缓存。设置文件迁移必须给旧配置自动补默认值，不得导致上一版配置无法启动。

## 10. 错误码

对 ESP32 只发送一个最有价值的短错误码：

- `dashboard_loading`
- `city_code_missing`
- `city_not_found`
- `qweather_credentials_missing`
- `weather_fetch_failed`
- `weather_response_invalid`
- `hourly_forecast_incomplete`
- `market_fetch_failed`
- `dashboard_message_too_large`

详细 HTTP 状态、异常堆栈和提供商响应只写本机滚动日志，不发给 ESP32。日志必须隐藏 API Key。

## 11. 必须增加的测试

单元测试：

- 每个天气源的固定 JSON 样本能转换成同一个统一模型；
- 今天不进入 `daily`，输出恰好未来三天；
- 小时输出恰好五项且按当地时间升序；
- 中文城市名按 UTF-8 长度校验；
- AMap 中文天气映射正确；
- Binance 字符串价格转数字正确；
- `sequence` 严格递增；
- 序列化结果小于等于 2048 字节；
- API Key 不出现在日志或导出的诊断信息中。

集成测试：

1. 启动 Windows，确认第一帧是 `hello`；
2. 确认遥测仍每秒一次；
3. 确认 dashboard 每 10 秒一次；
4. 在托盘切换城市，名称和天气应在下一帧同步到 ESP32；
5. ESP32 中键单击依次看到 SYSTEM、WEATHER、MARKETS；
6. 关闭 Windows 程序 30 秒，Mac 数据接管；
7. 重启 Windows，持续 60 秒后 ESP32 数据来源回到 Windows；
8. 断网时程序不崩溃，ESP32 保留缓存并显示错误；
9. 旧有 KVM 切换、显示器休眠/唤醒和托盘功能全部回归通过。

## 12. 不允许的实现

- 不允许让 ESP32 直接访问天气或行情 API；
- 不允许 Windows 与 Mac 互相转发数据；
- 不允许用 UI 文本作为协议字段；
- 不允许天气失败时发送全零的假天气；
- 不允许把 API Key、完整请求头或用户凭据写日志；
- 不允许因为 dashboard 请求阻塞每秒遥测或 DDC 指令处理；
- 不允许修改既有 `telemetry` 和 `command_result` 字段语义。

完成本升级后，将构建产物、配置迁移说明、测试结果和一份脱敏的实际 `dashboard` 报文一并交回。
