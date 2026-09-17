# 当前版本

- 版本：`v0.3.0`
- 日期：2026-09-07
- 状态：ESP32 最小可运行系统阶段冻结版
- ESP-IDF：v6.1
- 目标芯片：ESP32-S3-N16R8

## 本版内容

- 根据最终 `apple_retro_hybrid` CAD 将屏幕改为 320x240 横屏。
- 匹配 LCD 逆时针旋转 90 度安装、11 针排针在左、FPC 在右的结构。
- UI 重新设计为左右两栏，适配外壳的横向显示窗口。
- CPU 与内存模拟数据每 500 ms 更新一次。
- 数值和两条进度条采用局部刷新。
- `LIVE` 帧计数持续递增，用于观察卡屏和刷新稳定性。
- 串口每 10 帧输出一次当前 CPU、内存和帧编号。
- 当前数据仍是模拟数据，不是 Mac 的真实监控数据。
- 本版作为 macOS 常驻程序开发期间的 ESP32 稳定基线；暂不继续叠加网络、按键或外围控制。

## 历史版本

- 静态点屏验证版：`legacy/v0.2.0`
- 竖屏动态刷新版：`legacy/v0.2.1`

## 编译产物

完整构建目录保存在 `build/`。主要可交付文件包括：

- `build/kvm_controller.bin`
- `build/kvm_controller.elf`
- `build/kvm_controller.map`
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
