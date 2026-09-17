//
//  MonitorProtocol.swift
//  StatsForKVM
//

import Foundation
import Kit

struct MonitorHelloMessage: Encodable {
    let type = "hello"
    let protocolVersion = 1
    let deviceID: String
    let platform = "macos"
    let app = "StatsForKVM"
    let capabilities = ["switch_display", "display_sleep", "display_wake", "dashboard_data"]
}

struct MonitorTelemetryMessage: Encodable {
    let type = "telemetry"
    let protocolVersion = 1
    let deviceID: String
    let platform = "macos"
    let sequence: UInt64
    let timestampMilliseconds: Int64
    let metrics: [String: KVMTelemetryMetric]

    init(deviceID: String, sequence: UInt64, snapshot: KVMTelemetrySnapshot) {
        self.deviceID = deviceID
        self.sequence = sequence
        self.timestampMilliseconds = Int64((Date().timeIntervalSince1970 * 1_000).rounded())
        self.metrics = [
            KVMTelemetryMetricID.averageCPUTemperature.rawValue: snapshot.averageCPUTemperature,
            KVMTelemetryMetricID.memoryUsage.rawValue: snapshot.memoryUsage
        ]
    }
}

struct KVMCommandMessage: Decodable {
    let type: String
    let protocolVersion: Int
    let requestID: String
    let targetDeviceID: String
    let action: String
    let target: String?

    private enum CodingKeys: String, CodingKey {
        case type
        case protocolVersion = "protocol_version"
        case requestID = "request_id"
        case targetDeviceID = "target_device_id"
        case action
        case target
    }
}

struct DisplayPowerCommandResultMessage: Encodable {
    let type = "command_result"
    let protocolVersion = 1
    let requestID: String
    let deviceID: String
    let action: String
    let success: Bool
    let displayPowerState: String
    let error: String?

    private enum CodingKeys: String, CodingKey {
        case type, protocolVersion, requestID, deviceID, action, success
        case displayPowerState, error
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(self.type, forKey: .type)
        try container.encode(self.protocolVersion, forKey: .protocolVersion)
        try container.encode(self.requestID, forKey: .requestID)
        try container.encode(self.deviceID, forKey: .deviceID)
        try container.encode(self.action, forKey: .action)
        try container.encode(self.success, forKey: .success)
        try container.encode(self.displayPowerState, forKey: .displayPowerState)
        if let error {
            try container.encode(error, forKey: .error)
        } else {
            try container.encodeNil(forKey: .error)
        }
    }
}

struct KVMCommandResultMessage: Encodable {
    let type = "command_result"
    let protocolVersion = 1
    let requestID: String
    let deviceID: String
    let success: Bool
    let writeSucceeded: Bool
    let confirmed: Bool
    let requestedInput: UInt16
    let readBackInput: UInt16?
    let error: String?

    private enum CodingKeys: String, CodingKey {
        case type
        case protocolVersion
        case requestID
        case deviceID
        case success
        case writeSucceeded
        case confirmed
        case requestedInput
        case readBackInput
        case error
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(self.type, forKey: .type)
        try container.encode(self.protocolVersion, forKey: .protocolVersion)
        try container.encode(self.requestID, forKey: .requestID)
        try container.encode(self.deviceID, forKey: .deviceID)
        try container.encode(self.success, forKey: .success)
        try container.encode(self.writeSucceeded, forKey: .writeSucceeded)
        try container.encode(self.confirmed, forKey: .confirmed)
        try container.encode(self.requestedInput, forKey: .requestedInput)
        if let readBackInput {
            try container.encode(readBackInput, forKey: .readBackInput)
        } else {
            try container.encodeNil(forKey: .readBackInput)
        }
        if let error {
            try container.encode(error, forKey: .error)
        } else {
            try container.encodeNil(forKey: .error)
        }
    }
}

enum MonitorProtocolCodec {
    private static let encoder: JSONEncoder = {
        let encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
        encoder.outputFormatting = [.sortedKeys]
        return encoder
    }()

    private static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        // Explicit CodingKeys preserve common protocol abbreviations such as
        // request_id -> requestID, which automatic camel-case conversion does not.
        decoder.keyDecodingStrategy = .useDefaultKeys
        return decoder
    }()

    static func encode<T: Encodable>(_ value: T) throws -> Data {
        try self.encoder.encode(value)
    }

    static func decodeCommand(from data: Data) throws -> KVMCommandMessage {
        try self.decoder.decode(KVMCommandMessage.self, from: data)
    }
}
