# StatsForKVM

StatsForKVM 是基于开源 [Stats](https://github.com/exelban/stats) 的个人定制版菜单栏程序。当前分支保留 Stats 原有监控功能，并增加独立的 Monitor、Dashboard 和 KVM 设置能力：

- **监视器（Monitor）**：复用 Stats 的 `Average CPU` 温度和 RAM 占用率，每秒通过局域网 WebSocket 向 ESP32 发送一份最新 JSON 快照。
- **Dashboard**：采集天气、行情和城市信息，每 10 秒发送一份 dashboard 快照；Windows 是主数据源，macOS 是备用数据源。
- **KVM**：集成固定版本的 m1ddc 底层代码，用于发现外接显示器、切换 DDC/CI 输入源，以及响应 ESP32 的显示输出休眠/唤醒命令。

当前版本、与 ESP32 的兼容关系、构建方法和已验证范围见 [`VERSION.md`](VERSION.md)，实际测试证据与待人工验证项目见 [`VERIFICATION.md`](VERIFICATION.md)，线缆协议见 [`PROTOCOL.md`](PROTOCOL.md)，设计和后续路线见 [`KVM_DEVELOPMENT_PLAN.md`](KVM_DEVELOPMENT_PLAN.md)。

本项目当前只用于指定的 MacBook Air M5 和个人 KVM 设备，不是原版 Stats 的官方发行版。StatsForKVM 使用独立名称、Bundle ID、应用数据目录和签名；上游自动更新已关闭，避免被原版 Stats 覆盖。

## 本地构建

用 Xcode 打开 `Stats.xcodeproj`，选择 `Stats` Scheme 和 `My Mac` 后即可构建、运行和调试。也可以在终端运行：

```bash
xcodebuild \
  -project Stats.xcodeproj \
  -scheme Stats \
  -configuration Debug \
  -derivedDataPath /tmp/statsforkvm-build \
  build
```

构建产物位于：

```text
/tmp/statsforkvm-build/Build/Products/Debug/StatsForKVM.app
```

本机已验证的签名 Release 副本位于本地 `build/Release/StatsForKVM.app`；该构建产物不属于公开仓库。

以下内容为 Stats 上游 README，保留用于功能说明、来源和许可证归属。

---

# Stats（上游说明）

<a href="https://github.com/exelban/stats/releases"><p align="center"><img src="https://github.com/exelban/stats/raw/master/Stats/Supporting%20Files/Assets.xcassets/AppIcon.appiconset/icon_256x256.png" width="120"></p></a>

[![Stats](https://serhiy.s3.eu-central-1.amazonaws.com/Github_repo/stats/menus%3Fv2.3.2.png?v1)](https://github.com/exelban/stats/releases)
[![Stats](https://serhiy.s3.eu-central-1.amazonaws.com/Github_repo/stats/popups%3Fv2.3.2.png?v3)](https://github.com/exelban/stats/releases)

macOS system monitor in your menu bar

## Installation
### Manual
You can download the latest version [here](https://github.com/exelban/stats/releases/latest/download/Stats.dmg).
This will download a file called `Stats.dmg`. Open it and move the app to the application folder.

### Homebrew
To install it using Homebrew, open the Terminal app and type:
```bash
brew install stats
```

### Uninstall
Run the uninstall script bundled with the app (requires administrator privileges to remove the SMC helper):
```bash
sh /Applications/Stats.app/Contents/Resources/Scripts/uninstall.sh
```
The script quits Stats and removes:

   - the SMC helper (`/Library/LaunchDaemons/eu.exelban.Stats.SMC.Helper.plist` and `/Library/PrivilegedHelperTools/eu.exelban.Stats.SMC.Helper`)
   - `Stats.app`
   - application data and preferences (`~/Library/Application Support/Stats`, widget containers, and `eu.exelban.Stats` defaults)

If the app has already been moved to the Trash, the script can be run directly from the repository:
```bash
curl -fsSL https://raw.githubusercontent.com/exelban/stats/master/Kit/scripts/uninstall.sh | sh
```

### Legacy version
Legacy version for older systems could be found [here](https://mac-stats.com/downloads).

## Requirements
Stats is supported on macOS 12 (Monterey) and newer.
Beta versions of macOS are not supported - only stable releases.

## Features
Stats is an application that allows you to monitor your macOS system.

 - CPU utilization
 - GPU utilization
 - Memory usage
 - Disk utilization
 - Network usage
 - Battery level
 - Fan's control (not maintained)
 - Sensors information (Temperature/Voltage/Power)
 - Bluetooth devices
 - Multiple time zone clock

## FAQs

### How do you change the order of the menu bar icons?
macOS decides the order of the menu bar items not `Stats` - it may change after the first reboot after installing Stats.

To change the order of any menu bar icon - macOS Mojave (version 10.14) and up.

1. Hold down ⌘ (command key).
2. Drag the icon to the desired position on the menu bar.
3. Release ⌘ (command key)

### Stats icons do not appear in the menu bar
macOS 26 introduced a new privacy control under System Settings → Menu Bar. Apps must be explicitly allowed there to display menu bar items. If Stats is running with at least one module active and one widget enabled, but none of its icons show up in the menu bar, this is almost certainly the cause. More details you can find [here](https://github.com/exelban/stats/issues/3120).

**Solution:** open **System Settings → Menu Bar** and toggle **Stats** ON.

### Desktop widgets not showing the data
Due to a problem with high data load in the system process (`chronod`) responsible for communication between the app and widgets, communication is disabled by default on the Stats side. To enable it, the `macOS widgets` option must be enabled in the Stats settings. More details you can find [here](https://github.com/exelban/stats/issues/2733).

**Solution:** open **Stats Settings** and toggle **macOS widgets** ON.

### How to reduce energy impact or CPU usage of Stats?
Stats tries to be efficient as it's possible. But reading some data periodically is not a cheap task. Each module has its own "price". So, if you want to reduce energy impact from the Stats you need to disable some Stats modules. The most inefficient modules are Sensors and Bluetooth. Disabling these modules could reduce CPU usage and power efficiency by up to 50% in some cases.

### Fan control
Fan control is in legacy mode. It does not receive any updates or fixes. It's not dropped from the app just because in the old Macs it works pretty acceptable. I'm open to accepting fixed or improvements (via PR) for this feature in case someone would like to help with that. But have no option and time to provide support for this feature.

### Sensors show incorrect CPU/GPU core count
CPU/GPU sensors are simply thermal zones (sensors) on the CPU/GPU. They have no relation to the number of cores or specific cores.
For example, a CPU is typically divided into two clusters: efficiency and performance. Each cluster contains multiple temperature sensors, and Stats simply displays these sensors. However, "CPU Efficient Core 1" does not represent the temperature of a single efficient core—it only indicates one of the temperature sensors within the efficiency core cluster.
Additionally, with each new SoC, Apple changes the sensor keys. As a result, it takes time to determine which SMC values correspond to the appropriate sensors. If anyone knows how to accurately match the sensors for Apple Silicon, please contact me.

### App crash – what to do?
First, ensure that you are using the latest version of Stats. There is a high chance that a fix preventing the crash has already been released. If you are already running the latest version, check the open issues. Only if none of the existing issues address your problem should you open a new issue.

### Why my issue was closed without any response?
Most probably because it's a duplicated issue and there is an answer to the question, report, or proposition. Please use a search by closed issues to get an answer.
So, if your issue was closed without any response, most probably it already has a response.

### External API
Stats does not collect any telemetry or analytics. The only external requests it makes are to the following APIs:

- https://api.mac-stats.com – For update checks and retrieving the public IP address
- https://api.github.com – Fallback for update checks

Both of these APIs are used to check for updates. Additionally, an external request is required to obtain the public IP address. I do not want to use any third-party providers for retrieving the public IP address, so I use my own server for this purpose.

If you have concerns about these requests, you have a few options:

- propose a PR that allows these features to work without an external server
- block both of these servers using any network filtering app (if you're reading this, you're likely using something like Little Snitch, so you can easily do this). In this case do not expect to receive any updates or see your public IP in the network module.

### How to contribute to the project?
If you want to develop a new feature, or you've found something that doesn't work, the first step is to open an issue so the feature or problem can be discussed. Pull requests should only be opened for existing issues and after discussion; otherwise, they may be closed automatically. There are a few cases where this can be skipped: language changes, and contributors who have already made significant contributions and whose implementations align well with the project.

## Open source, but not open contribution
Stats is an open-source project: the full source code is available under the MIT license, and you are free to read it, learn from it, fork it, and build your own version of the app.

However, it is not an open-contribution project. Stats is developed and maintained by a single person, and keeping the project stable and coherent takes priority over accepting every proposed change. Reviewing external code, testing it across different Macs and macOS versions, and maintaining it afterward often takes more time than writing it in the first place.

For that reason, unsolicited pull requests are generally not accepted and may be closed without review. If you want to change or add something, please open an issue first so it can be discussed. The exceptions are translations and language fixes, which are always welcome, and contributions from people who have already made significant contributions to the project.

The best ways to support the project are reporting bugs, improving translations, and proposing ideas through issues.

## Supported languages
- English
- Polski
- Українська
- Русский
- 中文 (简体) (thanks to [chenguokai](https://github.com/chenguokai), [Tai-Zhou](https://github.com/Tai-Zhou), and [Jerry](https://github.com/Jerry23011))
- Türkçe (thanks to [yusufozgul](https://github.com/yusufozgul) and [setanarut](https://github.com/setanarut))
- 한국어 (thanks to [escapeanaemia](https://github.com/escapeanaemia) and [iamhslee](https://github.com/iamhslee))
- German (thanks to [natterstefan](https://github.com/natterstefan) and [aneitel](https://github.com/aneitel))
- 中文 (繁體) (thanks to [iamch15542](https://github.com/iamch15542) and [jrthsr700tmax](https://github.com/jrthsr700tmax))
- Spanish (thanks to [jcconca](https://github.com/jcconca))
- Vietnamese (thanks to [HXD.VN](https://github.com/xuandung38))
- French (thanks to [RomainLt](https://github.com/RomainLt))
- Italian (thanks to [gmcinalli](https://github.com/gmcinalli))
- Portuguese (Brazil) (thanks to [marcelochaves95](https://github.com/marcelochaves95) and [pedroserigatto](https://github.com/pedroserigatto))
- Norwegian Bokmål (thanks to [rubjo](https://github.com/rubjo))
- 日本語 (thanks to [treastrain](https://github.com/treastrain))
- Portuguese (Portugal) (thanks to [AdamModus](https://github.com/AdamModus))
- Czech (thanks to [mpl75](https://github.com/mpl75))
- Magyar (thanks to [moriczr](https://github.com/moriczr))
- Bulgarian (thanks to [zbrox](https://github.com/zbrox))
- Romanian (thanks to [razluta](https://github.com/razluta))
- Dutch (thanks to [ngohungphuc](https://github.com/ngohungphuc))
- Hrvatski (thanks to [milotype](https://github.com/milotype))
- Danish (thanks to [casperes1996](https://github.com/casperes1996) and [aleksanderbl29](https://github.com/aleksanderbl29))
- Catalan (thanks to [davidalonso](https://github.com/davidalonso))
- Indonesian (thanks to [yooody](https://github.com/yooody))
- Hebrew (thanks to [BadSugar](https://github.com/BadSugar))
- Slovenian (thanks to [zigapovhe](https://github.com/zigapovhe))
- Greek (thanks to [sudoxcess](https://github.com/sudoxcess) and [vaionicle](https://github.com/vaionicle))
- Persian (thanks to [ShawnAlisson](https://github.com/ShawnAlisson))
- Slovenský (thanks to [martinbernat](https://github.com/martinbernat))
- Thai (thanks to [apiphoomchu](https://github.com/apiphoomchu))
- Estonian (thanks to [postylem](https://github.com/postylem))
- Hindi (thanks to [patiljignesh](https://github.com/patiljignesh))
- Finnish (thanks to [eightscrow](https://github.com/eightscrow))
- Bengali (thanks to [adnan29979](https://github.com/adnan29979))
- Tamil (thanks to [sabapathy7](https://github.com/sabapathy7))

You can help by adding a new language or improving the existing translation.

## License
[MIT License](https://github.com/exelban/stats/blob/master/LICENSE)
