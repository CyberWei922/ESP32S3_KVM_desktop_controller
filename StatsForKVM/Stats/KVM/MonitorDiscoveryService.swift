//
//  MonitorDiscoveryService.swift
//  StatsForKVM
//

import Foundation
import Network

extension Notification.Name {
    static let monitorDiscoveryDidChange = Notification.Name("com.wei.statsforkvm.monitor.discoveryDidChange")
}

struct MonitorDiscoveredService: Equatable {
    let name: String
    let domain: String
    let interfaceName: String?

    var displayName: String {
        guard let interfaceName, !interfaceName.isEmpty else { return self.name }
        return "\(self.name) (\(interfaceName))"
    }
}

/// Bonjour is an auxiliary diagnostic/discovery channel only. Connections use
/// the host saved in Monitor settings so a VPN cannot silently redirect the
/// destination by changing discovery results.
final class MonitorDiscoveryService {
    static let shared = MonitorDiscoveryService()

    private let queue = DispatchQueue(label: "com.wei.statsforkvm.monitor.discovery", qos: .utility)
    private var browser: NWBrowser?

    private(set) var services: [MonitorDiscoveredService] = []
    private(set) var error: String?

    private init() {}

    func start() {
        self.queue.async { [weak self] in
            guard let self, self.browser == nil else { return }
            let descriptor = NWBrowser.Descriptor.bonjour(type: "_statsforkvm._tcp", domain: nil)
            let parameters = NWParameters.tcp
            parameters.includePeerToPeer = false
            parameters.preferNoProxies = true

            let browser = NWBrowser(for: descriptor, using: parameters)
            self.browser = browser
            browser.stateUpdateHandler = { [weak self, weak browser] state in
                guard let self, let browser, browser === self.browser else { return }
                switch state {
                case .failed(let error):
                    self.publish([], error: "bonjour_failed_\(error)")
                case .cancelled:
                    self.publish([], error: nil)
                default:
                    break
                }
            }
            browser.browseResultsChangedHandler = { [weak self, weak browser] results, _ in
                guard let self, let browser, browser === self.browser else { return }
                let services = results.compactMap { result -> MonitorDiscoveredService? in
                    guard case let .service(name, _, domain, interface) = result.endpoint else { return nil }
                    return MonitorDiscoveredService(
                        name: name,
                        domain: domain,
                        interfaceName: interface?.name
                    )
                }.sorted { lhs, rhs in
                    lhs.displayName.localizedCaseInsensitiveCompare(rhs.displayName) == .orderedAscending
                }
                self.publish(services, error: nil)
            }
            browser.start(queue: self.queue)
        }
    }

    func stop() {
        self.queue.async { [weak self] in
            guard let self else { return }
            self.browser?.stateUpdateHandler = nil
            self.browser?.browseResultsChangedHandler = nil
            self.browser?.cancel()
            self.browser = nil
            self.publish([], error: nil)
        }
    }

    private func publish(_ services: [MonitorDiscoveredService], error: String?) {
        DispatchQueue.main.async {
            self.services = services
            self.error = error
            NotificationCenter.default.post(name: .monitorDiscoveryDidChange, object: self)
        }
    }
}
