import XCTest
@testable import MotoNavigationCore

/// The OTA screen's client-side rules. The shared encoder is the authority on
/// the wire; these checks are only what the form may say before it sends, so a
/// rider is never told "started" for a request the device will refuse.
final class FirmwareUpdateTests: XCTestCase {
    func testCapabilityBitIsFifteen() {
        XCTAssertEqual(FirmwareUpdate.capability, 1 << 15)
        XCTAssertTrue(FirmwareUpdate.supports(capabilities: 1 << 15))
        XCTAssertFalse(FirmwareUpdate.supports(capabilities: 1 << 14))
        XCTAssertTrue(FirmwareUpdate.supports(capabilities: (1 << 15) | (1 << 14)))
    }

    func testServingURLUsesTheHotspotAddress() {
        XCTAssertEqual(FirmwareUpdate.servingURL(host: "172.20.10.1"),
                       "http://172.20.10.1:8123/moto-gps.bin")
    }

    func testDigestMustBeAWholeSHA256() {
        let digest = String(repeating: "a1", count: 32)
        XCTAssertNil(FirmwareUpdate.validate(
            url: "https://example.invalid/moto.bin", sizeBytes: 5 << 20,
            sha256Hex: digest))
        XCTAssertEqual(FirmwareUpdate.validate(
            url: "https://example.invalid/moto.bin", sizeBytes: 5 << 20,
            sha256Hex: String(digest.dropLast())), .digestNotHex)
        XCTAssertEqual(FirmwareUpdate.validate(
            url: "https://example.invalid/moto.bin", sizeBytes: 5 << 20,
            sha256Hex: String(repeating: "z", count: 64)), .digestNotHex)
    }

    func testURLMustBeAbsoluteHTTP() {
        let digest = String(repeating: "0f", count: 32)
        XCTAssertNil(FirmwareUpdate.validate(
            url: "http://172.20.10.1:8123/moto-gps.bin", sizeBytes: 0,
            sha256Hex: digest))
        XCTAssertEqual(FirmwareUpdate.validate(
            url: "ftp://example.invalid/moto.bin", sizeBytes: 0,
            sha256Hex: digest), .urlNotHTTP)
        XCTAssertEqual(FirmwareUpdate.validate(
            url: "https://", sizeBytes: 0, sha256Hex: digest), .urlTooShort)
        XCTAssertEqual(FirmwareUpdate.validate(
            url: "https://example.invalid/a b.bin", sizeBytes: 0,
            sha256Hex: digest), .urlNotHTTP)
        // 200 characters is the wire limit; one more and the device refuses.
        let long = "https://example.invalid/" + String(repeating: "a", count: 180)
        XCTAssertEqual(FirmwareUpdate.validate(url: long, sizeBytes: 0,
                                               sha256Hex: digest), .urlTooLong)
    }

    func testSizeIsTheOTASlot() {
        let digest = String(repeating: "0f", count: 32)
        let url = "https://example.invalid/moto.bin"
        XCTAssertNil(FirmwareUpdate.validate(url: url, sizeBytes: 1 << 20,
                                             sha256Hex: digest))
        XCTAssertNil(FirmwareUpdate.validate(url: url, sizeBytes: 7 << 20,
                                             sha256Hex: digest))
        XCTAssertEqual(FirmwareUpdate.validate(url: url, sizeBytes: (1 << 20) - 1,
                                               sha256Hex: digest), .imageTooSmall)
        XCTAssertEqual(FirmwareUpdate.validate(url: url, sizeBytes: (7 << 20) + 1,
                                               sha256Hex: digest), .imageTooLarge)
        // Zero means "not stated" and is always allowed.
        XCTAssertNil(FirmwareUpdate.validate(url: url, sizeBytes: 0,
                                             sha256Hex: digest))
    }

    func testReleaseManifestFillsTheUrlAndDigest() throws {
        let digest = String(repeating: "a1", count: 32)
        let json = """
        {
          "schema": "moto-gps-firmware/1",
          "version": "v0.1.0-12-gabc1234",
          "file": "moto-gps.bin",
          "url": "https://doupaw.site/firmware/moto-gps.bin",
          "sha256": "\(digest)",
          "size_bytes": 6029312,
          "built_at": "2026-10-28T00:00:00+09:00",
          "git_commit": "abc1234"
        }
        """
        let release = try FirmwareRelease.decode(Data(json.utf8))
        XCTAssertEqual(release.version, "v0.1.0-12-gabc1234")
        XCTAssertEqual(release.url, "https://doupaw.site/firmware/moto-gps.bin")
        XCTAssertEqual(release.sha256Hex, digest)
        XCTAssertEqual(release.sizeBytes, 6029312)
        XCTAssertEqual(release.gitCommit, "abc1234")
    }

    /// The device's own build decides whether a release is installed; the
    /// version this phone last flashed is only the fallback for a device that
    /// does not report one.
    func testAReleaseIsInstalledWhenTheDeviceRunsItsCommit() {
        let digest = String(repeating: "a1", count: 32)
        let release = FirmwareRelease(
            version: "20261028-27dc871",
            url: "https://example.invalid/moto-gps.bin",
            sha256Hex: digest, sizeBytes: 6 << 20, gitCommit: "27dc871")
        let running = DeviceFirmware(version: "0.2.1", build: "27dc871")
        XCTAssertTrue(release.isInstalled(on: running, lastFlashedVersion: ""))
        // A USB build of the same commit with local edits is not the release,
        // and neither is an older build the phone happens to have flashed.
        let edited = DeviceFirmware(version: "0.2.1", build: "27dc871-dirty")
        XCTAssertFalse(release.isInstalled(on: edited, lastFlashedVersion: ""))
        let older = DeviceFirmware(version: "0.2.1", build: "686572e")
        XCTAssertFalse(release.isInstalled(
            on: older, lastFlashedVersion: "20261028-27dc871"))
        // No report from the device: fall back to what this phone flashed.
        XCTAssertTrue(release.isInstalled(
            on: nil, lastFlashedVersion: "20261028-27dc871"))
        XCTAssertFalse(release.isInstalled(on: nil, lastFlashedVersion: ""))
        // No commit in the manifest: the version is all there is to compare.
        let bare = FirmwareRelease(
            version: "20261028-27dc871",
            url: "https://example.invalid/moto-gps.bin",
            sha256Hex: digest, sizeBytes: 6 << 20)
        XCTAssertFalse(bare.isInstalled(on: running, lastFlashedVersion: ""))
    }

    func testReleaseManifestIsValidatedBeforeItReachesTheDevice() {
        func manifest(schema: String = "moto-gps-firmware/1",
                      file: String = "moto-gps.bin",
                      url: String = "https://doupaw.site/firmware/moto-gps.bin",
                      digest: String = String(repeating: "a1", count: 32))
            -> Data {
            Data("""
            {
              "schema": "\(schema)",
              "version": "v1",
              "file": "\(file)",
              "url": "\(url)",
              "sha256": "\(digest)",
              "size_bytes": 6029312
            }
            """.utf8)
        }

        XCTAssertThrowsError(try FirmwareRelease.decode(manifest(schema: "other/1"))) {
            XCTAssertEqual($0 as? FirmwareRelease.DecodeError, .unsupportedSchema)
        }
        XCTAssertThrowsError(try FirmwareRelease.decode(
            manifest(file: "other.bin"))) { error in
            guard case .invalid = error as? FirmwareRelease.DecodeError else {
                return XCTFail("expected .invalid, got \(error)")
            }
        }
        XCTAssertThrowsError(try FirmwareRelease.decode(
            manifest(url: "http://doupaw.site/firmware/moto-gps.bin",
                     digest: String(repeating: "z", count: 64)))) { error in
            guard case .invalid = error as? FirmwareRelease.DecodeError else {
                return XCTFail("expected .invalid, got \(error)")
            }
        }
    }

    func testStatusTitlesAndFailures() {
        let running = FirmwareUpdateStatus(state: .downloading, failure: .none,
                                           progressPercent: 42, bytesReceived: 1000)
        XCTAssertEqual(running.title, "传输中 42%")
        XCTAssertTrue(running.isRunning)
        XCTAssertNil(running.message)

        let mismatch = FirmwareUpdateStatus(state: .failed, failure: .hashMismatch,
                                             progressPercent: 0, bytesReceived: 10)
        XCTAssertEqual(mismatch.state, .failed)
        XCTAssertFalse(mismatch.isRunning)
        XCTAssertEqual(mismatch.title, "更新失败")
        XCTAssertNotNil(mismatch.message)

        let ready = FirmwareUpdateStatus(state: .ready, failure: .none,
                                         progressPercent: 100, bytesReceived: 5)
        XCTAssertFalse(ready.isRunning)

        // Every failure the wire can carry has a sentence the rider can act on.
        for raw in UInt8(1)...UInt8(9) {
            let failure = FirmwareUpdateStatus.Failure(rawValue: raw)
            XCTAssertNotNil(failure)
            let status = FirmwareUpdateStatus(state: .failed,
                                              failure: failure ?? .network,
                                              progressPercent: 0, bytesReceived: 0)
            XCTAssertNotNil(status.message)
        }
    }

    /// The outcomes the device never reports still end the update with a
    /// reason, and a refusal says why rather than one sentence for all.
    func testPhoneSideProblemsExplainThemselves() {
        let unbonded = FirmwareUpdateProblem.refused(ackStatus: 2)
        XCTAssertTrue(unbonded.message.contains("配对"))
        XCTAssertNotEqual(unbonded.message,
                          FirmwareUpdateProblem.refused(ackStatus: 3).message)
        XCTAssertTrue(FirmwareUpdateProblem.refused(ackStatus: 9).message
            .contains("9"))
        XCTAssertFalse(FirmwareUpdateProblem.linkLost.message.isEmpty)
        // Three 15 s waits in a row is the longest the device stays silent
        // while it still works; the watchdog must not fire inside them.
        XCTAssertGreaterThan(FirmwareUpdateProblem.stallSeconds, 3 * 15)
        XCTAssertTrue(FirmwareUpdateProblem.stalled.message
            .contains("\(Int(FirmwareUpdateProblem.stallSeconds))"))
    }
}
