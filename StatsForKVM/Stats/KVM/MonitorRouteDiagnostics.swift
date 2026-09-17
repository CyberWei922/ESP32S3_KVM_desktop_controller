//
//  MonitorRouteDiagnostics.swift
//  StatsForKVM
//

import Darwin
import Foundation

struct MonitorIPv4Interface {
    let name: String
    let address: UInt32
    let netmask: UInt32

    var isVirtual: Bool {
        let prefixes = ["utun", "tun", "tap", "ppp", "ipsec", "vmnet", "vmenet", "bridge"]
        return prefixes.contains { self.name.hasPrefix($0) }
    }

    func contains(_ destination: UInt32) -> Bool {
        (self.address & self.netmask) == (destination & self.netmask)
    }
}

enum MonitorRouteDiagnostics {
    static func warning(host: String, preferWiFi: Bool) -> String? {
        guard let destination = self.ipv4Address(host) else { return nil }
        return self.warning(
            destination: destination,
            interfaces: self.ipv4Interfaces(),
            preferWiFi: preferWiFi
        )
    }

    static func warning(
        destination: UInt32,
        interfaces: [MonitorIPv4Interface],
        preferWiFi: Bool
    ) -> String? {
        let matches = interfaces.filter { $0.contains(destination) }
        let physical = matches.filter { !$0.isVirtual }
        let virtual = matches.filter(\.isVirtual)

        if !physical.isEmpty, !virtual.isEmpty {
            let names = matches.map(\.name).sorted().joined(separator: ",")
            return "vpn_subnet_overlap_detected:\(names)"
        }
        if physical.isEmpty, !virtual.isEmpty {
            return "destination_routes_only_through_virtual_interface:\(virtual.map(\.name).sorted().joined(separator: ","))"
        }
        if preferWiFi, physical.isEmpty {
            return "esp32_not_on_physical_lan_subnet"
        }
        return nil
    }

    private static func ipv4Address(_ host: String) -> UInt32? {
        var address = in_addr()
        let normalized = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard normalized.withCString({ inet_pton(AF_INET, $0, &address) }) == 1 else { return nil }
        return address.s_addr
    }

    private static func ipv4Interfaces() -> [MonitorIPv4Interface] {
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0, let first = head else { return [] }
        defer { freeifaddrs(head) }

        var result: [MonitorIPv4Interface] = []
        var cursor: UnsafeMutablePointer<ifaddrs>? = first
        while let current = cursor {
            let interface = current.pointee
            defer { cursor = interface.ifa_next }
            guard (Int32(interface.ifa_flags) & IFF_UP) != 0,
                  let addressPointer = interface.ifa_addr,
                  let netmaskPointer = interface.ifa_netmask,
                  Int32(addressPointer.pointee.sa_family) == AF_INET,
                  Int32(netmaskPointer.pointee.sa_family) == AF_INET else {
                continue
            }

            let address = UnsafeRawPointer(addressPointer)
                .assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr.s_addr
            let netmask = UnsafeRawPointer(netmaskPointer)
                .assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr.s_addr
            let name = String(cString: interface.ifa_name)
            guard name != "lo0" else { continue }
            result.append(MonitorIPv4Interface(name: name, address: address, netmask: netmask))
        }
        return result
    }
}
