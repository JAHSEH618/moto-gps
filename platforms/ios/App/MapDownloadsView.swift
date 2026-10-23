import Combine
import Foundation
import MapKit
import MotoNavigationCore
import SwiftUI

struct SurroundingMapStatusRow: View {
    @ObservedObject var store: SurroundingMapStore

    var body: some View {
        LabeledContent {
            Text(store.statusText).foregroundStyle(.secondary).multilineTextAlignment(.trailing)
        } label: { Label("周边地图", systemImage: "map") }
    }
}

struct MapCity: Decodable, Identifiable, Hashable {
    let id: String
    let name: String
    let detail: String
    let boundsWGS84: [Double]

    enum CodingKeys: String, CodingKey {
        case id, name, detail
        case boundsWGS84 = "bounds_wgs84"
    }
}

@MainActor
final class MapCitySearch: ObservableObject {
    @Published var query = ""
    @Published private(set) var cities: [MapCity] = []
    @Published private(set) var isSearching = false
    @Published private(set) var errorMessage: String?
    private let baseURL: URL
    private var task: Task<Void, Never>?
    private var generation = 0

    init(baseURL: URL) { self.baseURL = baseURL }
    deinit { task?.cancel() }

    func search() {
        task?.cancel()
        generation += 1
        let current = generation
        cities = []
        errorMessage = nil
        let keywords = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard (2 ... 80).contains(keywords.count) else { isSearching = false; return }
        isSearching = true
        var components = URLComponents(url: baseURL.appendingPathComponent("v1/map/cities"), resolvingAgainstBaseURL: false)!
        components.queryItems = [URLQueryItem(name: "keywords", value: keywords)]
        let url = components.url!
        task = Task { [weak self] in
            do {
                try await Task.sleep(for: .milliseconds(350))
                var request = URLRequest(url: url)
                request.timeoutInterval = 20
                let (data, response) = try await URLSession.shared.data(for: request)
                try Task.checkCancellation()
                guard let response = response as? HTTPURLResponse,
                      response.statusCode == 200, data.count <= 1_048_576
                else { throw URLError(.badServerResponse) }
                struct Reply: Decodable { let cities: [MapCity] }
                let reply = try JSONDecoder().decode(Reply.self, from: data)
                let valid = reply.cities.prefix(30).filter { city in
                    let b = city.boundsWGS84
                    return b.count == 4 && b.allSatisfy(\.isFinite) && b[0] < b[2] && b[1] < b[3] &&
                        b[0] >= -180 && b[2] <= 180 && b[1] >= -85 && b[3] <= 85
                }
                guard let self, self.generation == current else { return }
                self.cities = Array(valid)
                self.isSearching = false
            } catch {
                guard !Task.isCancelled, let self, self.generation == current else { return }
                self.isSearching = false
                self.errorMessage = "暂时无法搜索城市，请检查网络后重试。"
            }
        }
    }
}

// MARK: - Root

struct MapDownloadsView: View {
    @ObservedObject var store: SurroundingMapStore
    let gatewayBaseURL: URL
    let route: [GCJ02Point]
    let destinationName: String?
    @Environment(\.dismiss) private var dismiss
    @Environment(\.colorScheme) private var systemColorScheme
    @State private var planningRoute = false
    @State private var planningError: String?
    @State private var showsCitySearch = false

    private var theme: MotoThemeName {
        systemColorScheme == .dark ? .vice : .heat
    }

    var body: some View {
        NavigationStack {
            // The destination has to hang off the stack's content, not off the
            // stack: attached outside it, the push silently does nothing.
            sheet
                .navigationDestination(isPresented: $showsCitySearch) {
                    MapCitySearchPage(store: store, baseURL: gatewayBaseURL)
                }
        }
        .motoTheme(theme)
    }

    private var sheet: some View {
        MotoPosterSheet(
            kicker: "MAPS 02",
            title: "地图",
            sheetIdentifier: "map-downloads-sheet",
            dismissTitle: "完成",
            dismissIdentifier: "map-downloads-done",
            onDismiss: { dismiss() }
        ) {
            MotoHeroWord(
                word: "\(store.packs.count)",
                detail: store.packs.isEmpty ? "没有已保存的地图" : "已保存的地图"
            )

            section("01", "自动加载") {
                MotoPanel(padding: 0) {
                    MotoRow(title: "周边地图", subtitle: store.statusText) {
                        Image(systemName: "network")
                            .font(.system(.body, weight: .semibold))
                            .foregroundStyle(MotoPalette.band)
                            .accessibilityHidden(true)
                    }
                }
                MotoProse(
                    text: "骑行时自动加载圆屏周边的道路和建筑。网络不可用时，使用已下载的地图。",
                    tone: .muted
                )
            }

            section("02", "下载") {
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        Button {
                            showsCitySearch = true
                        } label: {
                            posterRow(title: "下载城市地图", detail: "按城市或区县") {
                                Image(systemName: "chevron.right")
                                    .font(.system(.footnote, weight: .bold))
                                    .foregroundStyle(palette.inkMuted)
                            }
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier("map-download-city")

                        if route.count >= 2 {
                            MotoRule()
                            Button(action: downloadRoute) {
                                posterRow(
                                    title: planningRoute ? "正在计算下载范围…" : "下载这条路线周边",
                                    detail: destinationName ?? "当前选择的路线"
                                ) {
                                    Image(systemName: "arrow.triangle.turn.up.right.diamond")
                                        .font(.system(.body, weight: .semibold))
                                        .foregroundStyle(MotoPalette.band)
                                }
                            }
                            .buttonStyle(.plain)
                            .disabled(store.isDownloading || planningRoute)
                            .accessibilityIdentifier("map-download-route")
                        }
                    }
                }
                MotoProse(
                    text: "跨市骑行可先规划路线，只保存沿途约一公里范围的底图。离线底图不包含实时路况，规划新路线仍需要网络。",
                    tone: .muted
                )
                if let planningError {
                    MotoProse(text: planningError, tone: .muted)
                }
                downloadStatus
            }

            section("03", "已保存") {
                MotoPanel(padding: 0) {
                    if store.packs.isEmpty {
                        MotoProse(text: "还没有下载城市或路线地图", tone: .muted)
                            .padding(MotoMetrics.s4)
                    } else {
                        ForEach(Array(store.packs.enumerated()), id: \.element.id) { index, pack in
                            if index > 0 { MotoRule() }
                            packRow(pack)
                        }
                    }
                    if !store.packs.isEmpty { MotoRule() }
                    MotoRow(title: "济南基础地图", subtitle: "随 App 提供") {
                        Image(systemName: "internaldrive")
                            .font(.system(.body))
                            .foregroundStyle(palette.inkMuted)
                            .accessibilityHidden(true)
                    }
                }
            }

            section("04", "缓存") {
                MotoPanel(padding: 0) {
                    Button {
                        store.clearTemporaryCache()
                    } label: {
                        posterRow(title: "清理自动缓存", detail: "保留手动下载的地图") {
                            Image(systemName: "trash")
                                .font(.system(.body))
                                .foregroundStyle(MotoPalette.band)
                        }
                    }
                    .buttonStyle(.plain)
                    .disabled(store.isDownloading)
                }
                MotoProse(
                    text: "自动缓存最多 128 MB，手动下载最多 512 MB；城市与路线共用的数据只保存一份。下载中断后，可在 App 内继续。",
                    tone: .muted
                )
            }

            MotoPanel(padding: 0) {
                VStack(spacing: 0) {
                    MotoLinkRow(title: "© OpenStreetMap contributors", destination: URL(string: "https://www.openstreetmap.org/copyright")!)
                    MotoRule()
                    MotoLinkRow(title: "Protomaps 地图数据", destination: URL(string: "https://protomaps.com")!)
                }
            }
        }
        #if DEBUG
        .onAppear {
            if ProcessInfo.processInfo.arguments.contains("--moto-open-city-search") {
                Task {
                    // Let the stack finish mounting before pushing.
                    try? await Task.sleep(for: .milliseconds(500))
                    showsCitySearch = true
                }
            }
        }
        #endif
    }

    /// Read off the theme rather than the environment: this view is *above*
    /// `.motoTheme`, so an environment lookup would resolve to the default
    /// palette and paint cream ink on cream paper.
    private var palette: MotoPalette { theme.palette }

    private func section<Content: View>(
        _ index: String,
        _ title: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            MotoSectionTitle(index: index, text: title)
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func posterRow<Trailing: View>(
        title: String,
        detail: String?,
        @ViewBuilder trailing: () -> Trailing
    ) -> some View {
        HStack(spacing: MotoMetrics.s3) {
            VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                Text(title)
                    .font(.system(.body, weight: .semibold))
                    .foregroundStyle(palette.ink)
                if let detail {
                    Text(detail)
                        .font(.system(.footnote))
                        .foregroundStyle(palette.inkMuted)
                }
            }
            Spacer(minLength: MotoMetrics.s2)
            trailing()
        }
        .padding(.horizontal, MotoMetrics.s4)
        .frame(minHeight: 60)
        .contentShape(Rectangle())
    }

    private func packRow(_ pack: MapDownloadPack) -> some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s2) {
            HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s3) {
                Text(pack.name)
                    .font(.system(.body, weight: .semibold))
                    .foregroundStyle(palette.ink)
                Spacer(minLength: MotoMetrics.s2)
                MotoLabel(
                    text: pack.isComplete ? "已下载" : "未完成",
                    tone: pack.isComplete ? .ok : .muted,
                    uppercased: false
                )
            }
            Text(pack.detail)
                .font(.system(.footnote))
                .foregroundStyle(palette.inkMuted)
            HStack(spacing: MotoMetrics.s3) {
                MotoLabel(
                    text: ByteCountFormatter.string(fromByteCount: pack.byteCount, countStyle: .file),
                    tone: .muted,
                    uppercased: false
                )
                Spacer(minLength: MotoMetrics.s2)
                if !pack.isComplete && !store.isDownloading {
                    Button("继续下载") { store.resume(pack) }
                        .font(.system(.footnote, weight: .semibold))
                        .foregroundStyle(MotoPalette.band)
                        .buttonStyle(.plain)
                }
                Button("删除") { store.delete(pack) }
                    .font(.system(.footnote, weight: .semibold))
                    .foregroundStyle(palette.abort)
                    .buttonStyle(.plain)
                    .disabled(store.isDownloading)
            }
        }
        .padding(MotoMetrics.s4)
    }

    @ViewBuilder
    private var downloadStatus: some View {
        if store.isDownloading {
            MotoPanel {
                VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                    ProgressView(value: store.downloadProgress)
                        .tint(MotoPalette.band)
                        .accessibilityLabel("地图下载进度")
                    MotoProse(text: store.downloadMessage ?? "正在下载…", tone: .muted)
                    Button("暂停下载") { store.cancelDownload() }
                        .font(.system(.footnote, weight: .semibold))
                        .foregroundStyle(palette.ink)
                        .buttonStyle(.plain)
                }
            }
        } else if let message = store.downloadMessage {
            MotoProse(text: message, tone: .muted)
        }
    }

    private func downloadRoute() {
        planningRoute = true
        planningError = nil
        let points = route
        Task {
            do {
                let tiles = try await Task.detached(priority: .userInitiated) {
                    try MapTilePlanner.corridorGCJ02(points)
                }.value
                store.download(name: "前往\(destinationName ?? "目的地")", detail: "所选路线及沿途约一公里", tiles: tiles)
            } catch { planningError = error.localizedDescription }
            planningRoute = false
        }
    }
}

// MARK: - City search

private struct MapCitySearchPage: View {
    @ObservedObject var store: SurroundingMapStore
    @StateObject private var search: MapCitySearch
    @Environment(\.dismiss) private var dismiss
    @Environment(\.colorScheme) private var colorScheme
    private var palette: MotoPalette {
        (colorScheme == .dark ? MotoThemeName.vice : .heat).palette
    }

    init(store: SurroundingMapStore, baseURL: URL) {
        self.store = store
        _search = StateObject(wrappedValue: MapCitySearch(baseURL: baseURL))
    }

    var body: some View {
        NavigationStack {
            page
        }
        .motoTheme(colorScheme == .dark ? .vice : .heat)
    }

    private var page: some View {
        MotoPosterPage(
            kicker: "MAPS 02",
            title: "下载城市",
            backIdentifier: "map-city-back",
            onBack: { dismiss() }
        ) {
            MotoPanel(padding: 0) {
                TextField(
                    "",
                    text: $search.query,
                    prompt: Text("搜索城市或区县")
                        .foregroundColor(palette.inkMuted)
                )
                    .font(.system(.body))
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .submitLabel(.search)
                    .padding(.horizontal, MotoMetrics.s4)
                    .frame(minHeight: 56)
                    .foregroundStyle(palette.ink)
                    .accessibilityIdentifier("map-city-search-field")
                    .onChange(of: search.query) { _, _ in search.search() }
            }

            if search.isSearching {
                MotoProse(text: "正在搜索城市…", tone: .muted)
            } else if let message = search.errorMessage {
                MotoProse(text: message, tone: .muted)
                Button("重试") { search.search() }
                    .font(.system(.body, weight: .semibold))
                    .foregroundStyle(palette.ink)
            } else if search.cities.isEmpty {
                VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                    MotoHeroWord(
                        word: search.query.count >= 2 ? "—" : "CITY",
                        detail: search.query.count >= 2 ? "未找到城市" : "输入城市或区县名称"
                    )
                    MotoProse(text: "例如「济南」「上海」「历下区」。", tone: .muted)
                }
            } else {
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        ForEach(Array(search.cities.enumerated()), id: \.element.id) { index, city in
                            if index > 0 { MotoRule() }
                            NavigationLink {
                                MapCityDownloadPage(store: store, city: city)
                            } label: {
                                HStack(spacing: MotoMetrics.s3) {
                                    Text(String(format: "%02d", index + 1))
                                        .font(.system(.footnote, design: .monospaced).weight(.bold))
                                        .foregroundStyle(MotoPalette.band)
                                    VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                                        Text(city.name)
                                            .font(.system(.body, weight: .semibold))
                                            .foregroundStyle(palette.ink)
                                        Text(city.detail)
                                            .font(.system(.footnote))
                                            .foregroundStyle(palette.inkMuted)
                                    }
                                    Spacer(minLength: MotoMetrics.s2)
                                    Image(systemName: "chevron.right")
                                        .font(.system(.footnote, weight: .bold))
                                        .foregroundStyle(palette.inkMuted)
                                }
                                .padding(.horizontal, MotoMetrics.s4)
                                .frame(minHeight: 60)
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                            .accessibilityIdentifier("map-city-result-\(city.id)")
                        }
                    }
                }
            }
        }
    }
}

// MARK: - City download

private struct MapCityDownloadPage: View {
    @ObservedObject var store: SurroundingMapStore
    let city: MapCity
    @State private var tiles: [MapTileID] = []
    @State private var errorMessage: String?
    @Environment(\.dismiss) private var dismiss
    @Environment(\.colorScheme) private var colorScheme
    private var palette: MotoPalette {
        (colorScheme == .dark ? MotoThemeName.vice : .heat).palette
    }
    private var type: MotoTypography {
        MotoTypography(theme: colorScheme == .dark ? .vice : .heat)
    }

    var body: some View {
        MotoPosterPage(
            kicker: "MAPS 02",
            title: city.name,
            backIdentifier: "map-city-download-back",
            onBack: { dismiss() }
        ) {
            // The map is content, not decoration: it runs edge to edge with no
            // card and no radius, the way the ride screen's map does.
            Map(initialPosition: .region(region)) {
                MapPolygon(coordinates: corners)
                    .foregroundStyle(MotoPalette.band.opacity(0.16))
                    .stroke(MotoPalette.band, lineWidth: 2)
            }
            .mapStyle(.standard(elevation: .flat, pointsOfInterest: .excludingAll))
            .frame(height: 260)
            .overlay(alignment: .topLeading) {
                Text("下载范围")
                    .font(type.mono(.label))
                    .tracking(MotoMetrics.labelTracking)
                    .foregroundStyle(MotoPalette.onBand)
                    .padding(.horizontal, MotoMetrics.s2)
                    .padding(.vertical, MotoMetrics.s1)
                    .background(MotoPalette.band)
                    .padding(MotoMetrics.s3)
            }
            .accessibilityLabel("\(city.name)离线地图下载范围")

            MotoProse(
                text: "洋红框内的道路和建筑将保存在手机。范围覆盖所选城市或区县，边缘可能包括部分相邻地区。",
                tone: .muted
            )

            MotoPanel(padding: 0) {
                VStack(spacing: 0) {
                    MotoRow(title: "下载区域", subtitle: city.detail) {
                        MotoLabel(text: "\(tiles.count)", tone: .ink, uppercased: false)
                    }
                    MotoRule()
                    Button {
                        store.download(name: city.name, detail: city.detail, tiles: tiles)
                    } label: {
                        Text("下载地图")
                            .font(.system(.body, weight: .bold))
                            .foregroundStyle(MotoPalette.onBand)
                            .frame(maxWidth: .infinity, minHeight: 52)
                            .background(MotoPalette.band)
                    }
                    .buttonStyle(.plain)
                    .disabled(tiles.isEmpty || store.isDownloading)
                    .opacity(tiles.isEmpty || store.isDownloading ? 0.45 : 1)
                    .accessibilityIdentifier("map-city-start-download")
                }
            }

            if let errorMessage {
                MotoProse(text: errorMessage, tone: .muted)
            }
            if store.isDownloading {
                MotoPanel {
                    VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                        ProgressView(value: store.downloadProgress)
                            .tint(MotoPalette.band)
                            .accessibilityLabel("地图下载进度")
                        MotoProse(text: store.downloadMessage ?? "正在下载…", tone: .muted)
                    }
                }
            }
            MotoProse(
                text: "建议连接 Wi-Fi 下载，实际大小会随当地道路和建筑数量变化。范围过大时，可搜索具体区县。",
                tone: .muted
            )
        }
        .task {
            do { tiles = try MapTilePlanner.boundsWGS84(city.boundsWGS84) }
            catch { errorMessage = error.localizedDescription }
        }
    }

    private var region: MKCoordinateRegion {
        let b = city.boundsWGS84
        return MKCoordinateRegion(center: CLLocationCoordinate2D(latitude: (b[1] + b[3]) / 2, longitude: (b[0] + b[2]) / 2),
            span: MKCoordinateSpan(latitudeDelta: (b[3] - b[1]) * 1.15, longitudeDelta: (b[2] - b[0]) * 1.15))
    }

    private var corners: [CLLocationCoordinate2D] {
        let b = city.boundsWGS84
        return [(b[1], b[0]), (b[1], b[2]), (b[3], b[2]), (b[3], b[0])].map {
            CLLocationCoordinate2D(latitude: $0.0, longitude: $0.1)
        }
    }
}
