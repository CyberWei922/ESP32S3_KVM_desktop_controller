//
//  Kit.swift
//  Tests
//
//  Created by Serhiy Mytrovtsiy on 04/07/2026.
//  Using Swift 6.0.
//  Running on macOS 26.5.
//
//  Copyright © 2026 Serhiy Mytrovtsiy. All rights reserved.
//

import XCTest
import Kit
@testable import StatsForKVM

class KitTests: XCTestCase {
    func testKVMTelemetrySnapshotUsesPercentAndPreservesSources() throws {
        KVMTelemetryStore.shared.updateAverageCPUTemperature(48.25)
        KVMTelemetryStore.shared.updateMemoryUsage(fraction: 0.513)

        let snapshot = KVMTelemetryStore.shared.snapshot()
        XCTAssertEqual(snapshot.averageCPUTemperature.value, 48.25)
        XCTAssertTrue(snapshot.averageCPUTemperature.valid)
        XCTAssertEqual(snapshot.averageCPUTemperature.source, "Average CPU")
        XCTAssertEqual(snapshot.memoryUsage.value ?? -1, 51.3, accuracy: 0.0001)
        XCTAssertTrue(snapshot.memoryUsage.valid)
        XCTAssertEqual(snapshot.memoryUsage.unit, "percent")
    }

    func testKVMTelemetryNeverSubstitutesZeroForUnavailableTemperature() throws {
        KVMTelemetryStore.shared.updateAverageCPUTemperature(nil, error: "sensor_unavailable")
        var snapshot = KVMTelemetryStore.shared.snapshot()
        XCTAssertNil(snapshot.averageCPUTemperature.value)
        XCTAssertFalse(snapshot.averageCPUTemperature.valid)
        XCTAssertEqual(snapshot.averageCPUTemperature.error, "sensor_unavailable")

        KVMTelemetryStore.shared.updateAverageCPUTemperature(0)
        snapshot = KVMTelemetryStore.shared.snapshot()
        XCTAssertNil(snapshot.averageCPUTemperature.value)
        XCTAssertFalse(snapshot.averageCPUTemperature.valid)
    }

    func testKVMProtocolEncodesUnavailableTemperatureAsJSONNull() throws {
        KVMTelemetryStore.shared.updateAverageCPUTemperature(nil, error: "sensor_unavailable")
        KVMTelemetryStore.shared.updateMemoryUsage(fraction: 0.42)
        let message = MonitorTelemetryMessage(
            deviceID: "mac-test",
            sequence: 7,
            snapshot: KVMTelemetryStore.shared.snapshot()
        )

        let data = try MonitorProtocolCodec.encode(message)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let metrics = try XCTUnwrap(json["metrics"] as? [String: Any])
        let temperature = try XCTUnwrap(metrics["cpu.temperature.average"] as? [String: Any])
        let memory = try XCTUnwrap(metrics["memory.usage"] as? [String: Any])

        XCTAssertTrue(temperature["value"] is NSNull)
        XCTAssertEqual(temperature["valid"] as? Bool, false)
        XCTAssertEqual(temperature["error"] as? String, "sensor_unavailable")
        XCTAssertEqual(memory["value"] as? Double ?? -1, 42, accuracy: 0.0001)
        XCTAssertEqual(json["protocol_version"] as? Int, 1)
        XCTAssertEqual(json["sequence"] as? Int, 7)
    }

    func testKVMProtocolDecodesExplicitIDFields() throws {
        let data = Data(#"{"type":"command","protocol_version":1,"request_id":"request-1","target_device_id":"mac-1","action":"switch_display","target":"windows"}"#.utf8)
        let command = try MonitorProtocolCodec.decodeCommand(from: data)

        XCTAssertEqual(command.requestID, "request-1")
        XCTAssertEqual(command.targetDeviceID, "mac-1")
        XCTAssertEqual(command.protocolVersion, 1)
        XCTAssertEqual(command.target, "windows")
    }

    func testKVMHelloAdvertisesDisplayPowerCapabilities() throws {
        let data = try MonitorProtocolCodec.encode(MonitorHelloMessage(deviceID: "mac-1"))
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let capabilities = try XCTUnwrap(json["capabilities"] as? [String])

        XCTAssertEqual(json["protocol_version"] as? Int, 1)
        XCTAssertEqual(capabilities, ["switch_display", "display_sleep", "display_wake"])
    }

    func testKVMProtocolDecodesDisplayPowerWithoutTarget() throws {
        let data = Data(#"{"type":"command","protocol_version":1,"request_id":"request-sleep","target_device_id":"mac-1","action":"display_sleep"}"#.utf8)
        let command = try MonitorProtocolCodec.decodeCommand(from: data)

        XCTAssertEqual(command.action, "display_sleep")
        XCTAssertNil(command.target)
    }

    func testDisplayPowerResultMatchesProtocol() throws {
        let message = DisplayPowerCommandResultMessage(
            requestID: "request-wake",
            deviceID: "mac-1",
            action: "display_wake",
            success: true,
            displayPowerState: "wake_requested",
            error: nil
        )
        let data = try MonitorProtocolCodec.encode(message)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["action"] as? String, "display_wake")
        XCTAssertEqual(json["display_power_state"] as? String, "wake_requested")
        XCTAssertTrue(json["error"] is NSNull)
    }

    func testKVMTelemetryMarksOldSamplesStaleWithoutKeepingValue() throws {
        KVMTelemetryStore.shared.updateAverageCPUTemperature(45)
        let snapshot = KVMTelemetryStore.shared.snapshot(staleAfter: -1)

        XCTAssertNil(snapshot.averageCPUTemperature.value)
        XCTAssertFalse(snapshot.averageCPUTemperature.valid)
        XCTAssertEqual(snapshot.averageCPUTemperature.error, "data_stale")
    }

    func testKVMCommandResultKeepsNullableFields() throws {
        let message = KVMCommandResultMessage(
            requestID: "request-2",
            deviceID: "mac-1",
            success: false,
            writeSucceeded: false,
            confirmed: false,
            requestedInput: 27,
            readBackInput: nil,
            error: "kvm_disabled"
        )
        let data = try MonitorProtocolCodec.encode(message)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["request_id"] as? String, "request-2")
        XCTAssertEqual(json["write_succeeded"] as? Bool, false)
        XCTAssertTrue(json["read_back_input"] is NSNull)
        XCTAssertEqual(json["error"] as? String, "kvm_disabled")
    }

    func testKVMWebSocketURLNormalizesPath() throws {
        let configuration = MonitorConnectionConfiguration(
            host: "127.0.0.1",
            port: 81,
            path: "statsforkvm",
            preferWiFi: false
        )
        XCTAssertEqual(configuration.url?.absoluteString, "ws://127.0.0.1:81/statsforkvm")
    }

    func testKVMRouteDiagnosticsDetectsVPNSubnetOverlap() throws {
        let interfaces = [
            MonitorIPv4Interface(name: "en0", address: 0xC0A8_0164, netmask: 0xFFFF_FF00),
            MonitorIPv4Interface(name: "utun5", address: 0xC0A8_0102, netmask: 0xFFFF_FF00)
        ]
        let warning = MonitorRouteDiagnostics.warning(
            destination: 0xC0A8_0132,
            interfaces: interfaces,
            preferWiFi: false
        )
        XCTAssertEqual(warning, "vpn_subnet_overlap_detected:en0,utun5")
    }

    func testKVMRouteDiagnosticsDetectsVirtualOnlyRoute() throws {
        let interfaces = [
            MonitorIPv4Interface(name: "utun2", address: 0x0A00_0002, netmask: 0xFFFF_0000)
        ]
        let warning = MonitorRouteDiagnostics.warning(
            destination: 0x0A00_0042,
            interfaces: interfaces,
            preferWiFi: false
        )
        XCTAssertEqual(warning, "destination_routes_only_through_virtual_interface:utun2")
    }

    func testIsNewestVersion_release() throws {
        XCTAssertFalse(isNewestVersion(currentVersion: "v2.11.0", latestVersion: "v2.11.0"))
        XCTAssertTrue(isNewestVersion(currentVersion: "v2.11.0", latestVersion: "v2.11.1"))
        XCTAssertFalse(isNewestVersion(currentVersion: "v2.11.1", latestVersion: "v2.11.0"))
        XCTAssertTrue(isNewestVersion(currentVersion: "v2.11.0", latestVersion: "v2.12.0"))
        XCTAssertFalse(isNewestVersion(currentVersion: "v2.12.0", latestVersion: "v2.11.5"))
        XCTAssertTrue(isNewestVersion(currentVersion: "v2.11.0", latestVersion: "v3.0.0"))
        XCTAssertFalse(isNewestVersion(currentVersion: "v3.0.0", latestVersion: "v2.99.99"))
    }
    
    func testIsNewestVersion_beta() throws {
        XCTAssertFalse(isNewestVersion(currentVersion: "v2.11.0-beta1", latestVersion: "v2.11.0-beta1"))
        XCTAssertFalse(isNewestVersion(currentVersion: "v2.11.0-beta2", latestVersion: "v2.11.0-beta1"))
        XCTAssertTrue(isNewestVersion(currentVersion: "v2.11.0-beta1", latestVersion: "v2.11.0-beta2"))
        XCTAssertTrue(isNewestVersion(currentVersion: "v2.11.0-beta1", latestVersion: "v2.11.0"))
        XCTAssertFalse(isNewestVersion(currentVersion: "v2.11.0-beta1", latestVersion: "v2.10.9"))
        XCTAssertFalse(isNewestVersion(currentVersion: "v2.11.0", latestVersion: "v2.11.1-beta1"))
        XCTAssertTrue(isNewestVersion(currentVersion: "v2.11.0-beta1", latestVersion: "v2.11.1-beta1"))
    }
    
    func testIsNewestVersion_malformed() throws {
        XCTAssertFalse(isNewestVersion(currentVersion: "v3", latestVersion: "v3.0.0"))
        XCTAssertTrue(isNewestVersion(currentVersion: "v3", latestVersion: "v3.0.1"))
        XCTAssertFalse(isNewestVersion(currentVersion: "v3.0", latestVersion: "v3.0.0"))
        XCTAssertFalse(isNewestVersion(currentVersion: "", latestVersion: ""))
    }
    
    func testUnitsGetReadableSpeed_byte() throws {
        XCTAssertEqual(Units(bytes: 0).getReadableSpeed(base: .byte), "0 KB/s")
        XCTAssertEqual(Units(bytes: 999).getReadableSpeed(base: .byte), "0 KB/s")
        XCTAssertEqual(Units(bytes: 1_000).getReadableSpeed(base: .byte), "1 KB/s")
        XCTAssertEqual(Units(bytes: 500_000).getReadableSpeed(base: .byte), "500 KB/s")
        XCTAssertEqual(Units(bytes: 2_500_000).getReadableSpeed(base: .byte), "2.5 MB/s")
        XCTAssertEqual(Units(bytes: 150_000_000).getReadableSpeed(base: .byte), "150 MB/s")
        XCTAssertEqual(Units(bytes: 2_000_000_000).getReadableSpeed(base: .byte), "2.0 GB/s")
        XCTAssertEqual(Units(bytes: 2_000_000_000_000).getReadableSpeed(base: .byte), "2.0 TB/s")
        XCTAssertEqual(Units(bytes: -5).getReadableSpeed(base: .byte), "0 KB/s")
    }
    
    func testUnitsGetReadableSpeed_bit() throws {
        XCTAssertEqual(Units(bytes: 100).getReadableSpeed(base: .bit), "0 Kb/s")
        XCTAssertEqual(Units(bytes: 50_000).getReadableSpeed(base: .bit), "400 Kb/s")
        XCTAssertEqual(Units(bytes: 500_000).getReadableSpeed(base: .bit), "4.0 Mb/s")
        XCTAssertEqual(Units(bytes: 200_000_000).getReadableSpeed(base: .bit), "1.6 Gb/s")
        XCTAssertEqual(Units(bytes: 200_000_000_000).getReadableSpeed(base: .bit), "1.6 Tb/s")
    }
    
    func testUnitsGetReadableSpeed_fixedUnit() throws {
        XCTAssertEqual(Units(bytes: 500_000).getReadableSpeed(base: .byte, unit: "KB"), "500 KB/s")
        XCTAssertEqual(Units(bytes: 500_000).getReadableSpeed(base: .byte, unit: "MB"), "0.5 MB/s")
        XCTAssertEqual(Units(bytes: 500_000).getReadableSpeed(base: .bit, unit: "MB"), "4 Mb/s")
    }
}
