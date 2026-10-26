import Foundation
import MotoNavigationCore

/// The bridge hands over wire units; the core type owns display units. Keeping
/// the conversion in one place stops the App target from inventing its own
/// scaling.
extension AttitudeSample {
    init(_ sample: MotoBLEAttitudeSample) {
        self.init(
            monotonicMilliseconds: sample.monotonicMilliseconds,
            leanCentiDegrees: sample.leanCentiDegrees,
            pitchCentiDegrees: sample.pitchCentiDegrees,
            longitudinalMilliG: sample.longitudinalMilliG,
            lateralMilliG: sample.lateralMilliG
        )
    }
}

extension DevicePerformanceRun {
    /// `nil` for a state this app does not know; the shared decoder already
    /// refuses one, so this only guards a future bridge.
    init?(_ run: MotoBLEPerformanceRun) {
        guard let state = State(rawValue: run.state) else { return nil }
        self.init(
            state: state,
            targetKph: Int(run.targetKph),
            runID: run.runID,
            elapsedMilliseconds: run.elapsedMilliseconds,
            bestMilliseconds: run.bestMilliseconds
        )
    }
}
