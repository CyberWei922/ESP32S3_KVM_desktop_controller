# 当前版本

- 版本：`v0.2.0`
- 日期：2026-09-07
- 状态：当前开发版
- ESP-IDF：v6.1
- 目标芯片：ESP32-S3-N16R8

## 本版内容

- 保留 v0.1.0 的系统状态模型和 N16R8 配置。
- 新增 MD024-QVGA-01-V01 / ILI9341V SPI 驱动。
- 使用厂商提供的专用初始化序列和 SPI Mode 3。
- 新增 240x320 彩条、边框及状态文字测试界面。
- 新增可集中修改的 LCD GPIO 配置和接线说明。

## 编译产物

完整构建目录保存在 `build/`。主要可交付文件包括：

- `build/kvm_controller.bin`
- `build/kvm_controller.elf`
- `build/kvm_controller.map`
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
