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
