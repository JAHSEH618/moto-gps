import CryptoKit
import MotoNavigationCore
import SwiftUI
import UniformTypeIdentifiers

/// Device -> Firmware. The image never crosses BLE: the phone serves the
/// picked `.bin` over the hotspot it already shares with the round screen, and
/// the device fetches it, checks the SHA-256 shown here, and reboots into it.
/// A build that comes up but never completes a BLE handshake is rolled back by
/// the bootloader, so a bad image cannot leave a dead screen on the bars.
struct FirmwareUpdateView: View {
    @ObservedObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var appearance = MotoAppearance.shared
    private var palette: MotoPalette { appearance.theme.palette }

    @State private var picked: PickedImage?
    @State private var pasteURL = ""
    @State private var pastedDigest = ""
    @State private var formError: String?
    @State private var showingPicker = false
    @State private var releaseMessage: String?
    @State private var isLoadingRelease = false

    private struct PickedImage {
        var data: Data
        var name: String
        var sha256: String
    }

    var body: some View {
        MotoPosterSheet(
            kicker: "OTA 05",
            title: "固件更新",
            sheetIdentifier: "firmware-sheet",
            actions: {
                MotoHeaderButton(title: "完成", identifier: "firmware-done-button") {
                    model.stopFirmwareServer()
                    dismiss()
                }
            }
        ) {
            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "01", text: "圆屏状态")
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
                        if let url = model.firmwareURL {
                            MotoRule()
                            MotoRow(title: "下载地址") {
                                MotoLabel(text: url, tone: .muted,
                                          uppercased: false)
                                    .textSelection(.enabled)
                                    .accessibilityIdentifier("firmware-url")
                            }
                        }
                    }
                }
                if let message = model.firmwareUpdate?.message ?? formError {
                    MotoProse(text: message, tone: .muted)
                        .accessibilityIdentifier("firmware-message")
                }
            }

            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "02", text: "选择镜像")
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        Button("从「文件」里选 .bin") { showingPicker = true }
                            .buttonStyle(MotoSecondaryButtonStyle())
                            .accessibilityIdentifier("firmware-pick-button")
                        if let picked {
                            MotoRule()
                            MotoRow(title: picked.name) {
                                MotoLabel(text: byteCount(UInt32(clamping: picked.data.count)),
                                          tone: .ink, uppercased: false)
                                    .accessibilityIdentifier("firmware-file-size")
                            }
                            MotoRule()
                            MotoRow(title: "SHA-256") {
                                MotoLabel(text: String(picked.sha256.prefix(16)) + "…",
                                          tone: .muted, uppercased: false)
                                    .accessibilityIdentifier("firmware-file-digest")
                            }
                        }
                    }
                }
                MotoBandButton("开始更新", identifier: "firmware-start-button") {
                    startServerImage()
                }
                .disabled(!canStart)
                .opacity(canStart ? 1 : 0.5)
                MotoProse(
                    text: "手机在个人热点（172.20.10.1:8123）上临时提供这个文件；圆屏下载完并校验 SHA-256 才切换启动分区。更新期间请保持亮屏与热点。",
                    tone: .muted
                )
            }

            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "03", text: "或使用发布地址")
                MotoBandButton("获取 doupaw.site 最新固件",
                               identifier: "firmware-release-button") {
                    Task { await loadRelease() }
                }
                .disabled(isLoadingRelease)
                .opacity(isLoadingRelease ? 0.5 : 1)
                if let releaseMessage {
                    MotoProse(text: releaseMessage, tone: .muted)
                        .accessibilityIdentifier("firmware-release-message")
                }
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        TextField("https://…/moto-GPS.bin", text: $pasteURL)
                            .textContentType(.URL)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .keyboardType(.URL)
                            .modifier(FirmwareFieldStyle(palette: palette))
                            .accessibilityIdentifier("firmware-url-field")
                        MotoRule()
                        TextField("SHA-256（64 位十六进制）", text: $pastedDigest)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .modifier(FirmwareFieldStyle(palette: palette))
                            .accessibilityIdentifier("firmware-digest-field")
                    }
                }
                MotoProse(
                    text: "发布地址适合把同一份固件发给多台车，或让圆屏自己从 HTTPS 下载（圆屏内存有限，请先连上热点）。清单里的 SHA-256 会填到下面的摘要栏，更新前请确认版本。",
                    tone: .muted
                )
                MotoBandButton("从地址更新", identifier: "firmware-url-button") {
                    startReleaseURL()
                }
                .disabled(!canStartFromURL)
                .opacity(canStartFromURL ? 1 : 0.5)
            }
        }
        .motoTheme(appearance.theme)
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
        .fileImporter(isPresented: $showingPicker,
                      allowedContentTypes: [.data],
                      allowsMultipleSelection: false) { result in
            importImage(result)
        }
        .onChange(of: picked?.sha256) { _, _ in formError = nil }
        .onDisappear { model.stopFirmwareServer() }
    }

    private var canStart: Bool {
        model.deviceSupportsOta && picked != nil &&
            model.firmwareUpdate?.isRunning != true
    }

    private var canStartFromURL: Bool {
        model.deviceSupportsOta && !pasteURL.isEmpty && !pastedDigest.isEmpty &&
            model.firmwareUpdate?.isRunning != true
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

    private func importImage(_ result: Result<[URL], Error>) {
        formError = nil
        switch result {
        case let .success(urls):
            guard let url = urls.first else { return }
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            do {
                let data = try Data(contentsOf: url)
                let digest = SHA256.hash(data: data)
                    .map { String(format: "%02x", $0) }.joined()
                if let error = FirmwareUpdate.validate(
                    url: FirmwareUpdate.servingURL(host: "0.0.0.0"),
                    sizeBytes: UInt32(clamping: data.count),
                    sha256Hex: digest
                ) {
                    formError = error.message
                    picked = nil
                    return
                }
                picked = PickedImage(data: data, name: url.lastPathComponent,
                                     sha256: digest)
            } catch {
                formError = "读取失败：\(error.localizedDescription)"
            }
        case let .failure(error):
            formError = "选择失败：\(error.localizedDescription)"
        }
    }

    private func startServerImage() {
        formError = nil
        guard let picked else { return }
        do {
            try model.startFirmwareUpdate(image: picked.data,
                                          sha256Hex: picked.sha256,
                                          fileName: picked.name)
        } catch {
            formError = (error as? LocalizedError)?.errorDescription
                ?? error.localizedDescription
        }
    }

    @MainActor
    private func loadRelease() {
        releaseMessage = nil
        formError = nil
        isLoadingRelease = true
        Task {
            defer { isLoadingRelease = false }
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
                pasteURL = value.url
                pastedDigest = value.sha256Hex
                let size = ByteCountFormatter.string(
                    fromByteCount: Int64(value.sizeBytes), countStyle: .file)
                releaseMessage =
                    "已获取 \(value.version) · \(size)。确认后点下面的「从地址更新」。"
            } catch {
                releaseMessage = (error as? LocalizedError)?.errorDescription
                    ?? error.localizedDescription
            }
        }
    }

    private func startReleaseURL() {
        formError = nil
        do {
            try model.startFirmwareUpdate(url: pasteURL.trimmingCharacters(
                                              in: .whitespacesAndNewlines),
                                          sha256Hex: pastedDigest
                                              .trimmingCharacters(in: .whitespacesAndNewlines),
                                          sizeBytes: 0)
        } catch {
            formError = (error as? LocalizedError)?.errorDescription
                ?? error.localizedDescription
        }
    }
}

private struct FirmwareFieldStyle: ViewModifier {
    let palette: MotoPalette

    func body(content: Content) -> some View {
        content
            .font(.system(.body, design: .default, weight: .medium))
            .foregroundStyle(palette.ink)
            .padding(.horizontal, MotoMetrics.s4)
            .padding(.vertical, MotoMetrics.s3)
    }
}
