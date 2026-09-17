//
//  DisplayController.swift
//  StatsForKVM
//

import Foundation
import Kit

struct KVMDisplayDescriptor: Equatable {
    let uuid: String
    let name: String
    let manufacturer: String
    let vendor: UInt32
    let model: UInt32
    let serial: UInt32
    let ddcAvailable: Bool

    var displayName: String {
        let suffix = self.ddcAvailable ? "" : localizedString(" (DDC unavailable)")
        return "\(self.name)\(suffix)"
    }
}

struct KVMSwitchResult {
    let target: String
    let input: UInt16
    let writeSucceeded: Bool
    let confirmed: Bool
    let readBackInput: UInt16?
    let error: String?
}

final class DisplayController {
    static let shared = DisplayController()

    private enum Key {
        static let enabled = "statsforkvm_kvm_enabled"
        static let displayUUID = "statsforkvm_kvm_display_uuid"
        static let macInput = "statsforkvm_kvm_mac_input"
        static let windowsInput = "statsforkvm_kvm_windows_input"
    }

    private let queue = DispatchQueue(label: "com.wei.statsforkvm.kvm.display", qos: .userInitiated)
    private var handledRequestIDs: [String] = []
    private var listening = false

    private(set) var displays: [KVMDisplayDescriptor] = []
    private(set) var lastResult: KVMSwitchResult?

    var enabled: Bool {
        Store.shared.bool(key: Key.enabled, defaultValue: false)
    }
    var selectedDisplayUUID: String {
        Store.shared.string(key: Key.displayUUID, defaultValue: "")
    }
    var macInput: Int {
        let saved = Store.shared.int(key: Key.macInput, defaultValue: 6)
        return [5, 6, 7].contains(saved) ? saved : 6
    }
    var windowsInput: Int {
        let saved = Store.shared.int(key: Key.windowsInput, defaultValue: 7)
        return [5, 6, 7].contains(saved) ? saved : 7
    }

    private init() {}

    func startCommandListener() {
        guard !self.listening else { return }
        self.listening = true
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(self.handleRemoteCommand),
            name: .kvmCommandReceived,
            object: nil
        )
    }

    func setEnabled(_ enabled: Bool) {
        Store.shared.set(key: Key.enabled, value: enabled)
        self.notifyChange()
    }

    func selectDisplay(uuid: String) {
        Store.shared.set(key: Key.displayUUID, value: uuid)
        self.notifyChange()
    }

    func setInputs(mac: Int, windows: Int) {
        Store.shared.set(key: Key.macInput, value: min(max(mac, 1), Int(UInt16.max)))
        Store.shared.set(key: Key.windowsInput, value: min(max(windows, 1), Int(UInt16.max)))
        self.notifyChange()
    }

    func refreshDisplays(completion: (() -> Void)? = nil) {
        self.queue.async { [weak self] in
            guard let self else { return }
            let list = KVMDDCBridge.availableDisplays().map {
                KVMDisplayDescriptor(
                    uuid: $0.uuid,
                    name: $0.name,
                    manufacturer: $0.manufacturer,
                    vendor: $0.vendor,
                    model: $0.model,
                    serial: $0.serial,
                    ddcAvailable: $0.isDDCAvailable
                )
            }
            DispatchQueue.main.async {
                self.displays = list
                if self.selectedDisplayUUID.isEmpty, let first = list.first(where: { $0.ddcAvailable }) {
                    self.selectDisplay(uuid: first.uuid)
                }
                self.notifyChange()
                completion?()
            }
        }
    }

    func switchToMac(completion: ((KVMSwitchResult) -> Void)? = nil) {
        self.switchInput(target: "mac", input: self.macInput, verifyReadBack: true, completion: completion)
    }

    func switchToWindows(completion: ((KVMSwitchResult) -> Void)? = nil) {
        self.switchInput(target: "windows", input: self.windowsInput, verifyReadBack: true, completion: completion)
    }

    private func switchInput(target: String, input: Int, verifyReadBack: Bool,
                             completion: ((KVMSwitchResult) -> Void)?) {
        let displayUUID = self.selectedDisplayUUID
        guard self.enabled else {
            self.finish(KVMSwitchResult(target: target, input: UInt16(clamping: input), writeSucceeded: false, confirmed: false, readBackInput: nil, error: "kvm_disabled"), completion: completion)
            return
        }
        guard !displayUUID.isEmpty else {
            self.finish(KVMSwitchResult(target: target, input: UInt16(clamping: input), writeSucceeded: false, confirmed: false, readBackInput: nil, error: "display_not_selected"), completion: completion)
            return
        }

        self.queue.async { [weak self] in
            guard let self else { return }
            let requestedInput = UInt16(clamping: input)
            var errorMessage: NSString?
            let wrote = KVMDDCBridge.setInput(requestedInput, displayUUID: displayUUID, errorMessage: &errorMessage)
            guard wrote else {
                self.finish(KVMSwitchResult(
                    target: target,
                    input: requestedInput,
                    writeSucceeded: false,
                    confirmed: false,
                    readBackInput: nil,
                    error: errorMessage as String? ?? "ddc_write_failed"
                ), completion: completion)
                return
            }

            // X41Q input read-back is not reliable. Remote KVM commands report
            // the successful write immediately so ESP32 can pulse USB without
            // waiting 0.6 seconds for a read that cannot confirm this monitor.
            guard verifyReadBack else {
                self.finish(KVMSwitchResult(
                    target: target,
                    input: requestedInput,
                    writeSucceeded: true,
                    confirmed: false,
                    readBackInput: nil,
                    error: "input_written_but_unconfirmed"
                ), completion: completion)
                return
            }

            // Many displays temporarily drop DDC while changing input. A failed
            // read-back therefore means "written but unconfirmed", not failure.
            Thread.sleep(forTimeInterval: 0.6)
            var readBack: UInt16 = 0
            errorMessage = nil
            let read = KVMDDCBridge.readInput(forDisplayUUID: displayUUID, value: &readBack, errorMessage: &errorMessage)
            let confirmed = read && readBack == requestedInput
            self.finish(KVMSwitchResult(
                target: target,
                input: requestedInput,
                writeSucceeded: true,
                confirmed: confirmed,
                readBackInput: read ? readBack : nil,
                error: confirmed ? nil : (errorMessage as String? ?? "input_written_but_unconfirmed")
            ), completion: completion)
        }
    }

    private func finish(_ result: KVMSwitchResult, completion: ((KVMSwitchResult) -> Void)?) {
        DispatchQueue.main.async {
            self.lastResult = result
            self.notifyChange()
            completion?(result)
        }
    }

    @objc private func handleRemoteCommand(_ notification: Notification) {
        guard let command = notification.object as? KVMCommandMessage else { return }
        guard !self.handledRequestIDs.contains(command.requestID) else { return }
        self.handledRequestIDs.append(command.requestID)
        if self.handledRequestIDs.count > 64 {
            self.handledRequestIDs.removeFirst(self.handledRequestIDs.count - 64)
        }

        let completion: (KVMSwitchResult) -> Void = { result in
            MonitorService.shared.sendCommandResult(
                requestID: command.requestID,
                result: result
            )
        }
        if command.target == "mac" {
            self.switchInput(target: "mac", input: self.macInput,
                             verifyReadBack: false, completion: completion)
        } else {
            self.switchInput(target: "windows", input: self.windowsInput,
                             verifyReadBack: false, completion: completion)
        }
    }

    private func notifyChange() {
        NotificationCenter.default.post(name: .kvmDisplayControllerDidChange, object: self)
    }
}

extension Notification.Name {
    static let kvmDisplayControllerDidChange = Notification.Name("com.wei.statsforkvm.kvm.displayControllerDidChange")
}
