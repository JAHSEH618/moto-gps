import SwiftUI

/// Home. One job: set a destination. The radar is the hero, the command bar is
/// the only input, everything else is a numbered menu — the shape of a pause
/// screen, not a settings list.
struct HomeScreen: View {
    @ObservedObject var model: AppModel
    @ObservedObject var homeStore: HomeLocationStore
    @Binding var showsDeviceDetails: Bool
    @Binding var showsThemeSettings: Bool
    @Binding var showsGatewaySettings: Bool
    @Binding var showsMapDownloads: Bool
    @Binding var showsDataUse: Bool
    @Binding var showsRaceTelemetry: Bool
    let openSystemSettings: () -> Void

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type
    @Environment(\.motoTheme) private var theme
    @Environment(\.colorSchemeContrast) private var contrast
    @FocusState private var searchFocused: Bool
    /// While true, the next place the rider picks becomes the home address.
    @State private var settingHome = false
    /// The screen width the cover is cut to: the radar takes what the fixed
    /// logo strip leaves, so the circle is as large as the phone allows.
    @State private var screenWidth: CGFloat = 375

    var body: some View {
        ZStack {
            // Measured on the background, which fills the screen whatever the
            // content does, so the cover can never widen its own measurement.
            MotoScreenBackground()
                .onGeometryChange(for: CGFloat.self) { $0.size.width } action: { screenWidth = $0 }
            ScrollView {
                VStack(alignment: .leading, spacing: MotoMetrics.s6) {
                    hero
                    if !model.destinationQuery.isEmpty {
                        searchResults
                    } else {
                        if model.isGatewayConfigured {
                            recentSection
                        } else {
                            gatewayPrompt
                        }
                        actionsSection
                    }
                }
                .padding(.horizontal, MotoMetrics.s4)
                .padding(.bottom, MotoMetrics.s7)
            }
            .scrollDismissesKeyboard(.interactively)
        }
        .safeAreaInset(edge: .top, spacing: 0) { statusBand }
        .toolbar(.hidden, for: .navigationBar)
        .onChange(of: model.destinationQuery) { _, _ in model.destinationQueryDidChange() }
        .onChange(of: model.selectedPlace) { _, place in
            if place != nil { searchFocused = false }
        }
    }

    // MARK: - Status band

    private var statusBand: some View {
        MotoStatusBand(
            linkText: model.deviceReady ? "link" : "no link",
            linkTone: model.deviceReady ? .ok : .abort,
            fixText: fixText,
            clock: clockText
        ) {
            bandButton("slider.horizontal.3", label: "网关设置", identifier: "gateway-settings-toolbar") {
                showsGatewaySettings = true
            }
            bandButton("circle.circle", label: "我的圆屏", identifier: "device-details-button") {
                showsDeviceDetails = true
            }
        }
    }

    private func bandButton(
        _ symbol: String,
        label: String,
        identifier: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(.body, weight: .semibold))
                .foregroundStyle(palette.onSlab)
                .frame(width: MotoMetrics.minTapTarget, height: MotoMetrics.minTapTarget)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityShowsLargeContentViewer {
            Label(label, systemImage: symbol)
        }
        .accessibilityLabel(label)
        .accessibilityValue(label == "我的圆屏" ? model.deviceStatusText : "")
        .accessibilityIdentifier(identifier)
    }

    private var clockText: String {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm"
        return formatter.string(from: Date())
    }

    private var fixText: String {
        switch model.searchLocationStatus {
        case .available: return "fix"
        case .preparing: return "fix…"
        case .permissionDenied: return "no fix"
        case .unavailable: return "no fix"
        }
    }

    // MARK: - Hero

    /// The GTA III cover: framed panels in thick band-colour gutters, each
    /// panel one bold image — the radar, the logo block, the safehouse, the
    /// verb — and the last panel is the one control: the command bar. The
    /// whole first screen is the poster, and on a 375pt phone the field to
    /// type in is still on it.
    private var hero: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            cover
                // Full bleed: the cover runs to both screen edges.
                .padding(.horizontal, -MotoMetrics.s4)

            if let failure = model.placeSearchFailure, model.destinationQuery.isEmpty {
                Text(failure)
                    .font(.system(.subheadline, weight: .semibold))
                    .foregroundStyle(palette.abort)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(.top, MotoMetrics.s3)
    }

    private let coverGutter = MotoMetrics.coverGutter
    private let sideStrip: CGFloat = 132

    /// What the radar panel gets once the gutters and the logo strip are cut.
    private var radarSize: CGFloat {
        let panel = screenWidth - coverGutter * 3 - sideStrip
        return min(max(panel - MotoMetrics.s3 * 2, 150), 280)
    }

    private var cover: some View {
        VStack(spacing: coverGutter) {
            HStack(spacing: coverGutter) {
                coverPanel(palette.surface2) {
                    MotoRadar(
                        size: radarSize,
                        route: [],
                        showsHome: homeStore.isSet,
                        homeOffset: homeRadarOffset
                    )
                    .padding(MotoMetrics.s3)
                }
                VStack(spacing: coverGutter) {
                    coverPanel(palette.slab) { logoBlock }
                    coverPanel(palette.surface) { homeButton }
                }
                // The side column is a fixed strip, as on the cover; the
                // radar panel takes the rest.
                .frame(width: sideStrip)
            }
            // Both columns take the taller one's height, so the gutters line
            // up into one grid.
            .fixedSize(horizontal: false, vertical: true)

            coverPanel(palette.surface, alignment: .leading) {
                wordmark
                    .padding(.horizontal, MotoMetrics.s4)
                    .padding(.vertical, MotoMetrics.s3)
            }

            commandBar
                .dynamicTypeSize(...DynamicTypeSize.accessibility2)
        }
        // The cover is one image: it grows with the reader's size to a point
        // and then holds, instead of reflowing its panels off the screen.
        .dynamicTypeSize(...DynamicTypeSize.accessibility1)
        .padding(coverGutter)
        .background(palette.band)
    }

    private func coverPanel<Content: View>(
        _ fill: Color,
        alignment: Alignment = .center,
        @ViewBuilder content: () -> Content
    ) -> some View {
        content()
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: alignment)
            .background(fill)
    }

    /// The cover's logo block: the name stacked in the wordmark face, white on
    /// black, the way GTA III sets its title in a panel of its own.
    private var logoBlock: some View {
        VStack(alignment: .leading, spacing: -6) {
            Text("MOTO")
                .font(theme.wordmarkFont(size: 44, relativeTo: .largeTitle))
            Text("GPS")
                .font(theme.wordmarkFont(size: 44, relativeTo: .largeTitle))
        }
        .foregroundStyle(palette.onSlab)
        .lineLimit(1)
        .minimumScaleFactor(0.6)
        .dynamicTypeSize(...DynamicTypeSize.accessibility1)
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, MotoMetrics.s3)
        .accessibilityAddTraits(.isHeader)
    }

    /// Home relative to the rider, north-up, 1.5 km to the rim. Equirectangular
    /// is exact enough at radar scale.
    private var homeRadarOffset: CGPoint? {
        guard let home = homeStore.place?.location, let rider = model.riderPoint else {
            return nil
        }
        let metresPerDegree = 111_320.0
        let east = (home.longitudeDeg - rider.longitudeDeg) * metresPerDegree
            * cos(rider.latitudeDeg * .pi / 180)
        let north = (home.latitudeDeg - rider.latitudeDeg) * metresPerDegree
        let range = 1_500.0
        return CGPoint(x: east / range, y: -north / range)
    }

    /// Poster line: one huge word with the Chinese verb beside it and the
    /// claim under the verb, the way the box art sets a title next to its
    /// tagline. Side by side, the panel is half as tall as a stacked one.
    private var wordmark: some View {
        HStack(alignment: .center, spacing: MotoMetrics.s4) {
            MotoWordmark(text: "GO", size: 112, fill: palette.accent, outline: palette.slab)
            VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                Text("出发")
                    .font(.system(.largeTitle, weight: .heavy))
                    .foregroundStyle(palette.ink)
                    .lineLimit(1)
                // The claim is decoration: at sizes where it would truncate,
                // it steps out rather than end in an ellipsis.
                ViewThatFits(in: .horizontal) {
                    MotoLabel(text: "spot · plan · ride", tone: .muted, token: .micro)
                        .fixedSize()
                    EmptyView()
                }
            }
            Spacer(minLength: 0)
        }
    }

    /// Safehouse button. Set it once; after that one tap starts the ride home.
    private var homeButton: some View {
        Button(action: homeButtonTapped) {
            VStack(spacing: MotoMetrics.s1) {
                Image(systemName: homeStore.isSet ? "house.fill" : "house")
                    .font(.system(.title3, weight: .bold))
                    .foregroundStyle(homeStore.isSet ? palette.onAccent : palette.onSlab)
                    .frame(width: 48, height: 48)
                    .background(homeStore.isSet ? palette.accent : palette.slab)
                    .overlay(
                        Rectangle().strokeBorder(
                            homeStore.isSet ? palette.ink : palette.line(contrast: contrast),
                            lineWidth: 2
                        )
                    )
                MotoLabel(
                    text: homeStore.isSet ? "home" : "set home",
                    tone: .muted,
                    token: .micro
                )
                .minimumScaleFactor(0.6)
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(homeStore.isSet ? "回家" : "设置家的地址")
        .accessibilityIdentifier("home-shortcut-button")
    }

    private func homeButtonTapped() {
        if homeStore.isSet {
            settingHome = false
            model.routeHome()
        } else {
            beginSettingHome()
        }
    }

    private func beginSettingHome() {
        settingHome = true
        searchFocused = true
        model.destinationQuery = ""
    }

    /// Solid accent block, the cover's last panel. The only filled surface on
    /// the screen.
    private var commandBar: some View {
        HStack(spacing: MotoMetrics.s3) {
            TextField("", text: $model.destinationQuery, prompt: promptText)
                .font(type.mono(.secondary))
                .foregroundStyle(palette.onAccent)
                .tint(palette.onAccent)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .submitLabel(.search)
                .focused($searchFocused)
                .onSubmit {
                    model.submitDestinationSearch()
                    searchFocused = false
                }
                .accessibilityLabel("搜索目的地")
                .accessibilityIdentifier("destination-search-field")

            if model.isSearchingPlaces {
                ProgressView().controlSize(.mini).tint(palette.onAccent)
            } else if !model.destinationQuery.isEmpty {
                Button {
                    model.clearDestination()
                    searchFocused = true
                } label: {
                    Image(systemName: "xmark")
                        .font(.system(.body, weight: .heavy))
                        .foregroundStyle(palette.onAccent)
                        .frame(width: MotoMetrics.minTapTarget, height: MotoMetrics.minTapTarget)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("清空搜索")
            } else {
                Image(systemName: "magnifyingglass")
                    .font(.system(.title3, weight: .heavy))
                    .foregroundStyle(palette.onAccent)
                    .accessibilityHidden(true)
            }
        }
        .padding(.leading, MotoMetrics.s4)
        .padding(.trailing, MotoMetrics.s3)
        .frame(minHeight: 60)
        .background(palette.accent)
    }

    private var promptText: Text {
        Text(settingHome ? "SET HOME ADDRESS" : "SET DESTINATION")
            .font(type.mono(.secondary))
            .foregroundColor(palette.onAccent.opacity(0.78))
    }

    // MARK: - Search results

    private var searchResults: some View {
        let failure = model.placeSearchFailure
        let retry: (String, () -> Void)? = failure.map { _ in
            ("重新搜索", { model.submitDestinationSearch() })
        }
        return VStack(alignment: .leading, spacing: 0) {
            sectionHeader("RESULTS")
            if model.isSearchingPlaces && model.placeResults.isEmpty {
                loadingRow
            } else if model.placeResults.isEmpty {
                emptyRow(
                    title: failure == nil ? "输入地点名称" : "暂无结果",
                    detail: failure ?? "至少输入两个字，例如“奥体中心”。",
                    action: retry
                )
            } else {
                menuList {
                    ForEach(Array(model.placeResults.enumerated()), id: \.offset) { index, place in
                        menuItem(
                            String(format: "%02d", index + 1), title: place.name,
                            detail: placeSubtitle(place), symbol: "arrow.up.right", tint: nil,
                            identifier: "place-result-\(index)"
                        ) { select(place) }
                    }
                }
            }
            if model.searchLocationStatus == .permissionDenied {
                inlineAction("前往设置开启定位", action: openSystemSettings)
            }
        }
    }

    // MARK: - Recent

    private var recentSection: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: MotoMetrics.s3) {
                sectionHeader("RECENT")
                if !model.recentPlaces.isEmpty {
                    Button("清空") { model.clearRecentPlaces() }
                        .font(.system(.subheadline, weight: .bold))
                        .foregroundStyle(palette.ink)
                        .frame(minWidth: MotoMetrics.minTapTarget, minHeight: MotoMetrics.minTapTarget)
                        .accessibilityLabel("清空最近搜索")
                }
            }
            if model.recentPlaces.isEmpty {
                emptyRow(title: "还没有目的地", detail: "搜索地点或地址，选好路线就出发。", action: nil)
            } else {
                menuList {
                    ForEach(Array(model.recentPlaces.enumerated()), id: \.offset) { index, place in
                        menuItem(
                            String(format: "%02d", index + 1), title: place.name,
                            detail: placeSubtitle(place), symbol: "arrow.up.right", tint: nil,
                            identifier: "recent-place-\(index)"
                        ) { select(place) }
                    }
                }
            }
        }
    }

    // MARK: - Gateway prompt

    private var gatewayPrompt: some View {
        VStack(alignment: .leading, spacing: 0) {
            sectionHeader("GATEWAY")
            Button { showsGatewaySettings = true } label: {
                HStack(spacing: MotoMetrics.s3) {
                    VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                        Text("设置导航网关")
                            .font(.system(.title2, weight: .bold))
                            .foregroundStyle(palette.ink)
                        Text("填写你部署的网关地址后，就可以搜索地点和规划路线。")
                            .font(.system(.subheadline))
                            .foregroundStyle(palette.inkMuted)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    Spacer(minLength: 0)
                    Image(systemName: "arrow.right")
                        .font(.system(.body, weight: .bold))
                        .foregroundStyle(palette.accent)
                }
                .padding(.vertical, MotoMetrics.s4)
                .frame(minHeight: MotoMetrics.minTapTarget)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("gateway-setup-button")
            MotoRule(weight: 2)
        }
    }

    // MARK: - Secondary actions

    private var actionsSection: some View {
        VStack(alignment: .leading, spacing: 0) {
            sectionHeader("MENU")
            menuList {
                menuItem(
                    "01", title: "我的圆屏", detail: model.deviceStatusText,
                    symbol: "circle.circle", tint: model.deviceReady ? palette.ok : nil,
                    identifier: "device-summary-button"
                ) { showsDeviceDetails = true }

                // One level under the menu: the look is chosen often enough
                // (day rides, night rides) that it must not sit inside the
                // device sheet.
                menuItem(
                    "02", title: "主题", detail: model.displayTheme.menuTitle,
                    symbol: "paintpalette", tint: palette.accent,
                    identifier: "theme-menu-button"
                ) { showsThemeSettings = true }

                menuItem(
                    "03", title: "地图与离线下载", detail: "自动加载周边，也能提前保存城市",
                    symbol: "map", tint: nil,
                    identifier: "map-downloads-button"
                ) { showsMapDownloads = true }

                menuItem(
                    "04", title: "演示导航", detail: "先体验一次完整流程",
                    symbol: "play.fill", tint: palette.accent,
                    identifier: "demo-navigation-button"
                ) { model.startDemoNavigation() }

                menuItem(
                    "05", title: "网关设置", detail: model.isGatewayConfigured ? "已配置" : "未配置",
                    symbol: "slider.horizontal.3", tint: nil,
                    identifier: "gateway-settings-button"
                ) { showsGatewaySettings = true }

                menuItem(
                    "06", title: "隐私与数据", detail: "位置、地图与遥测的使用说明",
                    symbol: "hand.raised", tint: nil,
                    identifier: "privacy-data-button"
                ) { showsDataUse = true }

                menuItem(
                    "07",
                    title: "家",
                    detail: homeStore.place?.name ?? "未设置 · 点封面上的房子",
                    symbol: homeStore.isSet ? "house.fill" : "house",
                    tint: homeStore.isSet ? palette.accent : nil,
                    identifier: "home-menu-button"
                ) {
                    if homeStore.isSet {
                        model.routeHome()
                    } else {
                        beginSettingHome()
                    }
                }

                // One level under the menu, like the theme: the race
                // instruments are their own page, not a section of the
                // device sheet.
                menuItem(
                    "08", title: "赛道看板", detail: raceDetail,
                    symbol: "gauge.with.dots.needle.67percent", tint: palette.accent,
                    identifier: "race-telemetry-button"
                ) { showsRaceTelemetry = true }
            }
        }
    }

    /// The recorded best 0-100 when there is one; otherwise what the board
    /// collects.
    private var raceDetail: String {
        _ = model.raceLogRevision
        let summary = model.raceLog.summary(.all, now: Date())
        if let best = summary.bestRun {
            return String(format: "0-%d 最佳 %.2f 秒 · 共 %d 次", best.targetKph, best.seconds, summary.runs.count)
        }
        return "累积 0-100、倾角与 G 值记录"
    }

    // MARK: - Pieces

    /// A poster headline: the word in the wordmark face, then a 2pt ink rule
    /// that runs it out to the margin.
    private func sectionHeader(_ text: String) -> some View {
        HStack(alignment: .center, spacing: MotoMetrics.s3) {
            Text(text.uppercased())
                .font(theme.wordmarkFont(size: 30, relativeTo: .title2))
                .foregroundStyle(palette.ink)
                .lineLimit(1)
                .accessibilityAddTraits(.isHeader)
            Rectangle()
                .fill(palette.ink)
                .frame(height: MotoMetrics.hairline * 2)
        }
        .padding(.top, MotoMetrics.s2)
        .padding(.bottom, MotoMetrics.s2)
        .dynamicTypeSize(...DynamicTypeSize.accessibility2)
    }

    /// Holds pause-menu rows. Each row carries its own inset so its pressed
    /// bar overhangs the text; the list pulls it back out by the same amount.
    private func menuList<Rows: View>(@ViewBuilder rows: () -> Rows) -> some View {
        VStack(spacing: 0) { rows() }
            .padding(.horizontal, -MotoMenuRow.inset)
    }

    private func menuItem(
        _ number: String,
        title: String,
        detail: String,
        symbol: String,
        tint: Color?,
        identifier: String,
        action: @escaping () -> Void
    ) -> some View {
        VStack(spacing: 0) {
            Button(action: action) {
                MotoMenuRow(number: number, title: title, detail: detail, symbol: symbol, symbolTint: tint)
            }
            .buttonStyle(MotoMenuRowStyle())
            .accessibilityIdentifier(identifier)
            MotoRule().padding(.horizontal, MotoMenuRow.inset)
        }
    }

    private var loadingRow: some View {
        HStack(spacing: MotoMetrics.s3) {
            ProgressView().controlSize(.small).tint(palette.accent)
            Text("正在搜索…")
                .font(.system(.subheadline))
                .foregroundStyle(palette.inkMuted)
        }
        .frame(minHeight: 56)
        .accessibilityElement(children: .combine)
    }

    private func emptyRow(title: String, detail: String, action: (String, () -> Void)?) -> some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s2) {
            Text(title)
                .font(.system(.title3, weight: .semibold))
                .foregroundStyle(palette.ink)
            Text(detail)
                .font(.system(.subheadline))
                .foregroundStyle(palette.inkMuted)
                .fixedSize(horizontal: false, vertical: true)
            if let action {
                Button(action.0, action: action.1)
                    .font(.system(.body, weight: .heavy))
                    .foregroundStyle(palette.accent)
                    .frame(minHeight: MotoMetrics.minTapTarget)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.vertical, MotoMetrics.s3)
    }

    private func inlineAction(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack {
                Text(title)
                    .font(.system(.body, weight: .heavy))
                    .foregroundStyle(palette.accent)
                Spacer()
                Image(systemName: "arrow.up.right")
                    .font(.system(.footnote, weight: .heavy))
                    .foregroundStyle(palette.accent)
                    .accessibilityHidden(true)
            }
            .frame(minHeight: MotoMetrics.minTapTarget)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    private func placeSubtitle(_ place: PlaceSearchResult) -> String {
        let area = place.displayArea.isEmpty
            ? [place.city, place.district].filter { !$0.isEmpty }.joined(separator: " · ")
            : place.displayArea
        var parts = [area, place.address].filter { !$0.isEmpty }
        if let distance = place.distanceM, distance >= 0 {
            let distanceText = distance >= 1_000
                ? String(format: "%.1f km", distance / 1_000)
                : "\(Int(distance.rounded())) m"
            parts.append("距你 \(distanceText)")
        }
        return parts.isEmpty ? "查看路线" : parts.joined(separator: " · ")
    }

    private func select(_ place: PlaceSearchResult) {
        searchFocused = false
        if settingHome {
            homeStore.setHome(place)
            settingHome = false
            model.destinationQuery = ""
            model.clearDestination()
        } else {
            model.selectPlace(place)
        }
    }
}
