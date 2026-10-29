import MotoNavigationCore
import SwiftUI

/// Device -> Firmware. The image never crosses BLE: the phone reads the
/// published manifest, and the device fetches the image over the hotspot it
/// already shares with the round screen, checks the SHA-256, and reboots into
/// it. A build that comes up but never completes a BLE handshake is rolled
/// back by the bootloader, so a bad image cannot leave a dead screen on the
/// bars.
///
/// Two steps, nothing else: check what doupaw.site publishes, and only when
/// that is not what the round screen runs does the update section appear at
/// all.
struct FirmwareUpdateView: View {
    @ObservedObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var appearance = MotoAppearance.shared

    @State private var release: FirmwareRelease?
    @State private var releaseChecked = false
    @State private var isLoadingRelease = false
    @State private var releaseMessage: String?
    @State private var formError: String?
    @State private var pendingVersion: String?
    // What this phone last flashed into the device. A round screen that
    // reports its build (FirmwareInfo) is judged on that instead; this is the
    // fallback for firmware that predates the report.
    @AppStorage("firmware.installedVersion") private var installedVersion = ""

    var body: some View {
        MotoPosterSheet(
            plate: .symbol("cpu"),
            title: "固件更新",
            sheetIdentifier: "firmware-sheet",
            actions: {
                MotoBandButton("完成", identifier: "firmware-done-button") {
                    model.stopFirmwareServer()
                    dismiss()
                }
            }
        ) {
            checkSection
            if let release, isUpdateAvailable, model.deviceSupportsOta {
                updateSection(release)
            }
        }
        .motoTheme(appearance.theme)
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
        .onChange(of: model.firmwareUpdate?.state) { _, state in
            // The device only reports Ready after the digest matched and the
            // slot was written, so that is the one moment this phone can
            // claim a version is on the round screen.
            guard state == .ready, let pendingVersion else { return }
            installedVersion = pendingVersion
            self.pendingVersion = nil
        }
        .onDisappear { model.stopFirmwareServer() }
    }

    // MARK: - 01 检查更新

    private var checkSection: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            MotoSectionTitle(index: "01", text: "检查更新")
            MotoPanel(padding: 0) {
                VStack(spacing: 0) {
                    MotoRow(title: "结果") {
                        MotoLabel(text: checkStateText, tone: checkTone,
                                  uppercased: false)
                            .accessibilityIdentifier("firmware-release-state")
                    }
                    if let device = model.deviceFirmware {
                        MotoRule()
                        MotoRow(title: "圆屏") {
                            MotoLabel(text: "\(device.version) · \(device.build)",
                                      tone: .ink, uppercased: false)
                                .textSelection(.enabled)
                                .accessibilityIdentifier("firmware-device-build")
                        }
                    }
                    if let release {
                        MotoRule()
                        MotoRow(title: "版本") {
                            MotoLabel(text: release.version, tone: .ink,
                                      uppercased: false)
                                .textSelection(.enabled)
                                .accessibilityIdentifier("firmware-release-version")
                        }
                        MotoRule()
                        MotoRow(title: "大小") {
                            MotoLabel(text: byteCount(release.sizeBytes),
                                      tone: .ink, uppercased: false)
                                .accessibilityIdentifier("firmware-release-size")
                        }
                    }
                }
            }
            Button("检查更新") {
                Task { await loadRelease() }
            }
            .buttonStyle(MotoPrimaryButtonStyle())
            .disabled(isLoadingRelease)
            .accessibilityIdentifier("firmware-check-button")
            if let releaseMessage {
                MotoProse(text: releaseMessage, tone: .muted)
                    .accessibilityIdentifier("firmware-release-message")
            }
            // A round screen flashed before the OTA capability landed cannot
            // be updated over the hotspot at all, so say why instead of
            // offering a button that would be refused.
            if model.deviceReady && !model.deviceSupportsOta {
                MotoProse(
                    text: "圆屏当前固件不支持 OTA，需要先用 USB 刷一次带 OTA 的固件，之后这一页才能用。",
                    tone: .muted
                )
                .accessibilityIdentifier("firmware-ota-unsupported")
            }
            MotoProse(text: checkCaption, tone: .muted)
        }
    }

    // MARK: - 02 更新圆屏

    private func updateSection(_ release: FirmwareRelease) -> some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            MotoSectionTitle(index: "02", text: "更新圆屏")
            MotoPanel(padding: 0) {
                VStack(spacing: 0) {
                    MotoRow(title: "更新") {
                        MotoLabel(text: statusTitle, tone: statusTone,
                                  uppercased: false)
                            .accessibilityIdentifier("firmware-status")
                    }
                    if let status = model.firmwareUpdate,
                       status.state == .downloading || status.state == .verifying {
                        MotoRule()
                        MotoRow(title: "已写入") {
                            MotoLabel(text: byteCount(status.bytesReceived),
                                      tone: .ink, uppercased: false)
                                .accessibilityIdentifier("firmware-bytes")
                        }
                    }
                }
            }
            Button("更新圆屏") {
                startUpdate(release)
            }
            .buttonStyle(MotoPrimaryButtonStyle())
            .disabled(!canUpdate)
            .accessibilityIdentifier("firmware-update-button")
            if let message = model.firmwareUpdate?.message
                ?? model.firmwareProblem?.message ?? formError {
                MotoProse(text: message, tone: .muted)
                    .accessibilityIdentifier("firmware-message")
            }
            MotoProse(
                text: "圆屏会通过手机热点下载 \(release.version)，校验 SHA-256 后才切换启动分区。更新期间请保持亮屏与热点。",
                tone: .muted
            )
        }
    }

    // MARK: - State

    private var canUpdate: Bool {
        model.deviceSupportsOta && model.firmwareUpdate?.isRunning != true
    }

    /// True when the published release is not what the round screen runs:
    /// its reported build against the manifest's commit, or, for firmware
    /// that does not report one, the version this phone last flashed. With
    /// nothing to go on the answer is "yes".
    private var isUpdateAvailable: Bool {
        guard let release else { return false }
        return !release.isInstalled(on: model.deviceFirmware,
                                    lastFlashedVersion: installedVersion)
    }

    private var checkStateText: String {
        if isLoadingRelease { return "检查中…" }
        if release == nil { return releaseChecked ? "检查失败" : "未检查" }
        return isUpdateAvailable ? "有新固件" : "已是最新"
    }

    private var checkTone: MotoLabel.Tone {
        if isLoadingRelease { return .muted }
        if release == nil { return releaseChecked ? .abort : .muted }
        return isUpdateAvailable ? .ink : .ok
    }

    private var checkCaption: String {
        guard let release else {
            return releaseChecked
                ? "没读到 doupaw.site 上的清单。确认网络后点「检查更新」重试。"
                : "点「检查更新」读 doupaw.site 上发布的固件版本。"
        }
        if let device = model.deviceFirmware, release.gitCommit != nil {
            return isUpdateAvailable
                ? "圆屏现在是构建 \(device.build)，最新发布是 \(release.version)。"
                : "圆屏运行的就是 \(release.version)，没有更新的发布。"
        }
        if !isUpdateAvailable {
            return "这台手机上次给圆屏刷的就是 \(release.version)，没有更新的发布。"
        }
        if installedVersion.isEmpty {
            return "还没有记录圆屏的版本，所以把 \(release.version) 当作新固件。"
        }
        return "这台手机上次给圆屏刷的是 \(installedVersion)，最新发布是 \(release.version)。"
    }

    private var statusTitle: String {
        guard model.deviceReady else { return "--" }
        guard model.deviceSupportsOta else { return "固件不支持" }
        return model.firmwareUpdate?.title ?? "未开始"
    }

    private var statusTone: MotoLabel.Tone {
        switch model.firmwareUpdate?.state {
        case .ready: .ok
        case .failed: .abort
        case .downloading, .verifying: .ink
        default: .muted
        }
    }

    private func byteCount(_ bytes: UInt32) -> String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        return formatter.string(fromByteCount: Int64(bytes))
    }

    // MARK: - Actions

    @MainActor
    private func loadRelease() {
        releaseMessage = nil
        formError = nil
        release = nil
        isLoadingRelease = true
        Task {
            defer {
                isLoadingRelease = false
                releaseChecked = true
            }
            do {
                var request = URLRequest(url: FirmwareRelease.defaultManifestURL)
                request.cachePolicy = .reloadIgnoringLocalCacheData
                request.timeoutInterval = 15
                let (data, response) = try await URLSession.shared.data(for: request)
                if let http = response as? HTTPURLResponse, http.statusCode != 200 {
                    throw FirmwareRelease.DecodeError.invalid(
                        "官网返回 HTTP \(http.statusCode)，请稍后再试。")
                }
                let value = try FirmwareRelease.decode(data)
                release = value
                releaseMessage = value.isInstalled(on: model.deviceFirmware,
                                                   lastFlashedVersion: installedVersion)
                    ? "已是最新：\(value.version)。"
                    : "发现新固件 \(value.version)。"
            } catch {
                releaseMessage = (error as? LocalizedError)?.errorDescription
                    ?? error.localizedDescription
            }
        }
    }

    private func startUpdate(_ release: FirmwareRelease) {
        formError = nil
        pendingVersion = release.version
        do {
            try model.startFirmwareUpdate(url: release.url,
                                          sha256Hex: release.sha256Hex,
                                          sizeBytes: release.sizeBytes)
        } catch {
            pendingVersion = nil
            formError = (error as? LocalizedError)?.errorDescription
                ?? error.localizedDescription
        }
    }
}
