# 硬件装配与走线实拍

本文展示当前原型的 ESP32-S3 控制板固定方式和主板走线。照片对应一台已经完成装配的实物样机，实际制作时仍应以 [`esp32/latest/docs/hardware_interfaces.md`](../esp32/latest/docs/hardware_interfaces.md) 中的接口定义为准。

## ESP32-S3 控制板固定

ESP32-S3 开发板固定在打孔实验板上，旁边安装两块继电器模块。显示屏、USB 共享器和其他外部线路从实验板边缘引出。

照片已在项目主页并排展示：[`README.md`](../README.md#成品展示)。

## 主板背面走线

主板背面使用焊接线路连接 ESP32-S3、显示屏、按键和继电器接口。照片中的蓝色线束主要用于 GPIO 信号连接，红色、黑色及其他电源线应按照硬件接口文档单独确认。

照片已在项目主页并排展示：[`README.md`](../README.md#成品展示)。

## 安全提示

- 修改线路前必须断开 USB 和外部电源。
- 继电器负载侧与 ESP32 逻辑电源侧必须保持正确隔离。
- 继电器控制线需要确认上电、复位和异常状态下不会误动作。
- 照片只代表当前原型的实际装配方式，不能替代电路检查和接口规格。

## 相关文档

- [ESP32 硬件接口](../esp32/latest/docs/hardware_interfaces.md)
- [ESP32 实现合同](../esp32/latest/docs/implementation_contract.md)
- [项目需求](../project_requirements.md)
- [当前外壳资料](../cad_enclosure/README.md)
