import MotoNavigationCore
import SwiftUI

struct GatewaySettingsView: View {
    @ObservedObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.colorScheme) private var colorScheme
    /// Read off the theme: this view applies `.motoTheme` itself, so an
    /// environment palette here would be the default one.
    private var palette: MotoPalette {
        (colorScheme == .dark ? MotoThemeName.vice : .heat).palette
    }
    @State private var address = ""
    @State private var message: String?
    @State private var isChecking = false
    @State private var checkTask: Task<Void, Never>?

    private var theme: MotoThemeName {
        colorScheme == .dark ? .vice : .heat
    }

    var body: some View {
        MotoPosterSheet(
            kicker: "GATEWAY 03",
            title: "网关设置",
            sheetIdentifier: "gateway-settings-sheet",
            actions: {
                HStack(spacing: MotoMetrics.s4) {
                    MotoHeaderButton(title: "取消", identifier: "gateway-cancel-button") {
                        dismiss()
                    }
                    .disabled(model.isUpdatingGateway)
                    MotoBandButton("保存", identifier: "gateway-save-button") {
                        save()
                    }
                    .disabled(model.isNavigationActive || model.isUpdatingGateway || address.isEmpty)
                    .opacity(model.isNavigationActive || model.isUpdatingGateway || address.isEmpty ? 0.5 : 1)
                }
            }
        ) {
            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "01", text: "网关地址")
                MotoPanel(padding: 0) {
                    TextField("https://你的网关地址/", text: $address, axis: .vertical)
                        .font(.system(.body))
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .lineLimit(1...3)
                        .padding(.horizontal, MotoMetrics.s4)
                        .padding(.vertical, MotoMetrics.s3)
                        .frame(minHeight: 52)
                        .foregroundStyle(palette.ink)
                        .accessibilityIdentifier("gateway-address-field")
                        .disabled(model.isUpdatingGateway)
                }
                MotoProse(
                    text: "填写完整的 HTTPS 根地址。例如 https://nav.example.com/moto-gps/api/，不要填高德 Key。",
                    tone: .muted
                )
            }

            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoPanel(padding: 0) {
                    Button {
                        checkConnection()
                    } label: {
                        HStack(spacing: MotoMetrics.s3) {
                            Text("测试连接")
                                .font(.system(.body, weight: .semibold))
                                .foregroundStyle(palette.ink)
                            Spacer(minLength: MotoMetrics.s2)
                            if isChecking {
                                ProgressView().tint(MotoPalette.band)
                            }
                        }
                        .padding(.horizontal, MotoMetrics.s4)
                        .frame(minHeight: 52)
                        .frame(maxWidth: .infinity)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .disabled(isChecking || model.isUpdatingGateway || address.isEmpty)
                    .accessibilityIdentifier("gateway-check-button")
                }
                if let message {
                    MotoProse(text: message, tone: .muted)
                        .accessibilityIdentifier("gateway-status-message")
                }
                MotoProse(
                    text: "连接测试只检查网关状态；真实搜索、导航和地图还需要相应服务可用。",
                    tone: .muted
                )
            }

            VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                MotoProse(text: "保存后立即生效，无需重新安装。更换地址会清空当前搜索并暂停地图下载，已下载的地图仍会保留。")
                MotoProse(text: "地点、路线和定位请求将发送到这个地址，请使用你信任的网关。")
            }

            if model.isNavigationActive {
                MotoPanel {
                    MotoProse(text: "请先结束导航，再更换网关地址。")
                }
            }
        }
        .motoTheme(theme)
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
        .interactiveDismissDisabled(model.isUpdatingGateway)
        .onAppear {
            address = model.isGatewayConfigured ? model.mapGatewayBaseURL.absoluteString : ""
        }
        .onChange(of: address) { _, _ in
            checkTask?.cancel()
            isChecking = false
            message = nil
        }
        .onDisappear { checkTask?.cancel() }
    }

    private func save() {
        checkTask?.cancel()
        isChecking = false
        Task {
            do {
                try await model.saveGatewayAddress(address)
                dismiss()
            } catch { message = error.localizedDescription }
        }
    }

    private func checkConnection() {
        checkTask?.cancel()
        message = nil
        let url: URL
        do { url = try GatewayConfiguration.normalizedURL(address) }
        catch { message = error.localizedDescription; return }
        isChecking = true
        checkTask = Task { @MainActor in
            defer { if !Task.isCancelled { isChecking = false } }
            do {
                var request = URLRequest(url: url.appendingPathComponent("healthz"))
                request.timeoutInterval = 10
                request.cachePolicy = .reloadIgnoringLocalCacheData
                let (bytes, response) = try await URLSession.shared.bytes(for: request)
                defer { bytes.task.cancel() }
                guard let response = response as? HTTPURLResponse, response.statusCode == 200,
                      response.expectedContentLength <= 65_536 else { throw URLError(.badServerResponse) }
                var data = Data()
                for try await byte in bytes {
                    guard data.count < 65_536 else { throw URLError(.dataLengthExceedsMaximum) }
                    data.append(byte)
                }
                struct Health: Decodable {
                    let status: String
                    let ready_for_live_navigation: Bool
                }
                let health = try JSONDecoder().decode(Health.self, from: data)
                guard health.status == "ok" else { throw URLError(.badServerResponse) }
                try Task.checkCancellation()
                message = health.ready_for_live_navigation
                    ? "网关已连接，已启用实时导航配置。"
                    : "网关已连接，但尚未启用实时导航。请检查服务端配置。"
            } catch {
                guard !Task.isCancelled else { return }
                message = "连接失败，请检查地址、HTTPS 证书和网关是否已启动。"
            }
        }
    }
}
