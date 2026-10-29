import Foundation
import XCTest
@testable import MotoRideCore

/// The round screen owns the 0-100 clock (PerformanceRun, 0x19) and the phone
/// records what it measured. This detector is the phone's check on that clock:
/// given the board's rules, it must time the shared fixture's samples to the
/// same result as `PerformanceClock` (tests/native/performance_clock_tests.cpp).
final class PerformanceClockFixtureTests: XCTestCase {
    private struct Case {
        var name: String
        var target: Double
        var expect: String
        var expectMs: Int?
        var samples: [(t: TimeInterval, kph: Double?)] = []
    }

    private func fixtureCases() throws -> [Case] {
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()  // MotoRideCoreTests
            .deletingLastPathComponent()  // Tests
            .deletingLastPathComponent()  // ios
            .deletingLastPathComponent()  // platforms
            .deletingLastPathComponent()  // repository
            .appendingPathComponent(
                "shared/protocol/fixtures/performance-runs.v1.txt"
            )
        let text = try String(contentsOf: url, encoding: .utf8)
        var cases: [Case] = []
        for line in text.split(separator: "\n") {
            if line.hasPrefix("#") { continue }
            let words = line.split(separator: " ").map(String.init)
            guard let head = words.first else { continue }
            switch head {
            case "case":
                // case <name> target <kph> expect finished <ms> | aborted <reason>
                let finished = words[5] == "finished"
                cases.append(
                    Case(
                        name: words[1],
                        target: Double(words[3]) ?? 0,
                        expect: finished ? "finished" : "aborted \(words[6])",
                        expectMs: finished ? Int(words[6]) : nil
                    )
                )
            case "end":
                continue
            default:
                let t = (Double(head) ?? 0) / 1000
                cases[cases.count - 1].samples.append((t, Double(words[1])))
            }
        }
        return cases
    }

    /// The board's rules: start at 1 km/h after a 1.5 s standstill, abort on a
    /// 5 km/h drop, after 30 s, or across a 2 s hole in the speed.
    private func boardRulesDetector(target: Double) -> PerformanceRunDetector {
        PerformanceRunDetector(
            targetKph: target,
            startSpeedKph: 1,
            stationaryHoldSeconds: 1.5,
            abortDropKph: 5,
            timeoutSeconds: 30,
            maximumSampleGapSeconds: 2
        )
    }

    private func reasonWord(_ reason: PerformanceRunAbortReason) -> String {
        switch reason {
        case .speedDrop: return "speed_drop"
        case .timeout: return "timeout"
        case .sensorGap: return "speed_lost"
        }
    }

    func testPhoneDetectorAgreesWithTheBoardClock() throws {
        let cases = try fixtureCases()
        XCTAssertGreaterThanOrEqual(cases.count, 5)
        for fixture in cases {
            var detector = boardRulesDetector(target: fixture.target)
            var result = "none"
            var resultMs: Int?
            samples: for sample in fixture.samples {
                // A sample with no speed is a hole in the stream; the detector
                // sees the gap at the next sample that has one.
                guard let kph = sample.kph else { continue }
                for event in detector.ingest(speedKph: kph, timestamp: sample.t) {
                    switch event {
                    case .started:
                        continue
                    case .finished(let run):
                        result = "finished"
                        resultMs = Int((run.duration * 1000).rounded())
                        break samples
                    case .aborted(let reason):
                        result = "aborted \(reasonWord(reason))"
                        break samples
                    }
                }
            }
            XCTAssertEqual(result, fixture.expect, fixture.name)
            if let want = fixture.expectMs, let got = resultMs {
                XCTAssertLessThanOrEqual(abs(got - want), 1, fixture.name)
            }
        }
    }
}
