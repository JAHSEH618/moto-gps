import MotoNavigationCore
import SwiftUI

/// Device -> WiFi. The round screen joins the iPhone's Personal Hotspot only
/// when a transfer needs it; BLE keeps carrying navigation either way. iOS
/// does not let an app read the hotspot's name or password, so the rider
/// types them once here and the phone hands them over the bonded BLE link.
struct WifiHotspotView: View {
    @ObservedObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var appearance = MotoAppearance.shared
    /// Read off the theme: this view applies `.motoTheme` itself, so an
    /// environment palette here would be the default one.
    private var palette: MotoPalette { appearance.theme.palette }
    @State private var ssid = ""
    @State private var password = ""
    @State private var formError: String?

    var body: some View {
        MotoPosterSheet(
            plate: .symbol("wifi"),
            title: "WiFi 热点",
            sheetIdentifier: "wifi-hotspot-sheet",
            actions: {
                HStack(spacing: MotoMetrics.s4) {
                    MotoHeaderButton(title: "完成", identifier: "wifi-done-button") {
                        dismiss()
                    }
                    MotoBandButton("保存", identifier: "wifi-save-button", action: save)
                        .disabled(!canSave)
                }
            }
        ) {
            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "01", text: "圆屏状态")
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        MotoRow(title: "WiFi") {
                            MotoLabel(text: statusTitle, tone: statusTone, uppercased: false)
                                .accessibilityIdentifier("wifi-status")
                        }
                        if let status = model.wifiStatus, status.state == .connected {
                            MotoRule()
                            MotoRow(title: "IP 地址") {
                                MotoLabel(text: status.ipv4Address, tone: .ink, uppercased: false)
                                    .textSelection(.enabled)
                                    .accessibilityIdentifier("wifi-ip")
                            }
                            MotoRule()
                            MotoRow(title: "信号") {
                                MotoLabel(text: "\(status.rssiDbm) dBm", tone: .muted, uppercased: false)
                                    .accessibilityIdentifier("wifi-rssi")
                            }
                        }
                    }
                }
                if let detail = statusDetail {
                    MotoProse(text: detail, tone: .muted)
                }
            }

            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "02", text: "个人热点")
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        TextField("热点名称（设置 → 个人热点）", text: $ssid)
                            .textContentType(.none)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .modifier(WifiFieldStyle(palette: palette))
                            .accessibilityIdentifier("wifi-ssid-field")
                        MotoRule()
                        SecureField("热点密码", text: $password)
                            .textContentType(.password)
                            .modifier(WifiFieldStyle(palette: palette))
                            .accessibilityIdentifier("wifi-password-field")
                    }
                }
                if let message = formError ?? saveMessage {
                    MotoProse(text: message, tone: .muted)
                        .accessibilityIdentifier("wifi-save-message")
                }
                MotoProse(
                    text: "在 iPhone 的“设置 → 个人热点”打开“允许其他人加入”和“最大兼容性”。圆屏只支持 2.4 GHz。",
                    tone: .muted
                )
                MotoProse(
                    text: "密码只通过已加密配对的蓝牙发给圆屏，保存在圆屏上，手机不保留。",
                    tone: .muted
                )
            }

            if model.wifiStatus?.deviceHasNetwork == true || !model.wifiHotspotName.isEmpty {
                MotoPanel(padding: 0) {
                    Button("让圆屏忘记这个热点") {
                        formError = nil
                        password = ""
                        model.forgetWifiHotspot()
                    }
                    .buttonStyle(MotoSecondaryButtonStyle())
                    .disabled(!model.deviceSupportsWifi)
                    .accessibilityIdentifier("wifi-forget-button")
                }
            }
        }
        .motoTheme(appearance.theme)
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
        .onAppear { ssid = model.wifiHotspotName }
        .onChange(of: ssid) { _, _ in formError = nil }
        .onChange(of: password) { _, _ in formError = nil }
        .onDisappear { password = "" }
    }

    private var canSave: Bool {
        model.deviceSupportsWifi && !ssid.isEmpty && !password.isEmpty
    }

    private func save() {
        do {
            try model.saveWifiHotspot(ssid: ssid, password: password)
            formError = nil
            password = ""
        } catch let error as WifiHotspot.CredentialError {
            formError = error.message
        } catch {
            formError = error.localizedDescription
        }
    }

    private var saveMessage: String? {
        switch model.wifiSave {
        case .idle: return nil
        case .waitingForDevice: return "正在发送给圆屏…"
        case .stored: return "圆屏已保存热点。"
        case let .refused(message): return message
        }
    }

    private var statusTitle: String {
        guard model.deviceReady else { return "圆屏未连接" }
        guard model.deviceSupportsWifi else { return "固件不支持" }
        return model.wifiStatus?.title ?? "等待圆屏"
    }

    private var statusTone: MotoLabel.Tone {
        switch model.wifiStatus?.state {
        case .connected: .ok
        case .failed: .abort
        default: .muted
        }
    }

    private var statusDetail: String? {
        if model.deviceReady && !model.deviceSupportsWifi {
            return "请更新圆屏固件后再设置 WiFi。"
        }
        return model.wifiStatus?.detail
    }
}

private struct WifiFieldStyle: ViewModifier {
    let palette: MotoPalette

    func body(content: Content) -> some View {
        content
            .font(.system(.body))
            .padding(.horizontal, MotoMetrics.s4)
            .padding(.vertical, MotoMetrics.s3)
            .frame(minHeight: 52)
            .foregroundStyle(palette.ink)
    }
}
