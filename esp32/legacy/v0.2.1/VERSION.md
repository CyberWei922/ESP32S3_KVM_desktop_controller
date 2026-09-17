# 当前版本

- 版本：`v0.2.1`
- 日期：2026-09-07
- 状态：动态刷新测试版
- ESP-IDF：v6.1
- 目标芯片：ESP32-S3-N16R8

## 本版内容

- 基于已完成实机点屏验证的 v0.2.0。
- CPU 与内存模拟数据每 500 ms 更新一次。
- 数值和两条进度条采用局部刷新。
- `LIVE` 帧计数持续递增，用于观察卡屏和刷新稳定性。
- 串口每 10 帧输出一次当前 CPU、内存和帧编号。
- 当前数据仍是模拟数据，不是 Mac 的真实监控数据。

## 历史版本

静态点屏验证版已完整保存到 `legacy/v0.2.0`。

## 编译产物

完整构建目录保存在 `build/`。主要可交付文件包括：

- `build/kvm_controller.bin`
- `build/kvm_controller.elf`
- `build/kvm_controller.map`
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
