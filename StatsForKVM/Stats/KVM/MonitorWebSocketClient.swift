//
//  MonitorWebSocketClient.swift
//  StatsForKVM
//

import Foundation
import Network

enum MonitorConnectionState: Equatable {
    case disabled
    case connecting
    case connected(interface: String)
    case waiting(reason: String)
    case failed(reason: String)

    var title: String {
        switch self {
        case .disabled: return "Disabled"
        case .connecting: return "Connecting"
        case .connected: return "Connected"
        case .waiting: return "Waiting to reconnect"
        case .failed: return "Connection failed"
        }
    }

    var detail: String? {
        switch self {
        case .connected(let interface): return interface
        case .waiting(let reason), .failed(let reason): return reason
        default: return nil
        }
    }
}

struct MonitorConnectionConfiguration: Equatable {
    let host: String
    let port: UInt16
    let path: String
    let preferWiFi: Bool

    var url: URL? {
        var components = URLComponents()
        components.scheme = "ws"
        components.host = self.host.trimmingCharacters(in: .whitespacesAndNewlines)
        components.port = Int(self.port)
        let normalizedPath = self.path.trimmingCharacters(in: .whitespacesAndNewlines)
        components.path = normalizedPath.hasPrefix("/") ? normalizedPath : "/\(normalizedPath)"
        return components.url
    }
}

final class MonitorWebSocketClient {
    var onStateChange: ((MonitorConnectionState) -> Void)?
    var onMessage: ((Data) -> Void)?

    private let queue = DispatchQueue(label: "com.wei.statsforkvm.monitor.websocket", qos: .utility)
    private var connection: NWConnection?
    private var configuration: MonitorConnectionConfiguration?
    private var shouldRun = false
    private var reconnectAttempt = 0
    private var reconnectWorkItem: DispatchWorkItem?
    private var pingTimer: DispatchSourceTimer?
    private(set) var state: MonitorConnectionState = .disabled

    func start(configuration: MonitorConnectionConfiguration) {
        self.queue.async { [weak self] in
            guard let self else { return }
            let changed = self.configuration != configuration
            self.configuration = configuration
            self.shouldRun = true
            if changed || self.connection == nil {
                self.cancelConnection()
                self.connect()
            }
        }
    }

    func stop() {
        self.queue.async { [weak self] in
            guard let self else { return }
            self.shouldRun = false
            self.reconnectAttempt = 0
            self.cancelConnection()
            self.publish(.disabled)
        }
    }

    func reconnect() {
        self.queue.async { [weak self] in
            guard let self, self.shouldRun else { return }
            self.cancelConnection()
            self.connect()
        }
    }

    func send(_ data: Data, completion: ((Error?) -> Void)? = nil) {
        self.queue.async { [weak self] in
            guard let self, case .connected = self.state, let connection = self.connection else {
                completion?(MonitorWebSocketError.notConnected)
                return
            }
            let metadata = NWProtocolWebSocket.Metadata(opcode: .text)
            let context = NWConnection.ContentContext(
                identifier: "statsforkvm-text",
                metadata: [metadata]
            )
            connection.send(content: data, contentContext: context, isComplete: true, completion: .contentProcessed { error in
                completion?(error)
            })
        }
    }

    private func connect() {
        guard self.shouldRun, let configuration = self.configuration, let url = configuration.url else {
            self.publish(.failed(reason: "invalid_address"))
            return
        }

        self.reconnectWorkItem?.cancel()
        self.reconnectWorkItem = nil

        let webSocket = NWProtocolWebSocket.Options()
        webSocket.autoReplyPing = true
        webSocket.maximumMessageSize = 2_048

        let parameters = NWParameters.tcp
        parameters.preferNoProxies = true
        parameters.defaultProtocolStack.applicationProtocols.insert(webSocket, at: 0)
        if configuration.preferWiFi {
            parameters.requiredInterfaceType = .wifi
        }

        let connection = NWConnection(to: .url(url), using: parameters)
        self.connection = connection
        self.publish(.connecting)

        connection.stateUpdateHandler = { [weak self, weak connection] state in
            guard let self, let connection, connection === self.connection else { return }
            self.queue.async {
                self.handle(state, connection: connection)
            }
        }
        connection.pathUpdateHandler = { [weak self, weak connection] _ in
            guard let self, let connection, connection === self.connection else { return }
            self.queue.async {
                if case .connected = self.state {
                    self.publish(.connected(interface: self.interfaceDescription(connection.currentPath)))
                }
            }
        }
        connection.betterPathUpdateHandler = { [weak self] hasBetterPath in
            guard hasBetterPath else { return }
            self?.reconnect()
        }
        connection.start(queue: self.queue)
    }

    private func handle(_ newState: NWConnection.State, connection: NWConnection) {
        switch newState {
        case .ready:
            self.reconnectAttempt = 0
            self.publish(.connected(interface: self.interfaceDescription(connection.currentPath)))
            self.receiveNextMessage(on: connection)
            self.startPingTimer()
        case .waiting(let error):
            self.publish(.waiting(reason: self.classify(error, path: connection.currentPath)))
            self.scheduleReconnect()
        case .failed(let error):
            self.publish(.failed(reason: self.classify(error, path: connection.currentPath)))
            self.scheduleReconnect()
        case .cancelled:
            self.stopPingTimer()
        default:
            break
        }
    }

    private func receiveNextMessage(on connection: NWConnection) {
        connection.receiveMessage { [weak self, weak connection] data, context, _, error in
            guard let self, let connection, connection === self.connection else { return }
            self.queue.async {
                if let data, data.count <= 2_048,
                   let metadata = context?.protocolMetadata(definition: NWProtocolWebSocket.definition) as? NWProtocolWebSocket.Metadata,
                   metadata.opcode == .text {
                    self.onMessage?(data)
                }
                if let error {
                    self.publish(.waiting(reason: self.classify(error, path: connection.currentPath)))
                    self.scheduleReconnect()
                    return
                }
                self.receiveNextMessage(on: connection)
            }
        }
    }

    private func startPingTimer() {
        self.stopPingTimer()
        let timer = DispatchSource.makeTimerSource(queue: self.queue)
        timer.schedule(deadline: .now() + 15, repeating: 15)
        timer.setEventHandler { [weak self] in self?.sendPing() }
        self.pingTimer = timer
        timer.resume()
    }

    private func stopPingTimer() {
        self.pingTimer?.setEventHandler {}
        self.pingTimer?.cancel()
        self.pingTimer = nil
    }

    private func sendPing() {
        guard case .connected = self.state, let connection = self.connection else { return }
        let metadata = NWProtocolWebSocket.Metadata(opcode: .ping)
        metadata.setPongHandler(self.queue) { [weak self] error in
            guard let error else { return }
            self?.publish(.waiting(reason: self?.classify(error, path: self?.connection?.currentPath) ?? "heartbeat_failed"))
            self?.scheduleReconnect()
        }
        let context = NWConnection.ContentContext(identifier: "statsforkvm-ping", metadata: [metadata])
        connection.send(content: Data(), contentContext: context, isComplete: true, completion: .idempotent)
    }

    private func scheduleReconnect() {
        guard self.shouldRun, self.reconnectWorkItem == nil else { return }
        self.stopPingTimer()
        self.connection?.cancel()
        self.connection = nil

        self.reconnectAttempt = min(self.reconnectAttempt + 1, 6)
        let delay = min(pow(2, Double(self.reconnectAttempt - 1)), 30)
        let workItem = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.reconnectWorkItem = nil
            self.connect()
        }
        self.reconnectWorkItem = workItem
        self.queue.asyncAfter(deadline: .now() + delay, execute: workItem)
    }

    private func cancelConnection() {
        self.reconnectWorkItem?.cancel()
        self.reconnectWorkItem = nil
        self.stopPingTimer()
        self.connection?.stateUpdateHandler = nil
        self.connection?.pathUpdateHandler = nil
        self.connection?.betterPathUpdateHandler = nil
        self.connection?.cancel()
        self.connection = nil
    }

    private func publish(_ state: MonitorConnectionState) {
        self.state = state
        DispatchQueue.main.async { [weak self] in self?.onStateChange?(state) }
    }

    private func interfaceDescription(_ path: NWPath?) -> String {
        guard let path else { return "Unknown interface" }
        let type: NWInterface.InterfaceType
        let label: String
        if path.usesInterfaceType(.wiredEthernet) {
            type = .wiredEthernet
            label = "Ethernet"
        } else if path.usesInterfaceType(.wifi) {
            type = .wifi
            label = "Wi-Fi"
        } else if path.usesInterfaceType(.cellular) {
            type = .cellular
            label = "Cellular"
        } else if path.usesInterfaceType(.loopback) {
            type = .loopback
            label = "Loopback"
        } else {
            type = .other
            label = "Other / VPN or virtual interface"
        }

        let names = path.availableInterfaces
            .filter { $0.type == type }
            .map(\.name)
            .joined(separator: ", ")
        var details = names.isEmpty ? label : "\(label) (\(names))"
        if path.isConstrained { details += ", constrained" }
        if path.isExpensive { details += ", expensive" }
        return details
    }

    private func classify(_ error: NWError, path: NWPath?) -> String {
        if #available(macOS 15.0, *), path?.unsatisfiedReason == .localNetworkDenied {
            return "local_network_permission_denied"
        }
        switch error {
        case .posix(let code):
            switch code {
            case .ENETUNREACH, .EHOSTUNREACH:
                if path?.usesInterfaceType(.other) == true {
                    return "vpn_or_virtual_route_may_block_local_network"
                }
                return code == .ENETUNREACH ? "local_network_unreachable" : "esp32_unreachable"
            case .ECONNREFUSED: return "connection_refused"
            case .ETIMEDOUT: return "connection_timed_out"
            default: return "network_error_\(code.rawValue)"
            }
        case .dns: return "address_resolution_failed"
        case .tls: return "tls_handshake_failed"
        case .wifiAware: return "wifi_aware_network_error"
        @unknown default: return "network_error"
        }
    }
}

private enum MonitorWebSocketError: LocalizedError {
    case notConnected

    var errorDescription: String? { "websocket_not_connected" }
}
