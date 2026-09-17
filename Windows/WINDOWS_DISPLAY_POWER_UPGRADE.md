# KVMBridgeWindows 显示待机联动升级指南

文档日期：2026-09-15  
目标版本：在已经完整实现现有 Windows v1 文档的代码基础上升级  
协同端：ESP32 `v0.5.0-dev.22`、StatsForKVM macOS `v0.1.0-dev`（代码已同步，等待 Windows 实现和三端实机验收）

## 1. 升级目标

本次不重写上一版功能。必须保留托盘程序、WebSocket、每秒遥测、DDC输入切换、配置、日志、开机启动、命令去重及所有既有测试，只新增以下能力：

1. ESP32左键长按关闭自身背光时，Windows停止显示输出，但台式机、托盘客户端、网络和后台任务继续运行。
2. Mac与Windows均停止输出后，X41Q因没有有效视频源自然进入闪灯待机。
3. ESP32需要Windows画面时，Windows客户端自动恢复视频输出。
4. Windows黑屏期间仍连接ESP32并每秒上报遥测。
5. KVM切换到显示输出已关闭的Windows前，ESP32可先唤醒Windows输出，再继续DDC和USB流程。

不得使用DDC `0xD6`作为主要关屏手段。实测表明X41Q收到`0xD6=4`后，如果另一输入仍有信号，会自动跳到另一输入；只有所有主机停止视频输出后才会稳定进入待机。

## 2. 开发前必须读取

按顺序完整阅读本目录：

1. `README.md`
2. `PROJECT_SPEC.md`
3. `PROTOCOL_V1.md`
4. `WINDOWS_IMPLEMENTATION.md`
5. `DDC_AND_METRICS.md`
6. `TEST_PLAN.md`
7. 本文件 `WINDOWS_DISPLAY_POWER_UPGRADE.md`

上一版文档继续约束所有旧功能；发生冲突时，本文件只覆盖“显示输出休眠/唤醒与协议协商”相关内容。

## 3. 兼容与能力协商

不得假设ESP32、Mac和Windows总会同时升级。连接仍兼容protocol v1；新版Windows在`hello`中增加可选能力列表：

```json
{
  "type": "hello",
  "protocol_version": 1,
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "platform": "windows",
  "app": "KVMBridgeWindows",
  "capabilities": [
    "switch_display",
    "display_sleep",
    "display_wake"
  ]
}
```

采用兼容扩展而不改变现有必填字段：

- 旧ESP32会忽略未知`capabilities`字段，原有功能继续工作。
- 新ESP32只有看到对应客户端明确声明能力后，才能发送新命令。
- 未声明能力时，ESP32不得尝试显示休眠联动，并应提示客户端需要升级。
- `protocol_version`本次保持为`1`，避免三端安装顺序造成旧遥测和DDC切换全部失效。
- 未知命令仍不得执行；客户端应记录`unsupported_action`并在报文身份完整时返回失败结果。

## 4. 新命令

### 4.1 停止显示输出

```json
{
  "type": "command",
  "protocol_version": 1,
  "request_id": "esp32-sleep-00000001",
  "target_device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "action": "display_sleep"
}
```

### 4.2 恢复显示输出

```json
{
  "type": "command",
  "protocol_version": 1,
  "request_id": "esp32-wake-00000002",
  "target_device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "action": "display_wake"
}
```

新命令不携带`target`、`requested_input`或任意VCP值。`target_device_id`仍必须与本机持久设备ID完全一致。

## 5. 新命令结果

成功停止显示输出：

```json
{
  "type": "command_result",
  "protocol_version": 1,
  "request_id": "esp32-sleep-00000001",
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "action": "display_sleep",
  "success": true,
  "display_power_state": "sleep_requested",
  "error": null
}
```

成功恢复输出：

```json
{
  "type": "command_result",
  "protocol_version": 1,
  "request_id": "esp32-wake-00000002",
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "action": "display_wake",
  "success": true,
  "display_power_state": "wake_requested",
  "error": null
}
```

失败结果：

```json
{
  "type": "command_result",
  "protocol_version": 1,
  "request_id": "esp32-wake-00000002",
  "device_id": "win-23e22c7f-ef2a-4e82-a9a2-d46d56918c60",
  "action": "display_wake",
  "success": false,
  "display_power_state": "unchanged",
  "error": "display_wake_failed"
}
```

约束：

- `action`必须原样对应请求。
- `display_power_state`只允许`sleep_requested`、`wake_requested`、`unchanged`。
- `success=true`时`error=null`；失败时必须给稳定错误码。
- 成功仅表示Windows系统调用已成功提交，不声称物理X41Q已经完成待机或亮屏。
- 旧`switch_display`结果结构完全保持原样，不要给它强行添加或删除字段。

新增错误码：

- `display_power_disabled`
- `display_sleep_failed`
- `display_wake_failed`
- `display_power_timeout`
- `unsupported_action`
- `system_sleep_not_prevented`

## 6. DisplayPowerService

新增独立`DisplayPowerService`，不得把P/Invoke写进托盘菜单、WebSocket接收循环或DDC服务。

### display_sleep

在普通用户会话中使用带超时的窗口消息调用：

```text
HWND_BROADCAST
WM_SYSCOMMAND = 0x0112
SC_MONITORPOWER = 0xF170
lParam = 2
```

要求：

- 优先`SendMessageTimeout`，不能让异常窗口无限阻塞命令线程。
- 不调用系统睡眠、休眠、关机或注销接口。
- 不停止显卡、禁用显示设备、修改分辨率或模拟拔线。
- 系统调用提交后立即返回结果；不能因屏幕已经黑掉而等待不可获得的视觉确认。

### display_wake

组合使用：

```text
SC_MONITORPOWER，lParam = -1
SetThreadExecutionState(ES_DISPLAY_REQUIRED)
```

- 先请求显示器开启，再重置显示空闲计时器。
- 不模拟用户密码、键盘内容或鼠标点击。
- 不自动切换X41Q输入源；DDC输入选择仍由既有`switch_display`流程负责。
- 必须在目标Windows真机和实际显卡驱动上验证恢复DP输出，不能以API返回值替代真机结论。

## 7. 防止Windows整机睡眠

显示输出关闭后，Windows托盘客户端必须继续收发WebSocket。实现运行时电源断言：

- 当连接功能与显示电源联动均启用时，调用`SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)`，只防止系统闲置睡眠，不包含`ES_DISPLAY_REQUIRED`。
- 关屏状态绝对不能持续持有`ES_DISPLAY_REQUIRED`，否则会把刚关闭的显示器再次点亮。
- 关闭功能或退出程序时调用`SetThreadExecutionState(ES_CONTINUOUS)`释放断言。
- 如果断言失败，记录并上报`system_sleep_not_prevented`，托盘状态显示警告。
- 不永久修改用户电源计划，不要求管理员权限。

## 8. 状态、并发与幂等

建议状态：

```text
Active
SleepRequested
OutputSleeping
WakeRequested
Error
```

- 所有`switch_display`、`display_sleep`和`display_wake`进入同一个命令调度器串行执行。
- 每个命令设置合理超时；显示电源命令不等待物理显示器确认。
- 缓存最近64个`request_id`及完整结果。重复ID只重发缓存结果，不能再次关闭或唤醒。
- 收到`sleep`后仍必须继续遥测和接收命令。
- WebSocket断线不得自动恢复显示输出，也不得自动关闭输出。
- 程序启动、系统登录、重连和配置保存不得自行执行显示电源动作。
- `display_wake`在已经Active时应幂等成功；`display_sleep`在已经OutputSleeping时也应幂等成功。

## 9. 托盘菜单调整

保持无主窗口设计。新增或补充：

- `显示电源联动`复选项，默认关闭，用户启用后保存。
- 状态文字：`显示输出：活动 / 已请求休眠 / 已请求唤醒 / 错误`。
- `测试关闭显示输出`与`测试恢复显示输出`，仅用于本机验证。
- `保持主机在线`状态，展示运行时系统睡眠断言是否生效。

手动测试项必须明确标注只影响显示输出，不会关闭Windows。测试关闭后应提供键鼠和托盘外的恢复说明。

## 10. 配置升级

旧配置必须无损迁移。新增字段可以采用：

```json
{
  "displayPower": {
    "enabled": false,
    "preventSystemSleepWhileEnabled": true,
    "commandTimeoutMilliseconds": 3000
  }
}
```

- 缺少新字段时使用上述安全默认值。
- 不改变旧连接、KVM、显示器ID、输入值和遥测配置。
- 配置仍需原子写入和损坏回退。

## 11. 日志

至少记录：

- 收到的动作、request_id、去重状态和耗时。
- Windows显示电源API调用及Win32错误码。
- 运行时防睡眠断言的建立与释放。
- 休眠输出期间WebSocket是否持续在线。
- 不记录Wi-Fi密码、令牌或无关隐私数据。

## 12. 自动测试

在既有测试上新增：

1. 新Hello包含三项能力且旧必填字段不变。
2. 新命令的严格解析、目标设备校验和未知动作拒绝。
3. sleep/wake成功与失败结果JSON完全匹配合同。
4. `SendMessageTimeout`和`SetThreadExecutionState`由可替换平台包装层测试，不在单元测试中真的黑屏。
5. 三类命令共享串行调度，不会并发执行。
6. 重复request_id不重复调用平台API。
7. 黑屏状态继续生成并发送遥测。
8. 旧配置迁移后新功能默认关闭。
9. 退出时释放`ES_SYSTEM_REQUIRED`断言。

## 13. Windows真机验收

1. 确认“接通电源后使设备进入睡眠”为“从不”，或确认运行时断言生效。
2. 执行测试休眠后，X41Q的DP信号消失，但Windows电脑、程序、下载及网络继续工作。
3. ESP32仍每秒收到Windows遥测。
4. 从ESP32发送`display_wake`后，Windows恢复DP输出。
5. Mac输出也停止时，X41Q进入闪灯待机。
6. 唤醒Windows作为唯一输出时，X41Q自动亮起并显示Windows。
7. 连续完成Windows关闭/恢复20次。
8. 断网、重复命令、应用重启和API失败均不造成循环黑屏或整机睡眠。

## 14. 交给Windows开发Agent的任务

在Windows电脑上把本文件和整个`Windows`目录交给Agent，并使用以下要求：

> 当前代码已经完整实现本目录上一版文档。不要重写项目，只做显示电源联动升级。完整阅读`WINDOWS_DISPLAY_POWER_UPGRADE.md`及其中列出的旧文档，保留全部既有功能和兼容性。实现能力声明、`display_sleep`、`display_wake`、DisplayPowerService、运行时防整机睡眠断言、托盘状态、配置迁移、日志、幂等和测试。先运行旧测试建立基线，再增量实现；不要修改Mac或ESP32代码。完成后运行全部自动测试和Release发布，并逐项报告尚需用户执行的X41Q真机测试。禁止用DDC 0xD6代替Windows停止视频输出，禁止让Windows整机睡眠，禁止把API调用成功描述为物理显示器已确认。
