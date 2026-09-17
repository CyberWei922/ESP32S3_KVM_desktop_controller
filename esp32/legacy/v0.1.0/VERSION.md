# 当前版本

- 版本：`v0.1.0`
- 日期：2026-09-07
- 状态：当前开发版
- ESP-IDF：v6.1
- 目标芯片：ESP32-S3-N16R8

## 本版内容

- 建立标准 ESP-IDF 工程。
- 建立 Mac/Windows 统一状态模型。
- 加入芯片、Flash、PSRAM 和复位原因日志。
- 加入模拟 Mac/Windows 状态数据。
- 配置 16MB Flash 和 8MB Octal PSRAM。

## 编译产物

完整构建目录保存在 `build/`。主要可交付文件包括：

- `build/kvm_controller.bin`
- `build/kvm_controller.elf`
- `build/kvm_controller.map`
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`

