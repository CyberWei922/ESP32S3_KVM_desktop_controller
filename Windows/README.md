# KVMBridgeWindows

Windows 端常驻托盘客户端，使用 C#、.NET 8 和 Windows Forms 开发。当前版本为 `v3.0.0`，与 ESP32 protocol v1 和 macOS `StatsForKVM` 客户端配套使用。

## 主要功能

- 每秒采集并上报 CPU 温度与内存占用；
- 通过 DDC/CI 在 Mac HDMI 2（值 6）和 Windows DP（值 7）之间切换显示器；
- 响应 ESP32 的显示输出休眠和唤醒命令，黑屏期间保持网络与后台任务运行；
- 每 10 秒发送天气、BTC/USDT、DOGE/USDT 和 USD/CNY dashboard；
- 支持 Open-Meteo、和风天气和高德天气，日预报为今天、明天、后天；
- Windows 作为 dashboard 主数据源，支持断线重连和缓存退避；
- API Key 保存到 Windows Credential Manager，不写入配置或日志；
- 提供托盘配置、显示器选择、手动测试、日志和开机启动功能。

## 目录

```text
src/KVMBridgeWindows/          客户端源码
tests/KVMBridgeWindows.Tests/ 自动测试
docs/                         协议、配置迁移和升级报告
release/                      win-x64 自包含成品包
```

## 使用成品

下载并解压 [`release/KVMBridgeWindows-v3.0.0-win-x64.zip`](release/KVMBridgeWindows-v3.0.0-win-x64.zip)，运行 `KVMBridgeWindows.exe`。首次运行后，在托盘中配置 ESP32 地址、目标显示器及天气城市。

配置和运行数据保存在：

```text
%LOCALAPPDATA%\KVMBridgeWindows
```

天气配置和 V2 升级说明见 [`docs/CONFIG_MIGRATION_V3.md`](docs/CONFIG_MIGRATION_V3.md)。

## 从源码构建

需要 Windows x64 和 .NET 8 SDK：

```powershell
dotnet test KVMBridgeWindows.sln -c Release
dotnet publish src\KVMBridgeWindows\KVMBridgeWindows.csproj `
  -c Release -r win-x64 --self-contained true `
  -p:PublishSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true
```

当前 Release 自动测试为 79/79 通过。涉及 ESP32、Mac、DDC 显示器和真实 API 凭据的项目仍需在对应硬件环境完成联调。
