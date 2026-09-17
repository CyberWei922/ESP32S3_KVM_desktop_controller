# 给下一位 AI 的 ESP32 / StatsForKVM 对接开发提示词

下面正文可以直接作为另一个 Codex/AI 对话的首条任务。它同时规定了范围、现状、实现顺序和验收边界。

---

## 任务开始

继续开发我的 KVM 桌面控制器 ESP32 固件，让当前 ESP32-S3 固件适配已经完成的 macOS `StatsForKVM` WebSocket protocol v1。

项目根目录：

```text
/Users/wei/Projects/KVM
```

ESP32 当前开发入口：

```text
/Users/wei/Projects/KVM/esp32/latest
```

macOS 协议和实现依据：

```text
/Users/wei/Projects/KVM/StatsForKVM/PROTOCOL.md
/Users/wei/Projects/KVM/StatsForKVM/VERSION.md
/Users/wei/Projects/KVM/StatsForKVM/VERIFICATION.md
```

### 一、开始前必须完整阅读

在修改任何文件之前，完整阅读：

1. `/Users/wei/Projects/KVM/README.md`
2. `/Users/wei/Projects/KVM/development_roadmap.md`
3. `/Users/wei/Projects/KVM/project_requirements.md`
4. `/Users/wei/Projects/KVM/esp32/README.md`
5. `/Users/wei/Projects/KVM/esp32/latest/VERSION.md`
6. `/Users/wei/Projects/KVM/esp32/latest/docs/lcd_wiring.md`
7. `/Users/wei/Projects/KVM/StatsForKVM/PROTOCOL.md`
8. `/Users/wei/Projects/KVM/StatsForKVM/VERSION.md`
9. `/Users/wei/Projects/KVM/StatsForKVM/VERIFICATION.md`

然后检查 `esp32/latest/main/`、`sdkconfig.defaults`、当前构建产物和工作区状态。保留用户已有内容，不覆盖无关改动。

若文档与实际代码不一致，以以下优先级处理：

1. `StatsForKVM/PROTOCOL.md` 中已经实现并实测的线缆字段。
2. `esp32/latest` 当前实机验证过的 LCD 引脚、初始化序列和方向。
3. 最新需求与路线图。

不要根据旧讨论自行改字段名。

### 二、当前已确认基线

- 当前 ESP32 冻结版本为 `v0.3.0`。
- 使用 ESP-IDF `v6.1`，芯片为 ESP32-S3-N16R8。
- 16 MB Flash 和 8 MB Octal PSRAM 已实机确认。
- MD024-QVGA-01-V01 / ILI9341V 已实机点亮。
- 屏幕供电为 3.3V，`BLK` 必须悬空。
- 当前 LCD 接线：SCLK GPIO12、MOSI GPIO11、MISO GPIO13、CS GPIO10、DC GPIO9、RST GPIO14。
- SPI Mode 3、10 MHz 和厂商初始化序列已经验证，不能擅自替换。
- 显示方向为 320×240 横屏；正面观察时排针在左、FPC 在右；MADCTL 当前为 `0x68`。
- 当前 UI 每 500 ms 局部刷新，`LIVE` 计数用于发现卡屏。
- 当前 CPU/内存仍为模拟百分比；这部分要改成真实主机状态。
- 现有 `app_state_t` 已为 Mac/Windows、在线状态、温度有效性、内存有效性、采样时间和错误信息预留字段。
- 当前固件没有 Wi-Fi、WebSocket、JSON 解析、NVS 初始化、按键或外围控制代码。
- 不开发按键、继电器、散热器、USB 共享器或外壳。
- 不修改 `/Users/wei/Projects/KVM/StatsForKVM` 的 macOS 源码。

### 三、版本保护规则

在改动 `esp32/latest` 之前：

1. 检查 `/Users/wei/Projects/KVM/esp32/legacy/v0.3.0` 是否存在。
2. 如果不存在，先把当前 `esp32/latest` 的完整 v0.3.0 基线安全复制到 `esp32/legacy/v0.3.0`，包括源码、配置、说明和已有构建产物。
3. 验证历史副本完整后再修改 `latest`；不得覆盖或修改任何已有 `legacy` 版本。
4. 新开发版命名为 `v0.4.0-dev`。只有真机联网和显示验证完成后才能冻结为 `v0.4.0`。
5. 不要执行破坏性 Git/文件命令，不要提交或推送，除非用户另行要求。

### 四、本轮目标

在保留现有 LCD 实机基线的前提下，完成一个可构建的 ESP32 WebSocket 服务端开发版：

1. ESP32 以 Wi-Fi Station 模式连接现有局域网。
2. 在端口 `81`、路径 `/statsforkvm` 提供 WebSocket 服务。
3. 同时容纳一个 macOS 客户端和未来一个 Windows 客户端。
4. 严格解析 StatsForKVM 的 `hello` 和 `telemetry`。
5. 将真实 Mac `Average CPU` 温度和内存占用百分比写入线程安全状态模型。
6. 让 LCD 使用状态快照显示真实值、离线、不可用和过期状态，不再自动装载模拟数据。
7. 建立 ESP32 向指定客户端发送 `command`、接收 `command_result` 的底层接口，但本轮不自动触发 DDC 切换。
8. 完成实际构建和自动化检查；不进行需要用户在场的烧录、Wi-Fi、ESP32 或显示器手动验证。

### 五、协议必须严格匹配

权威协议为：

```text
/Users/wei/Projects/KVM/StatsForKVM/PROTOCOL.md
```

不得把字段简化成自定义格式。Mac 连接后发送：

```json
{
  "app": "StatsForKVM",
  "device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "platform": "macos",
  "protocol_version": 1,
  "type": "hello"
}
```

随后约每秒发送：

```json
{
  "device_id": "mac-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "metrics": {
    "cpu.temperature.average": {
      "error": null,
      "sampled_at_milliseconds": 1788803532171,
      "source": "Average CPU",
      "unit": "celsius",
      "valid": true,
      "value": 42.5
    },
    "memory.usage": {
      "error": null,
      "sampled_at_milliseconds": 1788803532695,
      "source": "Stats RAM_Usage",
      "unit": "percent",
      "valid": true,
      "value": 63.2
    }
  },
  "platform": "macos",
  "protocol_version": 1,
  "sequence": 1,
  "timestamp_milliseconds": 1788803532719,
  "type": "telemetry"
}
```

接收规则：

- 只接受 UTF-8 WebSocket 文本帧。
- 单条应用消息最大 `2048 bytes`；在分配或解析 JSON 前先检查长度。
- 必须先收到合法 `hello`，该连接之后才能提交 `telemetry`。
- `protocol_version` 必须等于整数 `1`。
- `device_id` 必须非空；不要硬编码 Mac 当前生成的 UUID。
- `platform` 只接受 `macos` 或为未来预留的 `windows`。
- 固定维护 Mac/Windows 两个主机槽位，不建立无界客户端数组。
- 同一平台重新连接时采用新连接；旧连接随后关闭时不得错误地把新连接标记离线。
- 每个连接记录最近序号；同一连接内拒绝重复或倒退的 `sequence`。新 `hello`/新连接允许序号重新开始。
- CPU 只有在 `valid == true`、`value` 为有限数字且 `0 < value < 110` 时有效。
- 内存只有在 `valid == true`、`value` 为有限数字且 `0 <= value <= 100` 时有效。
- `value: null`、字段缺失、类型错误、NaN/Infinity、越界或 `valid:false` 均不得当作有效数值。
- 无效数据必须在状态中保留明确错误；LCD 显示 `--`、`N/A`、`STALE` 或 `OFFLINE`，绝不能显示 `0C` 冒充温度。
- v1 只发送内存百分比，不发送 used/total；不要根据不存在的字段伪造字节数。
- 远端 epoch 采样时间用于记录，主机离线/过期判断必须使用 ESP32 自身的单调时间，例如 `esp_timer_get_time()`，不能依赖 ESP32 已经完成 NTP 对时。

连接与过期建议：

- WebSocket socket 断开时立即把对应主机标记为离线。
- 连接仍在但 5 秒没有收到新的合法 telemetry 时，将两项数据标记过期，并显示 `STALE`；不要继续显示旧值。
- 收到下一份合法快照后自动恢复。
- Mac 每 15 秒发送 WebSocket ping；服务端必须按 ESP-IDF 官方行为正确处理 ping/pong，不能因为控制帧断开连接。

### 六、建议代码结构

在现有 `main/` 下按职责增加文件，名称可以小幅调整，但不要把所有逻辑塞进 `app_main.c`：

```text
main/
├── app_main.c
├── app_state.c
├── include/app_state.h
├── lcd_driver.c                 # 保留已验证底层行为
├── include/lcd_driver.h
├── lcd_test_ui.c                # 可改名为 display_ui.c
├── include/lcd_test_ui.h
├── network_manager.c
├── include/network_manager.h
├── websocket_server.c
├── include/websocket_server.h
├── protocol.c
├── include/protocol.h
├── kvm_config.example.h
└── kvm_config.local.h           # 本机私密文件，不提交
```

职责要求：

- `network_manager`：NVS、`esp_netif`、事件循环、Wi-Fi Station、断线退避重连、IP 状态。
- `websocket_server`：HTTP/WebSocket 生命周期、socket/客户端槽位、文本帧限长、ping/pong、异步发送。
- `protocol`：cJSON 解码、严格字段校验、hello/telemetry/command_result，以及 command JSON 编码。
- `app_state`：唯一共享状态和线程安全读写 API。
- `display_ui`：只读取一次完整状态快照并绘制，不直接解析 JSON 或访问 socket。
- `app_main`：只负责初始化模块和启动任务。

优先使用 ESP-IDF v6.1 自带的官方组件与本机 SDK 头文件，例如 `esp_wifi`、`esp_event`、`esp_netif`、`nvs_flash`、`esp_http_server`、`json/cJSON`、`esp_timer`。若 API 细节不确定，先核对当前安装的 ESP-IDF v6.1 官方头文件或官方示例，不要凭旧版本记忆猜函数签名。

### 七、状态并发规则

网络回调和 LCD 刷新运行在不同任务，当前全局结构不能继续无锁读写。

- 为 `app_state_t` 建立 mutex/临界区封装。
- 提供整体快照读取函数；LCD 每帧复制一次快照后再绘制。
- JSON 解析成功且整条消息通过校验后，一次性提交状态，不能让 UI 看到半更新结构。
- 网络回调不得直接绘制屏幕。
- LCD 绘制不得等待 socket I/O。
- 错误字符串、设备 ID 和请求 ID全部使用有界数组和显式结尾，禁止无界 `strcpy`/`sprintf`。
- 热路径不建立无界队列；每台主机只保存最新状态。

### 八、Wi-Fi 配置与隐私

- 不得把真实 SSID、密码、Mac 地址、密钥或其他凭据写进仓库和文档。
- 创建可提交的 `kvm_config.example.h`，只包含占位符。
- 实际凭据放入 `kvm_config.local.h` 或根目录已有忽略规则覆盖的同类本地文件。
- 如果本地配置不存在，工程仍应能够编译；运行时保持 LCD 可用并明确记录 `wifi_not_configured`，不能编译失败或加载模拟主机值。
- 不要擅自扫描并写入用户当前 Wi-Fi 密码。
- 固定 IP 由路由器 DHCP 保留完成，ESP32 代码默认使用 DHCP。
- `_statsforkvm._tcp` Bonjour/mDNS 是辅助功能。只有当前 ESP-IDF 环境已有可用官方组件时再加入；缺少组件不得阻塞固定 IP WebSocket 首版，并要在文档中标记待办。

### 九、LCD 改造边界

不得改动以下已经实机验证的部分，除非有明确构建错误且能说明原因：

- GPIO 定义。
- VCC/BLK 接线要求。
- ILI9341V 厂商初始化序列。
- SPI Mode 3、10 MHz。
- 320×240 坐标和 MADCTL `0x68`。
- 轮询 SPI 的基本工作方式。

UI 可在当前布局上做必要调整：

- CPU 字段改成摄氏温度，不再写 `%`。
- Mac 区域至少显示 `Average CPU` 和内存百分比。
- Windows 仍需保留独立区域，未接入时显示 `OFFLINE`。
- 为在线、离线、过期、温度不可用和内存不可用提供明确画面。
- 当前 5×7 字体没有完整符号时，可以补 `-` 等必要字形；不要引入大型 UI 框架重写已工作的驱动。
- 继续采用局部刷新和约 2 FPS，不要在每条网络消息中全屏重绘。
- 保留 `LIVE` 或等价运行指示，方便人工判断是否卡屏。

### 十、KVM 命令通道

本轮只建立安全的底层发送和回执解析，不接按钮、不自动发命令、不在启动时切显示器。

提供类似以下职责的 API：

```text
send_switch_display(host, target)
```

要求：

- 只向已经 hello、平台为 `macos` 且当前在线的目标连接发送。
- `target` 只允许 `mac` 或 `windows`。
- 自动生成唯一 `request_id`，长度不超过 128 bytes。
- `target_device_id` 使用该连接 hello 保存的真实设备 ID。
- 保存有限数量的 pending request，禁止无界增长。
- 解析 `command_result` 时同时记录 `write_succeeded` 和 `confirmed`；不能只看 `success`。
- 超时或断线时将请求明确标记失败/未知。
- 没有外部触发时函数保持未调用，不得为测试而自动切换真实显示器。

### 十一、错误处理和日志

至少区分并限频记录：

- Wi-Fi 未配置、认证失败、断开和重新取得 IP。
- WebSocket 客户端连接、身份确认、替换和断开。
- 消息过长、非文本帧、JSON 语法错误。
- 缺字段、字段类型错误、协议版本不支持、平台不支持。
- 数值无效、序号重复/倒退、未 hello 就发送 telemetry。
- 数据过期。
- command 发送失败、超时、写入成功但未确认、确认成功。

不要每秒打印完整 JSON；正常 telemetry 日志应采样或限频，避免串口和 Flash 压力。

### 十二、构建和自动验证

实现完成后：

1. 使用项目现有 ESP-IDF v6.1 环境构建，不要擅自升级 IDF 或重建整个工具链。
2. 运行 `idf.py build`；若当前 shell 未加载环境，先定位已有 IDF 环境并说明，不要直接重新安装。
3. 对协议解析器加入可以自动运行的测试或至少一组固定输入测试，覆盖：
   - 合法 Mac hello。
   - 合法 telemetry。
   - CPU `value:null`。
   - `valid:false`。
   - 内存越界。
   - 错误 protocol version。
   - 超过 2048 bytes。
   - telemetry 早于 hello。
   - 重复 sequence。
   - 合法与不完整 command_result。
4. 检查固件尺寸、IRAM/DRAM 和剩余 Flash；记录结果。
5. 检查没有把凭据写进源码、sdkconfig、日志或构建产物说明。
6. 不执行烧录、Wi-Fi 连接、显示器命令或其他硬件操作；这些等待用户醒来后手动配合。

构建通过不等于实机通过。不得把下列内容标记完成：

- Wi-Fi 真机连接。
- macOS 本地网络权限弹窗。
- 真实 WebSocket 长连接。
- Mac/Windows 双客户端。
- LCD 真机画面。
- VPN 环境访问。
- DDC 显示器切换。
- 4 小时或更长稳定性。

### 十三、文档和版本交付

代码完成后必须：

- 把 `esp32/latest/VERSION.md` 更新为 `v0.4.0-dev`，逐项区分“代码完成/构建验证”和“待实机验证”。
- 在 `esp32/latest/docs/` 写一份 ESP32 端 protocol v1 实现说明，引用 `StatsForKVM/PROTOCOL.md` 为权威线缆协议。
- 写清 Wi-Fi 本地配置文件的创建方法，但不填真实密码。
- 更新 `/Users/wei/Projects/KVM/development_roadmap.md`，只勾选真正完成和实际验证的项目。
- 展示修改文件清单、构建命令、构建结果、固件大小和测试结果。
- 明确列出需要用户醒来后完成的最短人工测试步骤。

### 十四、完成标准

本轮可以结束的条件：

- v0.3.0 已完整保存到 legacy 且未被修改。
- `esp32/latest` 是结构清晰的 v0.4.0-dev。
- Wi-Fi/WebSocket/JSON/状态快照/UI/命令通道代码基本完成。
- 与 StatsForKVM protocol v1 字段逐项一致。
- 没有真实凭据。
- `idf.py build` 成功。
- 可自动验证的协议边界已验证。
- ESP32 固件和文档没有把未实测项目标记为完成。

工作时主动检查和修复问题，不要只提供方案。遇到必须依赖真实 Wi-Fi、ESP32 串口、显示器或用户选择的事项时，把代码准备好并明确列入人工验证清单，不要编造结果。

## 任务结束

---

## 使用说明

把另一对话的工作目录设为 `/Users/wei/Projects/KVM`，然后让它完整读取本文件并执行“任务开始”到“任务结束”之间的内容。该任务允许修改 ESP32 目录和项目路线图，但明确禁止修改已经完成的 StatsForKVM 源码。
