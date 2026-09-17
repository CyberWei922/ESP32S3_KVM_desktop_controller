# V2 → V3 配置迁移

V3 继续使用 `%LOCALAPPDATA%\KVMBridgeWindows\config.json`，`schema_version` 仍为 `1`。旧 V2 配置缺少 `dashboard` 时会自动使用安全默认值，不影响连接、遥测、DDC、显示休眠/唤醒和开机启动设置。

新增默认配置：

```json
"dashboard": {
  "enabled": true,
  "send_interval_seconds": 10,
  "weather_refresh_minutes": 15,
  "weather_provider": "open_meteo",
  "city_code": "",
  "city_name": "",
  "latitude": null,
  "longitude": null,
  "timezone": "",
  "qweather_api_host": "",
  "qweather_api_key_credential_name": "KVMBridgeWindows.QWeather",
  "amap_api_key_credential_name": "KVMBridgeWindows.AMap"
}
```

首次启动 V3 后，在托盘中选择“天气与行情设置…”：

- Open-Meteo：填写 GeoNames 数字 ID 或中国城市名，不需要密钥；
- 和风天气：填写用户专属 API Host、Location ID/城市名和 API Key；
- 高德天气：填写 6 位 adcode 和 Web 服务 Key。

城市解析成功后才保存规范代码、名称、经纬度和时区；失败时保留原城市和缓存。和风、高德 API Key 写入 Windows Credential Manager，目标名分别为 `KVMBridgeWindows.QWeather` 和 `KVMBridgeWindows.AMap`，不会写入 JSON 或日志。

根据本次开发时用户的明确修改，`daily` 三项为“今天、明天、后天”，覆盖升级指南原文的“明天、后天、大后天”。
