# StatsForKVM 验证记录

验证日期：`2026-09-11`（本文件记录该日期的历史验证；当前协同 ESP32 为 `v0.5.0-dev.22`）

## 已自动验证

### 构建与签名

- Debug 构建：通过。
- Release 构建：通过。
- Xcode：使用本机已安装的 Xcode 版本。
- 主应用名称：`StatsForKVM.app`。
- 主 Bundle ID：`com.wei.statsforkvm`。
- 签名：使用本机 Apple Development 证书完成验证；具体证书身份不属于公开仓库文档。
- `codesign --verify --deep --strict`：通过，主应用、登录项、Widget 和内置框架均有效。
- 最终 Release 副本：`build/Release/StatsForKVM.app`（约 26 MB）；复制后再次通过深度签名校验，并完成启动/退出冒烟测试。

构建仍会显示 Stats 上游已有警告，包括部分模块的隐式 bridging header、WidgetKit import 提示、SystemStats capture 提示以及本机未安装 SwiftLint；它们没有阻止 Debug 或 Release 构建。Xcode 静态分析也已通过；分析器最初指出的两个未使用 m1ddc 包装入口已从精简集成中移除，最终未留下 StatsForKVM 新增的静态分析问题。

### 单元测试

StatsForKVM 新增的 9 项专项测试全部通过，覆盖：

- 温度和内存数值、单位及来源。
- 温度无效时使用 JSON `null`，不使用零值。
- 旧采样自动标记 `data_stale`。
- snake_case 命令字段解码。
- 命令结果中的可空字段。
- WebSocket 路径规范化。
- VPN 网段冲突和虚拟网卡独占路由诊断。

结果包（本机临时路径，不属于公开仓库）：

```text
/tmp/statsforkvm-final-tests/Logs/Test/Test-Stats-2026.09.08_07-24-59-+0800.xcresult
```

原版 Stats 全量测试中的 `testIsNewestVersion_beta` 和 `testIsNewestVersion_malformed` 仍失败；它们属于上游版本比较测试，与本次 Monitor/KVM 代码无关，因此没有把“全量测试通过”标为完成。

### 本机采集与 WebSocket 联调

在 MacBook Air M5 / macOS 27 上用本地 WebSocket 测试服务器运行签名版应用，得到：

- 启动后发送 `hello`，大小 131 bytes。
- 每秒发送一条真实遥测，约 484～494 bytes。
- CPU 温度和内存占用均随本机状态变化。
- 关闭测试服务器后自动进入重连；服务器恢复后重新发送 `hello` 和当时最新快照。
- 恢复连接时序号从断开后的当前值继续，没有补发离线期间历史遥测。
- 模拟一条合法的显示器切换命令；由于 KVM 开关故意保持关闭，应用安全返回 `kvm_disabled`，没有写显示器。

一条实际采集输出：

```json
{"device_id":"mac-6dd11891-d482-4936-b02d-1703039b13a7","metrics":{"cpu.temperature.average":{"error":null,"sampled_at_milliseconds":1788803532171,"source":"Average CPU","unit":"celsius","valid":true,"value":23.869318181818183},"memory.usage":{"error":null,"sampled_at_milliseconds":1788803532695,"source":"Stats RAM_Usage","unit":"percent","valid":true,"value":79.34637069702148}},"platform":"macos","protocol_version":1,"sequence":1,"timestamp_milliseconds":1788803532719,"type":"telemetry"}
```

安全拒绝结果：

```json
{"confirmed":false,"device_id":"mac-6dd11891-d482-4936-b02d-1703039b13a7","error":"kvm_disabled","protocol_version":1,"read_back_input":null,"request_id":"local-test-1","requested_input":15,"success":false,"type":"command_result","write_succeeded":false}
```

当前测试服务器和应用已经停止，Monitor 设置已恢复为关闭，不会在后台继续连接或发送。

### 流量估算

按最大实测 494 bytes、每秒一次计算，应用层遥测约 `3.95 kbit/s`、每天 `42.7 MB`（约 `40.7 MiB`）。加上 TCP/WebSocket/Wi-Fi 开销后仍远低于普通局域网容量。实现最多只保留“一条正在发送 + 一条最新待发”，因此慢网或断线不会产生无限队列。

## 2026-09-11 X41Q 实机 DDC 验证

- 更换 USB-C→HDMI 转接器后，m1ddc 识别到的转接器标识为 `12547`。
- 写入亮度 `100` 后，X41Q 的实际亮度变为 100，确认当前 Mac 视频链路支持 DDC 写入。
- 实测 X41Q 输入值为 DP=`7`、HDMI1=`5`、HDMI2=`6`，不是此前记录的 `15`、`17`、`18`。
- 实际画面已成功切换，Mac→Windows 与 Windows→Mac 的“当前活跃主机发送 DDC 指令”方案全链路可行。
- DDC 读取不可作为确认手段：m1ddc 对亮度、最大值和对比度等返回异常大数值，BetterDisplay 的 DDC 读取显示失败。必须区分工具执行成功、写入成功和用户可见的画面切换确认。

本次结论只覆盖 DDC 写入与实际输入切换；每方向 20 次重复性测试、显示器待机/唤醒以及长期稳定性仍待验证。

## 尚未验证，不得标记完成

- 真实 ESP32 WebSocket 服务器及 Mac/Windows 同时连接。
- VPN 全隧道、分流、系统代理/PAC、虚拟机网卡与局域网网段重叠的实机组合。
- Monitor/KVM 页面在用户实际设置窗口中的视觉和交互检查。
- X41Q 每个方向至少 20 次的重复切换与自动画面确认策略。
- DDC 待机/唤醒行为及是否出现系统权限提示；当前读回已确认异常，不作为成功判据。
- 登录后自动启动。
- 睡眠/唤醒、Wi-Fi 切换、ESP32 重启的端到端恢复。
- 4 小时及更长时间的连续运行。

这些项目需要真实 ESP32、显示器或用户常用网络环境，不能用本机模拟结果替代。
