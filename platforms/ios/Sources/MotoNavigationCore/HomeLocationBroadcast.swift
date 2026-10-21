import Foundation

/// One outbound `HomeLocation` (BLE `0x16`): the revision the device orders
/// messages by, and the address already converted into the frame the device
/// draws in. A missing address is a real message — the phone has to be able to
/// withdraw a home it no longer knows about.
public struct HomeLocationBroadcast: Equatable, Sendable {
    /// Capability bit 9. The phone only sends `0x16` to a peer that advertises
    /// it; an older firmware has never seen the message type.
    public static let capabilityHomeLocation: UInt32 = 1 << 9

    public let revision: UInt32
    public let coordinate: GCJ02Point?

    /// - Parameter address: the saved home in WGS84, or `nil` to clear it.
    ///   Conversion happens here and nowhere else, so a value can never be
    ///   shifted twice on its way to the device.
    public init(revision: UInt32, address: WGS84Point?) {
        self.revision = revision
        coordinate = address.map(ChinaCoordinateTransform.wgs84ToGCJ02)
    }

    public var hasHome: Bool { coordinate != nil }

    /// Revision zero is the device's "never received" marker, so the sequence
    /// skips it. Wrapping is fine: the device only needs ordering, and it
    /// resets its own watermark on every new BLE session.
    public static func nextRevision(after current: UInt32) -> UInt32 {
        let next = current &+ 1
        return next == 0 ? 1 : next
    }

    public func supports(capabilities: UInt32) -> Bool {
        capabilities & Self.capabilityHomeLocation != 0
    }
}
