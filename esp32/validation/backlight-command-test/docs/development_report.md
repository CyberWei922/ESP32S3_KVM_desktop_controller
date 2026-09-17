# v0.4.0-dev 开发交付记录

日期：2026-09-08

## 修改文件

核心固件：

- `main/app_main.c`
- `main/app_state.c`
- `main/include/app_state.h`
- `main/network_manager.c`
- `main/include/network_manager.h`
- `main/websocket_server.c`
- `main/include/websocket_server.h`
- `main/protocol.c`
- `main/include/protocol.h`
- `main/lcd_test_ui.c`
- `main/include/lcd_test_ui.h`
- `main/lcd_driver.c`（只补充显示 `-` 所需字形，已验证的 GPIO、初始化序列、MADCTL、SPI Mode 3 和 10 MHz 均未改动）
- `main/CMakeLists.txt`
- `main/idf_component.yml`
- `main/include/kvm_config.example.h`
- `sdkconfig`
- `sdkconfig.defaults`
- `dependencies.lock`

测试与说明：

- `test/protocol_test.c`
- `test/run_protocol_tests.sh`
- `VERSION.md`
- `docs/protocol_v1.md`
- `docs/wifi_configuration.md`
- `docs/lcd_wiring.md`
- `docs/development_report.md`
- 项目根目录 `README.md`、`esp32/README.md` 和 `development_roadmap.md`

ESP-IDF Component Manager 生成了 `managed_components/espressif__cjson/`，`build/` 已更新为本版完整构建产物。`StatsForKVM/` 源码未修改。

## 执行的验证

协议固定向量：

```bash
cd /Users/wei/Projects/KVM/esp32/latest
test/run_protocol_tests.sh
```

结果：`protocol fixed-vector tests: PASS`。

固件构建与尺寸：

```bash
cd /Users/wei/Projects/KVM/esp32/latest
idf.py build
idf.py size
```

初次无本地 Wi-Fi 配置构建成功。用户创建本机配置后重新生成的 `kvm_controller.bin` 为 `0xdb1a0` bytes（897,440 bytes），1 MiB 应用分区剩余 `0x24e60` bytes（14%）。size 工具报告镜像内容 897,316 bytes、DIRAM 使用 123,530 / 341,760 bytes（36.15%）、独立 IRAM 区使用 16,384 / 16,384 bytes。完整结果以本版 `build/kvm_controller.map` 和构建日志为准。

可提交源码和文档不包含真实凭据；`kvm_config.local.h` 由忽略规则保护。含本机配置的构建产物会正常包含联网所需的凭据，因此不得分发或复制到 legacy 归档。

本机存在 ESP-IDF Python 3.12/原工程 Python 3.14 环境提示，因此验证时显式使用原工程的 `idf6.1_py3.14_env`；没有升级 ESP-IDF 或重建工具链，构建实际使用 ESP-IDF v6.1。

## 自动验证范围外

本轮未执行烧录、串口连接、真实 Wi-Fi、macOS 本地网络授权、真实 WebSocket、LCD 真机检查、VPN、Windows 客户端、DDC 或长时间运行。`v0.4.0-dev` 只有在这些相关真机项目通过后才能冻结。

## 最短人工测试

1. 按 `wifi_configuration.md` 创建仅本机使用的配置文件。
2. 断电检查原 LCD 接线，确认 VCC 为 3.3V、BLK/CS2/PEN 悬空；不要改动已验证的 GPIO。
3. 在 `esp32/latest/` 手动执行 Build、Flash、Monitor，确认版本为 `0.4.0-dev`、Flash 16 MB、PSRAM 8 MB，并记下 DHCP 地址。
4. 把 StatsForKVM 地址设为 `ws://<该地址>:81/statsforkvm`，运行应用并处理 macOS 本地网络权限。
5. 确认 LCD 显示 Mac `ONLINE`、真实 CPU 摄氏温度和内存百分比，Windows 保持 `OFFLINE`。
6. 停止 StatsForKVM：确认 socket 断开后立即 `OFFLINE`；若只停止遥测但保持连接，5 秒后应为 `STALE`；恢复后应自动回到 `ONLINE`。
7. 再测试 ESP32 重启、Wi-Fi 断开恢复和同一 Mac 重新连接。DDC 命令接口本轮不需要触发。
