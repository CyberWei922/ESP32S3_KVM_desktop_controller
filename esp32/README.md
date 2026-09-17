# ESP32固件目录

## 当前入口

- `latest/`：当前 `v0.5.0-dev.22` 的唯一开发入口，包含正式开发源码、协议实现、LVGL UI、测试和开发文档。
- `legacy/v0.4.0-dev.3/`：LVGL重构前的最后一版验证固件，禁止直接修改。
- `validation/`：从冻结版本派生的临时单项验证工程，不属于正式版本。
- `kvm_config.local.h`：本机Wi-Fi配置备份，被根`.gitignore`的`*.local.h`规则忽略；创建新版工程时按新版模板复制，不得归档或分发。

`latest/`中的`build/`、`managed_components/`和本机生成的`sdkconfig`属于开发环境产物，不应提交到公开仓库。应提交`sdkconfig.defaults`和`dependencies.lock`，让其他开发者能够重新生成相同的构建环境。

## 版本规则

1. 新版本只在`latest/`中开发。
2. 开始下一版本前，把当前`latest/`完整冻结到`legacy/<version>/`。
3. 历史目录不覆盖、不压缩、不继续开发。
4. 私密配置和包含凭据的二进制不得进入`legacy`。
5. 新版不得直接复制整个旧工程；只按架构边界移植已经验证的底层模块。

## 新版文档

- `latest/README.md`
- `latest/VERSION.md`
- `latest/docs/architecture.md`
- `latest/docs/hardware_interfaces.md`
- `latest/docs/ui_spec.md`
- `latest/docs/development_plan.md`

当前协议权威定义在`../StatsForKVM/PROTOCOL.md`。历史目录中的文档仅用于解释对应历史版本；Windows 客户端源码目前维护在独立 Windows 主机，后续会同步到本仓库。
