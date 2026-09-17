//
//  MonitorService.swift
//  StatsForKVM
//

import Foundation
import Kit

extension Notification.Name {
    static let monitorServiceDidChange = Notification.Name("com.wei.statsforkvm.monitor.didChange")
    static let kvmCommandReceived = Notification.Name("com.wei.statsforkvm.kvm.commandReceived")
}

final class MonitorService {
    static let shared = MonitorService()

    private enum Key {
        static let enabled = "statsforkvm_monitor_enabled"
        static let host = "statsforkvm_monitor_host"
        static let port = "statsforkvm_monitor_port"
        static let path = "statsforkvm_monitor_path"
        static let preferWiFi = "statsforkvm_monitor_prefer_wifi"
        static let deviceID = "statsforkvm_monitor_device_id"
    }

    private let webSocket = MonitorWebSocketClient()
    private let queue = DispatchQueue(label: "com.wei.statsforkvm.monitor.service", qos: .utility)
    private var telemetryTimer: DispatchSourceTimer?
    private var dashboardTimer: DispatchSourceTimer?
    private var transientTestTimeout: DispatchWorkItem?
    private var sequence: UInt64 = 0
    private var dashboardSequence: UInt64 = 0
    private var transportConnected = false
    private var transportGeneration: UInt64 = 0
    private var telemetrySendInFlight = false
    private var pendingTelemetry: (data: Data, json: String)?
    private var transientTestActive = false
    private var testRequested = false
    private let displayPowerController = DisplayPowerController()
    let dashboardData = DashboardDataService.shared

    private(set) var connectionState: MonitorConnectionState = .disabled
    private(set) var lastSentAt: Date?
    private(set) var lastError: String?
    private(set) var lastJSON: String = ""
    private(set) var lastTestResult: String?
    private(set) var routeWarning: String?

    var onTelemetryRequirementChange: ((Bool) -> Void)?

    var enabled: Bool {
        Store.shared.bool(key: Key.enabled, defaultValue: false)
    }
    var host: String {
        Store.shared.string(key: Key.host, defaultValue: "192.168.1.50")
    }
    var port: Int {
        Store.shared.int(key: Key.port, defaultValue: 81)
    }
    var path: String {
        Store.shared.string(key: Key.path, defaultValue: "/statsforkvm")
    }
    var preferWiFi: Bool {
        Store.shared.bool(key: Key.preferWiFi, defaultValue: false)
    }
    var localNetworkPermissionDenied: Bool {
        self.lastError == "local_network_permission_denied" ||
        self.connectionState.detail == "local_network_permission_denied"
    }
    var deviceID: String {
        let existing = Store.shared.string(key: Key.deviceID, defaultValue: "")
        if !existing.isEmpty { return existing }
        let generated = "mac-\(UUID().uuidString.lowercased())"
        Store.shared.set(key: Key.deviceID, value: generated)
        return generated
    }

    private init() {
        self.webSocket.onStateChange = { [weak self] state in
            guard let self else { return }
            self.connectionState = state
            if case .connected = state {
                self.lastError = nil
                self.sendHello()
                let sendForTest = self.testRequested
                self.queue.async {
                    self.transportGeneration &+= 1
                    self.transportConnected = true
                    self.telemetrySendInFlight = false
                    self.pendingTelemetry = nil
                    self.sendTelemetryOnQueue(force: sendForTest)
                    self.sendDashboardOnQueue(force: sendForTest)
                }
                if self.testRequested, case .connected(let interface) = state {
                    self.testRequested = false
                    self.lastTestResult = localizedString("Connected via %0", interface)
                    self.transientTestTimeout?.cancel()
                    self.transientTestTimeout = nil
                    if self.transientTestActive {
                        self.finishTransientTest(after: 1)
                    }
                }
            } else if case .failed(let reason) = state {
                self.queue.async { self.markTransportDisconnectedOnQueue() }
                self.lastError = reason
            } else if case .waiting(let reason) = state {
                self.queue.async { self.markTransportDisconnectedOnQueue() }
                self.lastError = reason
            } else if case .disabled = state {
                self.queue.async { self.markTransportDisconnectedOnQueue() }
            }
            self.notifyChange()
        }
        self.webSocket.onMessage = { [weak self] data in
            self?.handleIncoming(data)
        }
    }

    func startIfEnabled() {
        if self.enabled { self.start() }
    }

    func shutdown() {
        self.cancelTransientTest()
        self.stopTelemetryTimer()
        self.stopDashboardTimer()
        self.webSocket.stop()
        self.onTelemetryRequirementChange?(false)
    }

    func setEnabled(_ enabled: Bool) {
        Store.shared.set(key: Key.enabled, value: enabled)
        enabled ? self.start() : self.stop()
    }

    func update(host: String, port: Int, path: String, preferWiFi: Bool) {
        Store.shared.set(key: Key.host, value: host.trimmingCharacters(in: .whitespacesAndNewlines))
        Store.shared.set(key: Key.port, value: min(max(port, 1), 65_535))
        Store.shared.set(key: Key.path, value: path.isEmpty ? "/statsforkvm" : path)
        Store.shared.set(key: Key.preferWiFi, value: preferWiFi)
        self.refreshRouteWarning()
        if self.enabled { self.start() }
        self.notifyChange()
    }

    func updateDashboard(provider: DashboardWeatherProvider, cityCode: String,
                         qweatherHost: String, qweatherKey: String, amapKey: String) {
        self.dashboardData.update(provider: provider, cityCode: cityCode,
                                  qweatherHost: qweatherHost, qweatherKey: qweatherKey,
                                  amapKey: amapKey)
        self.notifyChange()
    }

    func testConnection() {
        self.refreshRouteWarning()
        self.cancelConnectionTest()
        self.testRequested = true
        self.lastTestResult = localizedString("Testing…")
        if self.enabled {
            self.webSocket.reconnect()
        } else {
            self.transientTestActive = true
            self.onTelemetryRequirementChange?(true)
            self.webSocket.start(configuration: self.configuration)
        }
        let timeout = DispatchWorkItem { [weak self] in
            guard let self, self.testRequested else { return }
            self.testRequested = false
            self.lastTestResult = localizedString("Failed: %0", self.lastError ?? "connection_test_timed_out")
            if self.transientTestActive {
                self.finishTransientTest()
            } else {
                self.notifyChange()
            }
        }
        self.transientTestTimeout = timeout
        DispatchQueue.main.asyncAfter(deadline: .now() + 10, execute: timeout)
        self.notifyChange()
    }

    /// macOS has no explicit local-network permission API. Starting the real
    /// ESP32 connection is the supported operation that asks the system to
    /// present its authorization dialog when the decision is still unknown.
    func requestLocalNetworkAccess() {
        self.lastError = nil
        self.connectionState = .connecting
        self.notifyChange()
        self.testConnection()
    }

    func reconnect() {
        guard self.enabled else { return }
        self.webSocket.reconnect()
    }

    func applicationDidWake() {
        guard self.enabled else { return }
        self.webSocket.reconnect()
    }

    func previewJSON() -> String {
        do {
            let currentSequence = self.queue.sync { self.sequence }
            let message = MonitorTelemetryMessage(
                deviceID: self.deviceID,
                sequence: currentSequence,
                snapshot: KVMTelemetryStore.shared.snapshot()
            )
            let data = try MonitorProtocolCodec.encode(message)
            return String(data: data, encoding: .utf8) ?? ""
        } catch {
            return "{\"error\":\"json_encoding_failed\"}"
        }
    }

    func sendCommandResult(requestID: String, result: KVMSwitchResult) {
        let message = KVMCommandResultMessage(
            requestID: requestID,
            deviceID: self.deviceID,
            success: result.writeSucceeded,
            writeSucceeded: result.writeSucceeded,
            confirmed: result.confirmed,
            requestedInput: result.input,
            readBackInput: result.readBackInput,
            error: result.error
        )
        guard let data = try? MonitorProtocolCodec.encode(message) else { return }
        self.webSocket.send(data)
    }

    private func sendDisplayPowerResult(_ result: DisplayPowerCommandResultMessage) {
        guard let data = try? MonitorProtocolCodec.encode(result) else { return }
        self.webSocket.send(data)
    }

    private var configuration: MonitorConnectionConfiguration {
        MonitorConnectionConfiguration(
            host: self.host,
            port: UInt16(clamping: self.port),
            path: self.path,
            preferWiFi: self.preferWiFi
        )
    }

    private func start() {
        self.cancelTransientTest()
        self.refreshRouteWarning()
        self.onTelemetryRequirementChange?(true)
        self.startTelemetryTimer()
        self.startDashboardTimer()
        self.webSocket.start(configuration: self.configuration)
        self.notifyChange()
    }

    private func stop() {
        self.cancelTransientTest()
        self.stopTelemetryTimer()
        self.stopDashboardTimer()
        self.webSocket.stop()
        self.onTelemetryRequirementChange?(false)
        self.notifyChange()
    }

    private func startTelemetryTimer() {
        self.queue.async { [weak self] in
            guard let self else { return }
            self.stopTelemetryTimerOnQueue()
            let timer = DispatchSource.makeTimerSource(queue: self.queue)
            timer.schedule(deadline: .now() + 1, repeating: 1, leeway: .milliseconds(100))
            timer.setEventHandler { [weak self] in self?.sendTelemetryOnQueue() }
            self.telemetryTimer = timer
            timer.resume()
        }
    }

    private func stopTelemetryTimer() {
        self.queue.async { [weak self] in self?.stopTelemetryTimerOnQueue() }
    }

    private func stopTelemetryTimerOnQueue() {
        self.telemetryTimer?.setEventHandler {}
        self.telemetryTimer?.cancel()
        self.telemetryTimer = nil
    }

    private func startDashboardTimer() {
        self.queue.async { [weak self] in
            guard let self else { return }
            self.stopDashboardTimerOnQueue()
            self.dashboardData.refresh(forceWeather: true)
            let timer = DispatchSource.makeTimerSource(queue: self.queue)
            timer.schedule(deadline: .now() + 2, repeating: 10, leeway: .milliseconds(500))
            timer.setEventHandler { [weak self] in
                guard let self else { return }
                self.dashboardData.refresh()
                self.sendDashboardOnQueue()
            }
            self.dashboardTimer = timer
            timer.resume()
        }
    }

    private func stopDashboardTimer() {
        self.queue.async { [weak self] in self?.stopDashboardTimerOnQueue() }
    }

    private func stopDashboardTimerOnQueue() {
        self.dashboardTimer?.setEventHandler {}
        self.dashboardTimer?.cancel()
        self.dashboardTimer = nil
    }

    private func sendHello() {
        let message = MonitorHelloMessage(deviceID: self.deviceID)
        guard let data = try? MonitorProtocolCodec.encode(message) else { return }
        self.webSocket.send(data)
    }

    private func sendTelemetryOnQueue(force: Bool = false) {
        dispatchPrecondition(condition: .onQueue(self.queue))
        guard (self.enabled || force), self.transportConnected else { return }
        self.sequence &+= 1
        let message = MonitorTelemetryMessage(
            deviceID: self.deviceID,
            sequence: self.sequence,
            snapshot: KVMTelemetryStore.shared.snapshot()
        )
        do {
            let data = try MonitorProtocolCodec.encode(message)
            let json = String(data: data, encoding: .utf8) ?? ""
            self.enqueueTelemetryOnQueue(data: data, json: json)
        } catch {
            DispatchQueue.main.async { [weak self] in
                self?.lastError = "json_encoding_failed"
                self?.notifyChange()
            }
        }
    }

    private func sendDashboardOnQueue(force: Bool = false) {
        dispatchPrecondition(condition: .onQueue(self.queue))
        guard (self.enabled || force), self.transportConnected else { return }
        self.dashboardSequence &+= 1
        let message = self.dashboardData.message(deviceID: self.deviceID,
                                                 sequence: self.dashboardSequence)
        guard let data = try? MonitorProtocolCodec.encode(message), data.count <= 2_048 else {
            DispatchQueue.main.async { [weak self] in
                self?.lastError = "dashboard_message_too_large"
                self?.notifyChange()
            }
            return
        }
        self.webSocket.send(data) { [weak self] error in
            guard let error else { return }
            DispatchQueue.main.async {
                self?.lastError = error.localizedDescription
                self?.notifyChange()
            }
        }
    }

    private func enqueueTelemetryOnQueue(data: Data, json: String) {
        dispatchPrecondition(condition: .onQueue(self.queue))
        guard self.transportConnected else { return }
        if self.telemetrySendInFlight {
            // Replace, rather than append, so a slow path can retain at most
            // one latest snapshot behind the in-flight frame.
            self.pendingTelemetry = (data, json)
            return
        }
        self.sendTelemetryFrameOnQueue(data: data, json: json)
    }

    private func sendTelemetryFrameOnQueue(data: Data, json: String) {
        dispatchPrecondition(condition: .onQueue(self.queue))
        self.telemetrySendInFlight = true
        let generation = self.transportGeneration
        self.webSocket.send(data) { [weak self] error in
            guard let self else { return }
            self.queue.async {
                guard generation == self.transportGeneration else { return }
                self.telemetrySendInFlight = false
                let next = self.pendingTelemetry
                self.pendingTelemetry = nil

                DispatchQueue.main.async {
                    if let error {
                        self.lastError = error.localizedDescription
                    } else {
                        self.lastSentAt = Date()
                        self.lastJSON = json
                    }
                    self.notifyChange()
                }

                if self.transportConnected, let next {
                    self.sendTelemetryFrameOnQueue(data: next.data, json: next.json)
                }
            }
        }
    }

    private func markTransportDisconnectedOnQueue() {
        dispatchPrecondition(condition: .onQueue(self.queue))
        self.transportGeneration &+= 1
        self.transportConnected = false
        self.telemetrySendInFlight = false
        self.pendingTelemetry = nil
    }

    private func finishTransientTest(after delay: TimeInterval = 0) {
        self.transientTestTimeout?.cancel()
        self.transientTestTimeout = nil
        let finish = { [weak self] in
            guard let self, self.transientTestActive, !self.enabled else { return }
            self.transientTestActive = false
            self.webSocket.stop()
            self.onTelemetryRequirementChange?(false)
            self.notifyChange()
        }
        if delay > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: finish)
        } else {
            finish()
        }
    }

    private func cancelTransientTest() {
        self.cancelConnectionTest()
        self.transientTestActive = false
    }

    private func cancelConnectionTest() {
        self.transientTestTimeout?.cancel()
        self.transientTestTimeout = nil
        self.testRequested = false
    }

    private func refreshRouteWarning() {
        self.routeWarning = MonitorRouteDiagnostics.warning(host: self.host, preferWiFi: self.preferWiFi)
    }

    private func handleIncoming(_ data: Data) {
        guard data.count <= 2_048 else { return }
        do {
            let command = try MonitorProtocolCodec.decodeCommand(from: data)
            guard command.type == "command",
                  command.protocolVersion == 1,
                  !command.requestID.isEmpty,
                  command.requestID.utf8.count <= 128,
                  command.targetDeviceID.utf8.count <= 128,
                  command.targetDeviceID == self.deviceID else {
                return
            }

            switch command.action {
            case "switch_display":
                guard command.target == "mac" || command.target == "windows" else { return }
                DispatchQueue.main.async {
                    NotificationCenter.default.post(name: .kvmCommandReceived, object: command)
                }
            case "display_sleep", "display_wake":
                guard command.target == nil else { return }
                self.displayPowerController.execute(
                    requestID: command.requestID,
                    deviceID: self.deviceID,
                    action: command.action
                ) { [weak self] result in
                    self?.sendDisplayPowerResult(result)
                }
            default:
                self.sendDisplayPowerResult(DisplayPowerCommandResultMessage(
                    requestID: command.requestID,
                    deviceID: self.deviceID,
                    action: command.action,
                    success: false,
                    displayPowerState: "unchanged",
                    error: "unsupported_action"
                ))
            }
        } catch {
            DispatchQueue.main.async { [weak self] in
                self?.lastError = "invalid_server_message"
                self?.notifyChange()
            }
        }
    }

    private func notifyChange() {
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: .monitorServiceDidChange, object: self)
        }
    }
}

private final class DisplayPowerController {
    private let queue = DispatchQueue(label: "com.wei.statsforkvm.display-power", qos: .userInitiated)
    private var cachedResults: [String: DisplayPowerCommandResultMessage] = [:]
    private var requestOrder: [String] = []

    func execute(requestID: String, deviceID: String, action: String,
                 completion: @escaping (DisplayPowerCommandResultMessage) -> Void) {
        self.queue.async {
            if let cached = self.cachedResults[requestID] {
                completion(cached)
                return
            }

            let invocation: (path: String, arguments: [String], state: String, error: String)
            switch action {
            case "display_sleep":
                invocation = ("/usr/bin/pmset", ["displaysleepnow"], "sleep_requested", "display_sleep_failed")
            case "display_wake":
                // A short user-activity assertion resets display idle time and
                // asks macOS to restore video output without waking from system sleep.
                invocation = ("/usr/bin/caffeinate", ["-u", "-t", "1"], "wake_requested", "display_wake_failed")
            default:
                let result = DisplayPowerCommandResultMessage(
                    requestID: requestID,
                    deviceID: deviceID,
                    action: action,
                    success: false,
                    displayPowerState: "unchanged",
                    error: "unsupported_action"
                )
                self.remember(result)
                completion(result)
                return
            }

            let succeeded = self.run(invocation.path, arguments: invocation.arguments)
            let result = DisplayPowerCommandResultMessage(
                requestID: requestID,
                deviceID: deviceID,
                action: action,
                success: succeeded,
                displayPowerState: succeeded ? invocation.state : "unchanged",
                error: succeeded ? nil : invocation.error
            )
            self.remember(result)
            completion(result)
        }
    }

    private func run(_ path: String, arguments: [String]) -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: path)
        process.arguments = arguments
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
            return process.terminationReason == .exit && process.terminationStatus == 0
        } catch {
            return false
        }
    }

    private func remember(_ result: DisplayPowerCommandResultMessage) {
        self.cachedResults[result.requestID] = result
        self.requestOrder.append(result.requestID)
        if self.requestOrder.count > 64 {
            let overflow = self.requestOrder.count - 64
            let expired = Array(self.requestOrder.prefix(overflow))
            self.requestOrder.removeFirst(overflow)
            for requestID in expired {
                self.cachedResults.removeValue(forKey: requestID)
            }
        }
    }
}
