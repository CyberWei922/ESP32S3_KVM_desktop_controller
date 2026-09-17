# Wi-Fi 本地配置

## 创建私密配置

固件默认使用路由器 DHCP。请复制示例文件：

```bash
cd /Users/wei/Projects/KVM/esp32/latest
cp main/include/kvm_config.example.h main/include/kvm_config.local.h
```

然后只编辑 `main/include/kvm_config.local.h`：

```c
#pragma once
#define KVM_WIFI_SSID "你的 Wi-Fi 名称"
#define KVM_WIFI_PASSWORD "你的 Wi-Fi 密码"
```

`*.local.h` 已由项目根目录的忽略规则排除。不要把实际配置复制到文档、聊天记录、截图、版本归档或公开仓库；制作新的 legacy 版本时也必须排除这个本地文件。

不创建 `kvm_config.local.h` 也能正常构建。固件会继续运行 LCD，Wi-Fi 保持关闭，并在画面/串口中明确显示 `wifi_not_configured`，不会加载模拟主机数据。

## 构建与地址

在已经配置好的 ESP-IDF 终端中：

```bash
cd /Users/wei/Projects/KVM/esp32/latest
idf.py build
```

首次新建或删除本地配置头后，请先执行一次 `ESP-IDF: Reconfigure Project` 再 Build，确保构建系统重新检查这个可选文件。

烧录和真机连接由用户手动进行。连接成功后，串口会打印 DHCP 地址；StatsForKVM 的服务地址应设为：

```text
ws://<打印出的地址>:81/statsforkvm
```

建议在路由器里按 ESP32 设备建立 DHCP 地址保留，不在固件里硬编码固定 IP。程序不会打印 Wi-Fi 密码。

## 故障提示

- `wifi_not_configured`：本地配置文件不存在，或 SSID 仍为空。
- `check credentials`：认证失败或四次握手超时，核对密码和接入点安全方式。
- `access point not found`：SSID 不可见、距离过远或频段不兼容。
- `Wi-Fi disconnected`：固件会以 1、2、4、8、16、30 秒上限指数退避重连。
- 已取得 IP 但 Mac 连不上：核对 macOS 本地网络权限、VPN/代理路由、防火墙和 StatsForKVM 地址。

ESP32-S3 使用 2.4 GHz Wi-Fi。真实联网、断线恢复、VPN 环境和长时间稳定性尚未在本版自动验证。
