//
//  telemetry.swift
//  Kit
//
//  Shared data bridge for StatsForKVM. Existing Stats readers publish their
//  latest values here so the Monitor feature never starts duplicate readers.
//

import Foundation

public enum KVMTelemetryMetricID: String, Codable, CaseIterable {
    case averageCPUTemperature = "cpu.temperature.average"
    case memoryUsage = "memory.usage"
}

public struct KVMTelemetryMetric: Codable, Equatable {
    public let value: Double?
    public let unit: String
    public let valid: Bool
    public let source: String
    public let error: String?
    public let sampledAtMilliseconds: Int64?

    public init(
        value: Double?,
        unit: String,
        valid: Bool,
        source: String,
        error: String?,
        sampledAtMilliseconds: Int64?
    ) {
        self.value = value
        self.unit = unit
        self.valid = valid
        self.source = source
        self.error = error
        self.sampledAtMilliseconds = sampledAtMilliseconds
    }

    private enum CodingKeys: String, CodingKey {
        case value
        case unit
        case valid
        case source
        case error
        case sampledAtMilliseconds
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        if let value {
            try container.encode(value, forKey: .value)
        } else {
            try container.encodeNil(forKey: .value)
        }
        try container.encode(self.unit, forKey: .unit)
        try container.encode(self.valid, forKey: .valid)
        try container.encode(self.source, forKey: .source)
        if let error {
            try container.encode(error, forKey: .error)
        } else {
            try container.encodeNil(forKey: .error)
        }
        if let sampledAtMilliseconds {
            try container.encode(sampledAtMilliseconds, forKey: .sampledAtMilliseconds)
        } else {
            try container.encodeNil(forKey: .sampledAtMilliseconds)
        }
    }
}

public struct KVMTelemetrySnapshot: Equatable {
    public let averageCPUTemperature: KVMTelemetryMetric
    public let memoryUsage: KVMTelemetryMetric

    public init(averageCPUTemperature: KVMTelemetryMetric, memoryUsage: KVMTelemetryMetric) {
        self.averageCPUTemperature = averageCPUTemperature
        self.memoryUsage = memoryUsage
    }
}

public extension Notification.Name {
    static let kvmTelemetryDidChange = Notification.Name("com.wei.statsforkvm.telemetry.didChange")
}

public final class KVMTelemetryStore {
    public static let shared = KVMTelemetryStore()

    private let lock = NSLock()
    private var averageCPUTemperature = KVMTelemetryMetric(
        value: nil,
        unit: "celsius",
        valid: false,
        source: "Average CPU",
        error: "sensor_unavailable",
        sampledAtMilliseconds: nil
    )
    private var memoryUsage = KVMTelemetryMetric(
        value: nil,
        unit: "percent",
        valid: false,
        source: "Stats RAM_Usage",
        error: "data_unavailable",
        sampledAtMilliseconds: nil
    )

    private init() {}

    public func updateAverageCPUTemperature(_ value: Double?, error: String? = nil) {
        let validValue = value.flatMap { candidate -> Double? in
            guard candidate.isFinite, candidate > 0, candidate < 110 else { return nil }
            return candidate
        }
        let metric = KVMTelemetryMetric(
            value: validValue,
            unit: "celsius",
            valid: validValue != nil,
            source: "Average CPU",
            error: validValue == nil ? (error ?? "sensor_unavailable") : nil,
            sampledAtMilliseconds: validValue == nil ? nil : Self.nowMilliseconds
        )
        self.update(metric, id: .averageCPUTemperature)
    }

    /// Accepts the same 0...1 fraction used by Stats' RAM_Usage.usage.
    public func updateMemoryUsage(fraction: Double?, error: String? = nil) {
        let validValue = fraction.flatMap { candidate -> Double? in
            guard candidate.isFinite, candidate >= 0, candidate <= 1 else { return nil }
            return candidate * 100
        }
        let metric = KVMTelemetryMetric(
            value: validValue,
            unit: "percent",
            valid: validValue != nil,
            source: "Stats RAM_Usage",
            error: validValue == nil ? (error ?? "data_unavailable") : nil,
            sampledAtMilliseconds: validValue == nil ? nil : Self.nowMilliseconds
        )
        self.update(metric, id: .memoryUsage)
    }

    public func snapshot(staleAfter seconds: TimeInterval = 5) -> KVMTelemetrySnapshot {
        self.lock.lock()
        let temperature = self.averageCPUTemperature
        let memory = self.memoryUsage
        self.lock.unlock()

        let staleThreshold = Int64(seconds * 1_000)
        let now = Self.nowMilliseconds
        return KVMTelemetrySnapshot(
            averageCPUTemperature: Self.markStale(temperature, now: now, threshold: staleThreshold),
            memoryUsage: Self.markStale(memory, now: now, threshold: staleThreshold)
        )
    }

    private func update(_ metric: KVMTelemetryMetric, id: KVMTelemetryMetricID) {
        self.lock.lock()
        switch id {
        case .averageCPUTemperature:
            self.averageCPUTemperature = metric
        case .memoryUsage:
            self.memoryUsage = metric
        }
        self.lock.unlock()

        NotificationCenter.default.post(name: .kvmTelemetryDidChange, object: self)
    }

    private static func markStale(_ metric: KVMTelemetryMetric, now: Int64, threshold: Int64) -> KVMTelemetryMetric {
        guard metric.valid,
              let timestamp = metric.sampledAtMilliseconds,
              now - timestamp <= threshold else {
            return KVMTelemetryMetric(
                value: nil,
                unit: metric.unit,
                valid: false,
                source: metric.source,
                error: metric.sampledAtMilliseconds == nil ? (metric.error ?? "data_unavailable") : "data_stale",
                sampledAtMilliseconds: metric.sampledAtMilliseconds
            )
        }
        return metric
    }

    private static var nowMilliseconds: Int64 {
        Int64((Date().timeIntervalSince1970 * 1_000).rounded())
    }
}
