//
//  KVMSettingsView.swift
//  StatsForKVM
//

import Cocoa
import Kit

final class KVMSettingsView: NSStackView {
    private let controller = DisplayController.shared
    private var enabledSwitch: NSSwitch!
    private var displaySelector: NSPopUpButton!
    private var macInputSelector: NSPopUpButton!
    private var windowsInputSelector: NSPopUpButton!
    private var statusValue: NSTextField!

    private let inputSources: [(name: String, value: Int)] = [
        ("DisplayPort", 7),
        ("HDMI 1", 5),
        ("HDMI 2", 6)
    ]

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

        self.enabledSwitch = self.switchView(action: #selector(self.toggleEnabled), state: self.controller.enabled)
        self.displaySelector = NSPopUpButton()
        self.displaySelector.target = self
        self.displaySelector.action = #selector(self.selectDisplay)

        scrollView.stackView.addArrangedSubview(PreferencesSection(title: "KVM", [
            PreferencesRow(localizedString("Enable display control"), component: self.enabledSwitch),
            PreferencesRow(localizedString("Target display"), component: self.displaySelector),
            PreferencesRow(component: self.buttonView(#selector(self.refreshDisplays), text: localizedString("Refresh displays")))
        ]))

        self.macInputSelector = self.makeInputSelector(selected: self.controller.macInput, action: #selector(self.changeMacInput))
        self.windowsInputSelector = self.makeInputSelector(selected: self.controller.windowsInput, action: #selector(self.changeWindowsInput))
        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Input sources"), [
            PreferencesRow("Mac", component: self.macInputSelector),
            PreferencesRow("Windows", component: self.windowsInputSelector)
        ]))

        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Manual test"), [
            PreferencesRow(localizedString("Switch to Mac"), component: self.buttonView(#selector(self.switchToMac), text: localizedString("Switch"))),
            PreferencesRow(localizedString("Switch to Windows"), component: self.buttonView(#selector(self.switchToWindows), text: localizedString("Switch")))
        ]))

        self.statusValue = NSTextField(wrappingLabelWithString: localizedString("Not tested"))
        self.statusValue.isSelectable = true
        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Last operation"), [
            PreferencesRow(component: self.statusValue)
        ]))

        let warning = NSTextField(wrappingLabelWithString: localizedString("Display switching uses private Apple display APIs from m1ddc. Input values and DDC support must be verified on this monitor after macOS updates."))
        warning.textColor = .secondaryLabelColor
        scrollView.stackView.addArrangedSubview(PreferencesSection(title: localizedString("Compatibility"), [
            PreferencesRow(component: warning)
        ]))

        self.addArrangedSubview(scrollView)

        NotificationCenter.default.addObserver(self, selector: #selector(self.render), name: .kvmDisplayControllerDidChange, object: nil)
        self.render()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    func viewWillAppear() {
        self.enabledSwitch.state = self.controller.enabled ? .on : .off
        self.refreshDisplays()
    }

    @objc private func toggleEnabled(_ sender: NSControl) {
        self.controller.setEnabled(controlState(sender))
    }

    @objc private func refreshDisplays() {
        self.displaySelector.removeAllItems()
        self.displaySelector.addItem(withTitle: localizedString("Scanning…"))
        self.displaySelector.isEnabled = false
        self.controller.refreshDisplays()
    }

    @objc private func selectDisplay(_ sender: NSPopUpButton) {
        guard let uuid = sender.selectedItem?.representedObject as? String else { return }
        self.controller.selectDisplay(uuid: uuid)
    }

    @objc private func changeMacInput(_ sender: NSPopUpButton) {
        guard let value = sender.selectedItem?.representedObject as? Int else { return }
        self.controller.setInputs(mac: value, windows: self.controller.windowsInput)
    }

    @objc private func changeWindowsInput(_ sender: NSPopUpButton) {
        guard let value = sender.selectedItem?.representedObject as? Int else { return }
        self.controller.setInputs(mac: self.controller.macInput, windows: value)
    }

    @objc private func switchToMac() {
        self.statusValue.stringValue = localizedString("Switching…")
        self.controller.switchToMac()
    }

    @objc private func switchToWindows() {
        self.statusValue.stringValue = localizedString("Switching…")
        self.controller.switchToWindows()
    }

    @objc private func render() {
        DispatchQueue.main.async {
            self.enabledSwitch.state = self.controller.enabled ? .on : .off
            self.renderDisplays()
            self.selectInput(self.macInputSelector, value: self.controller.macInput)
            self.selectInput(self.windowsInputSelector, value: self.controller.windowsInput)
            if let result = self.controller.lastResult {
                let target = result.target == "mac" ? "Mac" : "Windows"
                let error = result.error ?? "unknown_error"
                if result.confirmed {
                    self.statusValue.stringValue = localizedString("%0: input %1, confirmed", target, "\(result.input)")
                } else if result.writeSucceeded {
                    self.statusValue.stringValue = localizedString("%0: command written, not confirmed (%1)", target, error)
                } else {
                    self.statusValue.stringValue = localizedString("Failed: %0", error)
                }
            }
        }
    }

    private func renderDisplays() {
        self.displaySelector.removeAllItems()
        if self.controller.displays.isEmpty {
            self.displaySelector.addItem(withTitle: localizedString("No external display found"))
            self.displaySelector.isEnabled = false
            return
        }
        self.displaySelector.isEnabled = true
        for display in self.controller.displays {
            self.displaySelector.addItem(withTitle: display.displayName)
            self.displaySelector.lastItem?.representedObject = display.uuid
            if display.uuid == self.controller.selectedDisplayUUID {
                self.displaySelector.select(self.displaySelector.lastItem)
            }
        }
    }

    private func makeInputSelector(selected: Int, action: Selector) -> NSPopUpButton {
        let selector = NSPopUpButton()
        selector.target = self
        selector.action = action
        for source in self.inputSources {
            selector.addItem(withTitle: "\(source.name) (\(source.value))")
            selector.lastItem?.representedObject = source.value
        }
        self.selectInput(selector, value: selected)
        return selector
    }

    private func selectInput(_ selector: NSPopUpButton, value: Int) {
        if let item = selector.itemArray.first(where: { ($0.representedObject as? Int) == value }) {
            selector.select(item)
        }
    }
}
