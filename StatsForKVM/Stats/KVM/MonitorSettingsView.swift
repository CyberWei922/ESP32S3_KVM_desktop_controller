//
//  MonitorSettingsView.swift
//  StatsForKVM
//

import Cocoa
import Kit

final class MonitorSettingsView: NSStackView, NSTextFieldDelegate {
    private let service = MonitorService.shared
    private let discovery = MonitorDiscoveryService.shared

    private var enabledSwitch: NSSwitch!
    private var hostField: NSTextField!
    private var portField: NSTextField!
    private var pathField: NSTextField!
    private var preferWiFiSwitch: NSSwitch!
    private var connectionValue: NSTextField!
    private var discoveryValue: NSTextField!
    private var localNetworkPermissionValue: NSTextField!
    private var localNetworkPermissionButton: NSButton!
    private var connectionDetailValue: NSTextField!
    private var routeWarningValue: NSTextField!
    private var lastSentValue: NSTextField!
    private var testResultValue: NSTextField!
    private var temperatureValue: NSTextField!
    private var memoryValue: NSTextField!
    private var jsonValue: NSTextField!
    private var jsonSizeValue: NSTextField!
    private var weatherProviderPopup: NSPopUpButton!
    private var cityCodeField: NSTextField!
    private var cityNameValue: NSTextField!
    private var qweatherHostField: NSTextField!
    private var qweatherKeyField: NSTextField!
    private var amapKeyField: NSTextField!

    override init(frame: NSRect) {
        super.init(frame: frame)
        self.translatesAutoresizingMaskIntoConstraints = false
        self.orientation = .vertical

        let scrollView = ScrollableStackView(orientation: .vertical)
        scrollView.stackView.edgeInsets = NSEdgeInsets(
            top: 0,
            left: Constants.Settings.margin,
            bottom: Constants.Settings.margin,
            right: Constants.Settings.margin
        )
        scrollView.stackView.spacing = Constants.Settings.margin

        self.enabledSwitch = self.switchView(action: #selector(self.toggleEnabled), state: self.service.enabled)
        self.hostField = self.inputField(id: "monitor-host", value: self.service.host, placeholder: "192.168.1.50")
        self.portField = self.inputField(id: "monitor-port", value: "\(self.service.port)", placeholder: "81")
        self.pathField = self.inputField(id: "monitor-path", value: self.service.path, placeholder: "/statsforkvm")
        self.preferWiFiSwitch = self.switchView(action: #selector(self.togglePreferWiFi), state: self.service.preferWiFi)
        self.discoveryValue = self.textView("")
        self.localNetworkPermissionValue = self.textView("")
        self.localNetworkPermissionButton = self.buttonView(
            #selector(self.requestLocalNetworkAccess),
            text: localizedString("Request access")
        )

        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Monitor"), [
            PreferencesRow(localizedString("Enable data transmission"), component: self.enabledSwitch),
            PreferencesRow(localizedString("ESP32 address"), component: self.hostField),
            PreferencesRow(localizedString("Port"), component: self.portField),
            PreferencesRow(localizedString("WebSocket path"), component: self.pathField),
            PreferencesRow(
                localizedString("Prefer physical Wi-Fi"),
                localizedString("Use when a VPN or virtual adapter captures local traffic"),
                component: self.preferWiFiSwitch
            ),
            PreferencesRow(localizedString("Discovered ESP32"), component: self.discoveryValue),
            PreferencesRow(
                localizedString("Local network access"),
                localizedString("Required for direct communication with the ESP32"),
                component: self.localNetworkPermissionValue
            ),
            PreferencesRow(component: self.localNetworkPermissionButton),
            PreferencesRow(component: self.buttonView(#selector(self.testConnection), text: localizedString("Test connection")))
        ]))

        self.connectionValue = self.textView("")
        self.connectionDetailValue = self.textView("")
        self.routeWarningValue = self.textView("")
        self.lastSentValue = self.textView("")
        self.testResultValue = self.textView("")
        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Connection status"), [
            PreferencesRow(localizedString("State"), component: self.connectionValue),
            PreferencesRow(localizedString("Network path"), component: self.connectionDetailValue),
            PreferencesRow(localizedString("Route diagnostic"), component: self.routeWarningValue),
            PreferencesRow(localizedString("Last sent"), component: self.lastSentValue),
            PreferencesRow(localizedString("Connection test"), component: self.testResultValue)
        ]))

        self.weatherProviderPopup = NSPopUpButton()
        DashboardWeatherProvider.allCases.forEach { provider in
            self.weatherProviderPopup.addItem(withTitle: provider.title)
            self.weatherProviderPopup.lastItem?.representedObject = provider.rawValue
        }
        self.weatherProviderPopup.target = self
        self.weatherProviderPopup.action = #selector(self.weatherProviderChanged)
        self.cityCodeField = self.inputField(id: "dashboard-city-code",
                                             value: self.service.dashboardData.cityCode,
                                             placeholder: "101020100 / Shanghai")
        self.cityNameValue = self.textView(self.service.dashboardData.cityName)
        self.qweatherHostField = self.inputField(id: "dashboard-qweather-host",
                                                 value: self.service.dashboardData.qweatherHost,
                                                 placeholder: "your.qweatherapi.com")
        self.qweatherKeyField = NSSecureTextField()
        self.qweatherKeyField.widthAnchor.constraint(equalToConstant: 240).isActive = true
        self.qweatherKeyField.stringValue = self.service.dashboardData.qweatherKey
        self.qweatherKeyField.placeholderString = "X-QW-Api-Key"
        self.qweatherKeyField.delegate = self
        self.amapKeyField = NSSecureTextField()
        self.amapKeyField.widthAnchor.constraint(equalToConstant: 240).isActive = true
        self.amapKeyField.stringValue = self.service.dashboardData.amapKey
        self.amapKeyField.placeholderString = "AMap Web Service Key"
        self.amapKeyField.delegate = self
        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Dashboard data (Mac backup)"), [
            PreferencesRow(localizedString("Weather source"), component: self.weatherProviderPopup),
            PreferencesRow(localizedString("City code or name"),
                           localizedString("The resolved city name is sent to the ESP32"),
                           component: self.cityCodeField),
            PreferencesRow(localizedString("Resolved city"), component: self.cityNameValue),
            PreferencesRow(localizedString("QWeather API Host"), component: self.qweatherHostField),
            PreferencesRow(localizedString("QWeather API Key"), component: self.qweatherKeyField),
            PreferencesRow(localizedString("AMap API Key"), component: self.amapKeyField),
            PreferencesRow(component: self.buttonView(#selector(self.refreshDashboard),
                                                       text: localizedString("Resolve city and refresh")))
        ]))

        self.temperatureValue = self.textView("")
        self.memoryValue = self.textView("")
        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Data sent in version 1"), [
            PreferencesRow("Average CPU", component: self.temperatureValue),
            PreferencesRow(localizedString("Memory usage"), component: self.memoryValue)
        ]))

        self.jsonValue = NSTextField(wrappingLabelWithString: "")
        self.jsonValue.font = NSFont.monospacedSystemFont(ofSize: 10, weight: .regular)
        self.jsonValue.textColor = .secondaryLabelColor
        self.jsonValue.isSelectable = true
        self.jsonValue.maximumNumberOfLines = 8
        self.jsonSizeValue = self.textView("")
        scrollView.stackView.addArrangedSubview(PreferencesSection(title: "JSON", [
            PreferencesRow(localizedString("Message size"), component: self.jsonSizeValue),
            PreferencesRow(component: self.jsonValue)
        ]))

        self.addArrangedSubview(scrollView)

        NotificationCenter.default.addObserver(self, selector: #selector(self.refresh), name: .monitorServiceDidChange, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(self.refresh), name: .kvmTelemetryDidChange, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(self.refresh), name: .monitorDiscoveryDidChange, object: nil)
        self.discovery.start()
        self.refresh()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
        self.discovery.stop()
    }

    func viewWillAppear() {
        self.enabledSwitch.state = self.service.enabled ? .on : .off
        self.hostField.stringValue = self.service.host
        self.portField.stringValue = "\(self.service.port)"
        self.pathField.stringValue = self.service.path
        self.preferWiFiSwitch.state = self.service.preferWiFi ? .on : .off
        self.selectWeatherProvider(self.service.dashboardData.provider)
        self.cityCodeField.stringValue = self.service.dashboardData.cityCode
        self.qweatherHostField.stringValue = self.service.dashboardData.qweatherHost
        self.qweatherKeyField.stringValue = self.service.dashboardData.qweatherKey
        self.amapKeyField.stringValue = self.service.dashboardData.amapKey
        self.refresh()
    }

    @objc private func toggleEnabled(_ sender: NSControl) {
        self.saveConnectionFields()
        self.service.setEnabled(controlState(sender))
    }

    @objc private func togglePreferWiFi(_ sender: NSControl) {
        self.saveConnectionFields(preferWiFi: controlState(sender))
    }

    @objc private func testConnection() {
        self.saveConnectionFields()
        self.service.testConnection()
    }

    @objc private func requestLocalNetworkAccess() {
        self.saveConnectionFields()
        // Restart Bonjour as well as the direct ESP32 connection. Bonjour is
        // the operation macOS uses to present/re-evaluate local-network access.
        self.discovery.stop()
        self.discovery.start()
        self.service.requestLocalNetworkAccess()
    }

    func controlTextDidEndEditing(_ notification: Notification) {
        self.saveAllFields()
    }

    @objc private func weatherProviderChanged() { self.saveDashboardFields() }

    @objc private func refreshDashboard() { self.saveDashboardFields() }

    private func saveAllFields() {
        self.saveConnectionFields()
        self.saveDashboardFields()
    }

    private func saveDashboardFields() {
        let raw = self.weatherProviderPopup.selectedItem?.representedObject as? String ?? "open_meteo"
        let provider = DashboardWeatherProvider(rawValue: raw) ?? .openMeteo
        self.service.updateDashboard(provider: provider,
                                     cityCode: self.cityCodeField.stringValue,
                                     qweatherHost: self.qweatherHostField.stringValue,
                                     qweatherKey: self.qweatherKeyField.stringValue,
                                     amapKey: self.amapKeyField.stringValue)
    }

    private func selectWeatherProvider(_ provider: DashboardWeatherProvider) {
        if let index = self.weatherProviderPopup.itemArray.firstIndex(where: {
            ($0.representedObject as? String) == provider.rawValue
        }) { self.weatherProviderPopup.selectItem(at: index) }
    }

    private func saveConnectionFields(preferWiFi: Bool? = nil) {
        self.service.update(
            host: self.hostField.stringValue,
            port: Int(self.portField.stringValue) ?? self.service.port,
            path: self.pathField.stringValue,
            preferWiFi: preferWiFi ?? (self.preferWiFiSwitch.state == .on)
        )
    }

    @objc private func refresh() {
        DispatchQueue.main.async {
            let snapshot = KVMTelemetryStore.shared.snapshot()
            self.connectionValue.stringValue = self.connectionTitle(self.service.connectionState)
            self.localNetworkPermissionValue.stringValue = self.localNetworkPermissionTitle
            self.localNetworkPermissionButton.title = localizedString("Request access")
            if let error = self.discovery.error {
                self.discoveryValue.stringValue = error
            } else if self.discovery.services.isEmpty {
                self.discoveryValue.stringValue = self.localizedNoBonjourService
            } else {
                self.discoveryValue.stringValue = self.discovery.services.map(\.displayName).joined(separator: ", ")
            }
            self.connectionDetailValue.stringValue = self.connectionDetail(
                self.service.connectionState.detail ?? self.service.lastError
            )
            self.routeWarningValue.stringValue = self.routeWarningText(self.service.routeWarning)
            if let date = self.service.lastSentAt {
                self.lastSentValue.stringValue = DateFormatter.localizedString(from: date, dateStyle: .none, timeStyle: .medium)
            } else {
                self.lastSentValue.stringValue = "—"
            }
            self.testResultValue.stringValue = self.service.lastTestResult ?? "—"
            self.cityNameValue.stringValue = self.service.dashboardData.cityName
            self.temperatureValue.stringValue = self.metricText(snapshot.averageCPUTemperature, decimals: 1)
            self.memoryValue.stringValue = self.metricText(snapshot.memoryUsage, decimals: 1)
            let json = self.service.previewJSON()
            self.jsonValue.stringValue = json
            self.jsonSizeValue.stringValue = localizedString("%0 bytes at one message per second", "\(json.utf8.count)")
        }
    }

    private func connectionTitle(_ state: MonitorConnectionState) -> String {
        switch state {
        case .disabled: return localizedString("Disabled")
        case .connecting: return localizedString("Connecting")
        case .connected: return localizedString("Connected")
        case .waiting: return localizedString("Waiting to reconnect")
        case .failed: return localizedString("Connection failed")
        }
    }

    private func connectionDetail(_ detail: String?) -> String {
        guard let detail else { return "—" }
        switch detail {
        case "vpn_or_virtual_route_may_block_local_network":
            return localizedString("VPN or a virtual route may block LAN access; allow local network traffic in the VPN")
        case "local_network_unreachable":
            return localizedString("Local network is unreachable")
        case "esp32_unreachable":
            return localizedString("ESP32 is unreachable; verify its address and LAN route")
        case "connection_refused":
            return localizedString("ESP32 refused the connection; verify its WebSocket port")
        case "connection_timed_out":
            return localizedString("Connection timed out; VPN or subnet overlap may be blocking the route")
        case "address_resolution_failed":
            return localizedString("Address resolution failed")
        case "local_network_permission_denied":
            return localizedString("Local network access is denied; allow StatsForKVM in System Settings")
        default:
            return detail
        }
    }

    private var localNetworkPermissionTitle: String {
        if self.service.localNetworkPermissionDenied {
            return localizedString("Denied")
        }
        if case .connected = self.service.connectionState {
            return localizedString("Allowed")
        }
        return localizedString("Not confirmed")
    }

    private func routeWarningText(_ warning: String?) -> String {
        guard let warning else { return localizedString("No obvious subnet conflict detected") }
        if warning.hasPrefix("vpn_subnet_overlap_detected:") {
            let interfaces = warning.split(separator: ":", maxSplits: 1).last.map(String.init) ?? ""
            return localizedString("VPN and LAN subnets overlap on %0", interfaces)
        }
        if warning.hasPrefix("destination_routes_only_through_virtual_interface:") {
            let interfaces = warning.split(separator: ":", maxSplits: 1).last.map(String.init) ?? ""
            return localizedString("ESP32 address currently matches only virtual interface %0", interfaces)
        }
        if warning == "esp32_not_on_physical_lan_subnet" {
            return localizedString("ESP32 address is not on a detected physical LAN subnet")
        }
        return warning
    }

    private var localizedNoBonjourService: String {
        localizedString("None; saved address remains primary")
    }

    private func metricText(_ metric: KVMTelemetryMetric, decimals: Int) -> String {
        guard metric.valid, let value = metric.value else {
            return localizedString("Unavailable (%0)", metric.error ?? "unknown_error")
        }
        return String(format: "%.*f %@", decimals, value, metric.unit == "percent" ? "%" : "°C")
    }

    private func inputField(id: String, value: String, placeholder: String) -> NSTextField {
        let field = NSTextField()
        field.identifier = NSUserInterfaceItemIdentifier(id)
        field.widthAnchor.constraint(equalToConstant: 240).isActive = true
        field.font = NSFont.systemFont(ofSize: 12)
        field.isEditable = true
        field.isSelectable = true
        field.usesSingleLineMode = true
        field.maximumNumberOfLines = 1
        field.focusRingType = .default
        field.stringValue = value
        field.placeholderString = placeholder
        field.delegate = self
        return field
    }
}
