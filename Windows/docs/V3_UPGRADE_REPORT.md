# KVMBridgeWindows V3 仪表盘数据升级报告

日期：2026-09-17  
版本：3.0.0  
协议版本：1（保留既有消息语义）

## 1. 目录与存档

- V2 工作目录：`F:\Projects\KVM\Windows_v2`（未修改）。
- V2 独立存档：`F:\Projects\KVM\Windows_v2_archive_20260917`（未修改）。
- V3 开发与交付：`F:\Projects\KVM\Windows_v3`。
- 开发前 V2 Release 自动测试：63/63 通过。

## 2. 已实现

- `hello.capabilities` 新增 `dashboard_data`，已有三项能力保持不变。
- WebSocket 首帧仍为 `hello`；遥测和 dashboard 使用独立、重连后重置的序号。
- 每 10 秒从内存缓存发送一帧完整、未分片、最大 2048 UTF-8 字节的 dashboard；发送线程不访问互联网。
- 天气后台每 15 分钟刷新；行情每 10 秒刷新；失败按 30/60/120/300 秒退避并保留旧缓存。
- Open-Meteo、和风天气 v1、高德+Open-Meteo 小时补全三种天气适配器均已实现。
- 根据用户最终指令，日预报固定为今天、明天、后天三项。
- Binance 批量获取 BTC/USDT、DOGE/USDT，Frankfurter 获取 USD/CNY。
- 托盘增加天气源、城市、API Host、API Key 设置和立即刷新；规范城市仅在解析成功后持久化。
- API Key 使用 Windows Credential Manager；配置、dashboard、日志均不包含密钥。
- 网络失败不生成全零假天气；天气和行情可独立为 `null`，两者均无数据时发送稳定错误码。
- V2 的每秒遥测、DDC、显示休眠/唤醒、托盘、重连和命令结果语义保持不变。

## 3. 自动验证与发布

- Release 构建：成功，0 警告、0 错误。
- Release 自动测试：79/79 通过，0 失败，0 跳过。
- 新测试覆盖三天气源固定样本、今天/明天/后天、五小时、官方字段转换、UTF-8 城市长度、高德映射、Binance/Frankfurter 数字转换、协议字段、序列、2048 字节、密钥不入日志，以及本地 WebSocket 首帧/独立序号/未分片发送。
- 发布目标：win-x64、自包含、单文件、内嵌原生库。
- 发布目录：`F:\Projects\KVM\Windows_v3\publish\v3-win-x64-single-file`。
- EXE 版本：3.0.0.0。
- EXE SHA-256：`1F4E9379DC743EEAD753D4C7EA492D8BC66C9F9EA3AC1F14584E17954AAD1334`。
- 脱敏序列化报文：`F:\Projects\KVM\Windows_v3\dashboard.sample.redacted.json`。该文件由 V3 合同字段和固定验证数据生成；设备 ID 已脱敏。

## 4. 外部接口验证边界

- 本次构建机可访问 Binance，已取得 BTCUSDT/DOGEUSDT 正常字符串价格与涨跌字段。
- Open-Meteo 和 Frankfurter 在最后一次构建机现场探测时出现 TLS 握手/超时；对应客户端超时、错误码、缓存和退避路径已实现，固定响应测试已通过，但不能把该次探测写成在线成功。
- 和风天气和高德天气需要用户自己的 Host/Key，未使用或提交任何真实凭据；解析严格按官方响应结构测试。

## 5. 仍需实机验收

1. 在目标 Windows 网络中配置城市，确认 Open-Meteo 或带凭据的数据源能在线刷新。
2. ESP32 依次显示 SYSTEM、WEATHER、MARKETS，日预报为今天、明天、后天。
3. 验证默认 10 秒 dashboard、1 秒 telemetry，以及城市保存后的下一帧同步。
4. 关闭 Windows 端 30 秒确认 Mac 接管；恢复 Windows 并稳定发送 60 秒后确认切回。
5. 断网时确认 ESP32 保留旧缓存并标记错误/过期。
6. 回归真实显示器 DDC、显示休眠/唤醒和托盘操作。

以上涉及 ESP32、Mac、真实 API 凭据和显示器硬件的项目不能由本机构建测试代替，因此未标记为已完成。
