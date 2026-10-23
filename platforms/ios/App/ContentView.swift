import MotoNavigationCore
import SwiftUI

enum MotoScreen: Hashable {
    case routePreview
    case activeNavigation
}

/// One primary flow: destination → route → ride. Device management is secondary.
struct ContentView: View {
    @ObservedObject var model: AppModel
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.openURL) private var openURL
    @FocusState private var searchFocused: Bool
    @State private var showsDeviceDetails = false
    @State private var showsMapDownloads = false
    @State private var showsDataUse = false
    @State private var showsGatewaySettings = false
    @Environment(\.colorScheme) private var systemColorScheme
    /// Read straight off the theme: this view sits *above* `.motoTheme(...)`,
    /// so an environment lookup here would resolve to the default palette.
    private var palette: MotoPalette { theme.palette }

    /// Vice at night, Heat in daylight. The device owns the choice once the BLE
    /// theme lands; until then the system appearance is the truthful signal.
    private var theme: MotoThemeName {
        systemColorScheme == .dark ? .vice : .heat
    }

    var body: some View {
        NavigationStack(path: navigationPath) {
            HomeScreen(
                model: model,
                homeStore: model.homeStore,
                showsDeviceDetails: $showsDeviceDetails,
                showsGatewaySettings: $showsGatewaySettings,
                showsMapDownloads: $showsMapDownloads,
                showsDataUse: $showsDataUse,
                openSystemSettings: openSystemSettings
            )
            .navigationDestination(for: MotoScreen.self) { screen in
                switch screen {
                case .routePreview: routePreviewScreen
                case .activeNavigation: activeNavigationScreen
                }
            }
        }
        .motoTheme(theme)
        .tint(theme.palette.accent)
        .onChange(of: model.destinationQuery) { _, _ in model.destinationQueryDidChange() }
        .onChange(of: model.selectedPlace) { _, place in
            if place != nil { searchFocused = false }
        }
        .onChange(of: model.isNavigationActive) { _, active in
            if active { searchFocused = false }
        }
        .sheet(isPresented: $showsDeviceDetails) { deviceDetails }
        .sheet(isPresented: $showsMapDownloads) {
            MapDownloadsView(
                store: model.surroundingMap,
                gatewayBaseURL: model.mapGatewayBaseURL,
                route: model.mapDownloadRoute,
                destinationName: model.selectedPlace?.name
            )
        }
        .sheet(isPresented: $showsDataUse) { DataUseView() }
        .sheet(isPresented: $showsGatewaySettings) { GatewaySettingsView(model: model) }
        #if DEBUG
        // Screenshot harness: the device sheet is two taps in, and holding a
        // simulator while a UI test drives taps is a lot of machinery for a
        // layout review.
        .task {
            let arguments = ProcessInfo.processInfo.arguments
            if arguments.contains("--moto-open-device-sheet") {
                showsDeviceDetails = true
            } else if arguments.contains("--moto-open-privacy") {
                showsDataUse = true
            } else if arguments.contains("--moto-open-gateway") {
                showsGatewaySettings = true
            } else if arguments.contains("--moto-open-maps")
                        || arguments.contains("--moto-open-city-search") {
                showsMapDownloads = true
            }
        }
        #endif
    }

    // Derive the stack from the session instead of synchronizing two mutable
    // paths with onChange. A system Back gesture uses the same cleanup as End.
    private var navigationPath: Binding<[MotoScreen]> {
        Binding(
            get: {
                if model.isNavigationActive { return [.activeNavigation] }
                if model.selectedPlace != nil { return [.routePreview] }
                return []
            },
            set: { path in
                if model.isNavigationActive, !path.contains(.activeNavigation) {
                    model.stopNavigation()
                } else if model.selectedPlace != nil, !path.contains(.routePreview) {
                    model.clearDestination()
                }
            }
        )
    }

    // MARK: - Destination search

    private var homeScreen: some View {
        List {
            if !model.isGatewayConfigured {
                Section {
                    Button { showsGatewaySettings = true } label: {
                        Label("设置导航网关", systemImage: "network")
                    }
                    .accessibilityIdentifier("gateway-setup-button")
                } footer: {
                    Text("填写你部署的网关地址后，就可以搜索地点和规划路线。")
                }
            }
            Section { searchField }
            if !model.destinationQuery.isEmpty {
                searchResultsSection
            } else {
                if model.recentPlaces.isEmpty {
                    Section {
                        ContentUnavailableView {
                            Label {
                                Text("选择目的地")
                                    .lineLimit(nil)
                                    .fixedSize(horizontal: false, vertical: true)
                            } icon: { Image(systemName: "map") }
                        } description: {
                            Text("搜索地点或地址，选好路线就出发。")
                        }
                        .padding(.vertical, 12)
                    }
                    .listRowBackground(Color.clear)
                } else {
                    recentPlacesSection
                }
                Section {
                    deviceSummaryButton
                    mapDownloadsButton
                    demoButton
                } footer: {
                    Text("连接圆屏后，导航指引会自动同步。")
                }
                Section {
                    Button { showsGatewaySettings = true } label: {
                        Label("网关设置", systemImage: "network")
                            .foregroundStyle(Color.primary)
                    }
                    .accessibilityIdentifier("gateway-settings-button")
                    Button { showsDataUse = true } label: {
                        Label("隐私与数据", systemImage: "hand.raised")
                            .foregroundStyle(Color.primary)
                    }
                    .accessibilityIdentifier("privacy-data-button")
                }
            }
        }
        .listStyle(.insetGrouped)
        .scrollDismissesKeyboard(.interactively)
        .navigationTitle("出发")
        .toolbar {
            ToolbarItem(placement: .topBarLeading) {
                Button { showsGatewaySettings = true } label: { Image(systemName: "gearshape") }
                    .accessibilityLabel("网关设置")
                    .accessibilityIdentifier("gateway-settings-toolbar")
            }
            ToolbarItem(placement: .topBarTrailing) { deviceToolbarButton }
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("完成") { searchFocused = false }
            }
        }
    }

    private var searchField: some View {
        HStack(spacing: 10) {
            Image(systemName: "magnifyingglass")
                .foregroundStyle(Color.secondary)
                .accessibilityHidden(true)
            TextField("搜索地点或地址", text: $model.destinationQuery)
                .font(.body)
                .accessibilityIdentifier("destination-search-field")
                .accessibilityLabel("搜索目的地")
                .focused($searchFocused)
                .submitLabel(.search)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .onSubmit {
                    model.submitDestinationSearch()
                    searchFocused = false
                }
            if !model.destinationQuery.isEmpty {
                Button {
                    model.clearDestination()
                    searchFocused = true
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundStyle(Color.secondary)
                        .frame(minWidth: 28, minHeight: 44)
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("清空搜索")
            }
        }
        .frame(minHeight: 44)
    }

    private var searchResultsSection: some View {
        Section {
            if model.isSearchingPlaces {
                HStack(spacing: 12) {
                    ProgressView()
                    Text("正在搜索…").foregroundStyle(Color.secondary)
                }
                .frame(minHeight: 56)
                .accessibilityElement(children: .combine)
            } else if !model.placeResults.isEmpty {
                ForEach(Array(model.placeResults.enumerated()), id: \.offset) { index, place in
                    Button { select(place) } label: {
                        placeRow(place, symbol: "mappin.circle.fill")
                    }
                    .accessibilityIdentifier("place-result-\(index)")
                }
            } else {
                ContentUnavailableView {
                    Label {
                        Text(model.placeSearchFailure == nil ? "输入地点名称" : "暂无结果")
                            .lineLimit(nil)
                            .fixedSize(horizontal: false, vertical: true)
                    } icon: { Image(systemName: "magnifyingglass") }
                } description: {
                    Text(model.placeSearchFailure ?? "至少输入两个字，例如“奥体中心”。")
                } actions: {
                    if model.placeSearchFailure != nil {
                        Button("重新搜索", action: model.submitDestinationSearch)
                    }
                }
            }
        } header: {
            Text("搜索结果")
        } footer: {
            VStack(alignment: .leading, spacing: 8) {
                Text(model.searchScopeText)
                if model.searchLocationStatus == .permissionDenied {
                    Button("前往设置开启定位", action: openSystemSettings)
                }
            }
        }
    }

    private var recentPlacesSection: some View {
        Section {
            ForEach(Array(model.recentPlaces.enumerated()), id: \.offset) { index, place in
                Button { select(place) } label: { placeRow(place, symbol: "clock") }
                    .accessibilityIdentifier("recent-place-\(index)")
            }
        } header: {
            HStack {
                Text("最近搜索")
                Spacer()
                Button("清空", action: model.clearRecentPlaces)
                    .textCase(nil)
                    .accessibilityLabel("清空最近搜索")
            }
        }
    }

    private func placeRow(_ place: PlaceSearchResult, symbol: String) -> some View {
        HStack(spacing: 14) {
            Image(systemName: symbol)
                .font(.title3)
                .foregroundStyle(symbol == "clock" ? Color.secondary : .blue)
                .frame(width: 28)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 5) {
                Text(place.name)
                    .font(.body)
                    .foregroundStyle(Color.primary)
                    .fixedSize(horizontal: false, vertical: true)
                Text(placeSubtitle(place))
                    .font(.subheadline)
                    .foregroundStyle(Color.secondary)
                    .lineLimit(dynamicTypeSize.isAccessibilitySize ? nil : 2)
            }
            Spacer(minLength: 0)
            Image(systemName: "chevron.right")
                .font(.caption.weight(.semibold))
                .foregroundStyle(Color(uiColor: .tertiaryLabel))
                .accessibilityHidden(true)
        }
        .padding(.vertical, 7)
        .frame(minHeight: 48)
        .contentShape(Rectangle())
    }

    private func select(_ place: PlaceSearchResult) {
        searchFocused = false
        model.selectPlace(place)
    }

    private var demoButton: some View {
        Button {
            searchFocused = false
            model.startDemoNavigation()
        } label: {
            Label {
                VStack(alignment: .leading, spacing: 4) {
                    Text("演示导航").foregroundStyle(Color.primary)
                    Text("先体验一次导航流程")
                        .font(.subheadline)
                        .foregroundStyle(Color.secondary)
                }
            } icon: {
                Image(systemName: "play.circle").foregroundStyle(.blue)
            }
            .padding(.vertical, 5)
        }
        .accessibilityIdentifier("demo-navigation-button")
    }

    // MARK: - Route selection

    private var mapDownloadsButton: some View {
        Button { showsMapDownloads = true } label: {
            HStack(spacing: MotoMetrics.s3) {
                VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                    Text("地图与离线下载")
                        .font(.system(.body, weight: .semibold))
                        .foregroundStyle(palette.ink)
                    Text("自动加载周边，也能提前保存城市和沿途地图")
                        .font(.system(.footnote))
                        .foregroundStyle(palette.inkMuted)
                }
                Spacer(minLength: MotoMetrics.s2)
                Image(systemName: "map")
                    .font(.system(.body, weight: .semibold))
                    .foregroundStyle(MotoPalette.band)
                    .accessibilityHidden(true)
            }
            .padding(.horizontal, MotoMetrics.s4)
            .frame(minHeight: 60)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("map-downloads-button")
    }

    private var routePreviewScreen: some View {
        MotoPosterPage(
            kicker: "ROUTE 05",
            title: "路线",
            backIdentifier: "destination-change-button",
            onBack: {
                model.clearDestination()
                searchFocused = true
            }
        ) {
            guard let place = model.selectedPlace else { return AnyView(EmptyView()) }
            return AnyView(routePreviewBody(place))
        }
        .safeAreaInset(edge: .bottom, spacing: 0) { navigationActionBar }
    }

    private func routePreviewBody(_ place: PlaceSearchResult) -> some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s6) {
            if model.isPlanningRoutePreview {
                VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                    MotoHeroWord(word: "…", detail: "正在规划路线")
                    MotoProse(text: "获取当前位置与路况…", tone: .muted)
                }
                .accessibilityIdentifier("route-preview-loading")
            } else if model.hasRoutePreview, let selected = selectedRoutePreview {
                // The destination is the small line; the time is the poster.
                MotoHeroWord(
                    word: selected.durationText,
                    detail: "\(place.name) · \(selected.distanceText)"
                )

                RouteOverviewMap(
                    candidates: model.routePreviewCandidates,
                    selectedID: model.selectedRoutePreviewID,
                    origin: model.routePreviewOrigin,
                    destination: place.location
                )
                .frame(height: dynamicTypeSize.isAccessibilitySize ? 200 : 260)
                .accessibilityIdentifier("route-preview-map")
                .accessibilityLabel("前往\(place.name)的路线全览")

                VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                    MotoSectionTitle(index: "01", text: "选择路线")
                    MotoPanel(padding: 0) {
                        VStack(spacing: 0) {
                            ForEach(Array(model.routePreviewCandidates.enumerated()), id: \.element.id) { index, candidate in
                                if index > 0 { MotoRule() }
                                routeOptionRow(candidate, index: index + 1)
                            }
                        }
                    }
                    MotoProse(text: "高德驾车路线 · 预计时间会随路况变化", tone: .muted)
                }

                VStack(alignment: .leading, spacing: MotoMetrics.s3) {
                    MotoSectionTitle(index: "02", text: "地图")
                    MotoPanel(padding: 0) { mapDownloadsButton }
                }
            } else if let failure = model.routePreviewFailure {
                MotoHeroWord(word: "—", detail: "无法规划路线")
                MotoProse(text: failure)
                if model.searchLocationStatus == .permissionDenied {
                    Button("前往设置", action: openSystemSettings)
                        .font(.system(.body, weight: .semibold))
                        .foregroundStyle(palette.ink)
                }
            }

            if let failure = model.navigationFailure {
                MotoProse(text: failure, tone: .muted)
            }
        }
    }

    private var selectedRoutePreview: RoutePreviewCandidate? {
        if let id = model.selectedRoutePreviewID,
           let match = model.routePreviewCandidates.first(where: { $0.id == id })
        {
            return match
        }
        return model.routePreviewCandidates.first
    }

    private func routeOptionRow(_ candidate: RoutePreviewCandidate, index: Int) -> some View {
        let selected = candidate.id == model.selectedRoutePreviewID
        return Button {
            withAnimation(reduceMotion ? nil : .easeInOut(duration: 0.2)) {
                model.selectRoutePreview(candidate.id)
            }
        } label: {
            HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s3) {
                Text(String(format: "%02d", index))
                    .font(.system(.footnote, design: .monospaced).weight(.bold))
                    .foregroundStyle(selected ? MotoPalette.band : palette.inkMuted)
                VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                    Text(candidate.durationText)
                        .font(.system(.title2, weight: .bold))
                        .monospacedDigit()
                        .foregroundStyle(palette.ink)
                    Text("\(candidate.title) · \(candidate.distanceText)")
                        .font(.system(.footnote))
                        .foregroundStyle(palette.inkMuted)
                    Text(candidate.trafficSummary)
                        .font(.system(.caption, weight: .semibold))
                        .foregroundStyle(trafficTint(candidate))
                }
                Spacer(minLength: MotoMetrics.s2)
                if selected {
                    Text("已选")
                        .font(.system(.caption, weight: .bold))
                        .foregroundStyle(MotoPalette.onBand)
                        .padding(.horizontal, MotoMetrics.s2)
                        .padding(.vertical, 3)
                        .background(MotoPalette.band)
                }
            }
            .padding(.horizontal, MotoMetrics.s4)
            .padding(.vertical, MotoMetrics.s3)
            .frame(maxWidth: .infinity, alignment: .leading)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("route-option-\(candidate.ordinal)")
        .accessibilityLabel("\(candidate.title)，\(candidate.durationText)，\(candidate.distanceText)，\(candidate.trafficSummary)")
        .accessibilityAddTraits(selected ? .isSelected : [])
    }

    private func routeDuration(_ candidate: RoutePreviewCandidate) -> some View {
        Text(candidate.durationText)
            .font(.title2.weight(.semibold))
            .monospacedDigit()
            .foregroundStyle(Color.primary)
            .fixedSize(horizontal: false, vertical: true)
    }

    private func routeDistance(_ candidate: RoutePreviewCandidate) -> some View {
        Text(candidate.distanceText)
            .font(.subheadline)
            .monospacedDigit()
            .foregroundStyle(Color.secondary)
    }

    // MARK: - Ride in progress

    private var activeNavigationScreen: some View {
        ZStack {
            MotoScreenBackground()
            ScrollView {
                VStack(spacing: MotoMetrics.s4) {
                    MotoInstructionCard(
                        destination: model.activeDestinationName,
                        instruction: navigationTitle,
                        detail: navigationDetail,
                        symbol: navigationSymbol
                    )

                    HStack(alignment: .top, spacing: MotoMetrics.s4) {
                        MotoRadar(
                            size: MotoMetrics.RadarSize.ride,
                            route: rideMap.route,
                            roads: rideMap.roads,
                            buildings: rideMap.buildings,
                            progress: rideMap.progress,
                            showsSweep: false
                        )
                        VStack(alignment: .leading, spacing: 0) {
                            MotoStatCell(
                                label: "left",
                                value: remainingDistanceValue,
                                unit: remainingDistanceUnit,
                                token: .displayMD,
                                tone: .accent
                            )
                            MotoRule().padding(.vertical, MotoMetrics.s2)
                            MotoStatCell(
                                label: "eta",
                                value: remainingDurationValue,
                                unit: remainingDurationUnit,
                                token: .displayMD
                            )
                            MotoRule().padding(.vertical, MotoMetrics.s2)
                            MotoStatCell(
                                label: "speed",
                                value: speedValue,
                                unit: "km/h",
                                token: .displayMD
                            )
                        }
                        .padding(.top, MotoMetrics.s2)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)

                    VStack(spacing: 0) {
                        rideStatusLine("手机定位", locationStatus, ok: model.navigation.hasUsableFix)
                        MotoRule()
                        rideStatusLine("路况", trafficStatus, ok: true)
                        MotoRule()
                        Button { showsMapDownloads = true } label: {
                            HStack {
                                Text("管理离线地图")
                                    .font(.system(.subheadline, weight: .medium))
                                    .foregroundStyle(palette.ink)
                                Spacer()
                                Image(systemName: "chevron.right")
                                    .font(.system(.caption2, weight: .bold))
                                    .foregroundStyle(palette.inkMuted)
                            }
                            .frame(minHeight: MotoMetrics.minTapTarget)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier("ride-offline-maps-button")
                        MotoRule()
                        SurroundingMapStatusRow(store: model.surroundingMap)
                    }

                }
                .padding(.horizontal, MotoMetrics.s4)
                .padding(.vertical, MotoMetrics.s4)
            }
        }
        .safeAreaInset(edge: .top, spacing: 0) {
            MotoStatusBand(
                linkText: model.deviceReady ? "link" : "no link",
                linkTone: model.deviceReady ? .ok : .abort,
                fixText: model.navigation.hasUsableFix ? "fix" : "no fix"
            ) {
                Button { showsDeviceDetails = true } label: {
                    Image(systemName: "circle.circle")
                        .font(.system(.footnote, weight: .medium))
                        .foregroundStyle(palette.inkMuted)
                        .frame(width: MotoMetrics.minTapTarget, height: MotoMetrics.minTapTarget - 10)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("我的圆屏")
                .accessibilityValue(deviceStatus)
                .accessibilityIdentifier("device-details-button")
                MotoLabel(text: rideClockText, tone: .onSlab, token: .micro)
                    .fixedSize()
                    .layoutPriority(-1)
            }
        }
        .toolbar(.hidden, for: .navigationBar)
        .safeAreaInset(edge: .bottom, spacing: 0) { navigationActionBar }
    }

    private func rideStatusLine(_ title: String, _ value: String, ok: Bool) -> some View {
        HStack {
            Text(title)
                .font(.system(.subheadline))
                .foregroundStyle(palette.inkMuted)
            Spacer()
            HStack(spacing: MotoMetrics.s2) {
                Circle()
                    .fill(ok ? palette.ok : palette.inkMuted)
                    .frame(width: 6, height: 6)
                Text(value)
                    .font(.system(.subheadline, weight: .medium))
                    .foregroundStyle(palette.ink)
            }
        }
        .frame(minHeight: MotoMetrics.minTapTarget)
        .accessibilityElement(children: .combine)
    }

    private func rideMetric(_ title: String, value: String) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(value)
                .font(.title.weight(.semibold))
                .monospacedDigit()
                .fixedSize(horizontal: false, vertical: true)
            Text(title).font(.subheadline).foregroundStyle(Color.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.vertical, 10)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(title)，\(value)")
    }

    private var navigationActionBar: some View {
        Button(action: model.toggleNavigation) {
            HStack(spacing: MotoMetrics.s2) {
                if model.isPlanningRoutePreview {
                    ProgressView().tint(palette.onAccent)
                } else {
                    Image(systemName: model.isNavigationActive ? "stop.fill" : "location.fill")
                        .font(.system(.footnote, weight: .bold))
                }
                Text(model.isNavigationActive ? "结束导航" : model.primaryActionTitle)
                    .font(.system(.headline, weight: .bold))
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(maxWidth: .infinity, minHeight: 56)
            .foregroundStyle(model.isNavigationActive ? palette.bg : palette.onAccent)
            .background(model.isNavigationActive ? palette.abort : palette.accent)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(!model.isNavigationActive && (model.selectedPlace == nil || model.isPlanningRoutePreview))
        .opacity(model.isNavigationActive || model.selectedPlace != nil ? 1 : 0.4)
        .accessibilityIdentifier("primary-navigation-action")
        .padding(.horizontal, MotoMetrics.s4)
        .padding(.top, MotoMetrics.s3)
        .padding(.bottom, MotoMetrics.s2)
        .background(palette.bg)
    }

    // MARK: - Device sheet

    private var displayThemeBinding: Binding<DeviceSettingsTheme> {
        Binding(
            get: { model.displayTheme },
            set: { model.setDisplayTheme($0) }
        )
    }

    private var showBuildingsBinding: Binding<Bool> {
        Binding(
            get: { model.showBuildings },
            set: { model.setShowBuildings($0) }
        )
    }

    private var deviceToolbarButton: some View {
        Button {
            searchFocused = false
            showsDeviceDetails = true
        } label: { Image(systemName: "circle.circle") }
        .accessibilityLabel("我的圆屏")
        .accessibilityValue(deviceStatus)
        .accessibilityIdentifier("device-details-button")
    }

    private var deviceSummaryButton: some View {
        Button {
            searchFocused = false
            showsDeviceDetails = true
        } label: {
            HStack(spacing: 14) {
                Image(systemName: "circle.circle")
                    .font(.title3)
                    .foregroundStyle(.blue)
                    .frame(width: 24)
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 4) {
                    Text("我的圆屏").foregroundStyle(Color.primary)
                    Text(deviceStatus).font(.subheadline).foregroundStyle(Color.secondary)
                }
                Spacer(minLength: 4)
                if model.deviceReady {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                        .accessibilityHidden(true)
                }
                Image(systemName: "chevron.right")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(Color(uiColor: .tertiaryLabel))
                    .accessibilityHidden(true)
            }
            .padding(.vertical, 6)
            .contentShape(Rectangle())
        }
        .accessibilityIdentifier("device-summary-button")
    }

    private var deviceDetails: some View {
        MotoPosterSheet(
            kicker: "DEVICE 01",
            title: "我的圆屏",
            sheetIdentifier: "device-details-sheet",
            dismissTitle: "完成",
            dismissIdentifier: "device-details-done",
            onDismiss: { showsDeviceDetails = false }
        ) {
            VStack(alignment: .leading, spacing: MotoMetrics.s4) {
                HStack(alignment: .top) {
                    Image(systemName: "location.north.circle")
                        .font(.system(size: 40, weight: .light))
                        .foregroundStyle(palette.ink)
                        .accessibilityHidden(true)
                    Spacer(minLength: MotoMetrics.s2)
                    MotoLabel(
                        text: deviceName,
                        tone: .muted,
                        uppercased: false
                    )
                }
                // The one enormous thing on this sheet: whether the bar is
                // talking to the phone. Everything else is a setting.
                MotoHeroWord(
                    word: model.deviceReady ? "ONLINE" : "OFFLINE",
                    detail: deviceDetail
                )
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            VStack(spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "01", text: "连接")
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        MotoRow(title: "状态") {
                            MotoLabel(
                                text: deviceStatus,
                                tone: model.deviceReady ? .ok : .muted,
                                uppercased: false
                            )
                        }
                        MotoRule()
                        Button(connectionActionTitle, action: model.toggleDeviceConnection)
                            .buttonStyle(MotoPrimaryButtonStyle())
                            .accessibilityIdentifier("device-connection-action")
                        if case .bluetoothUnavailable = model.device.connection {
                            MotoRule()
                            Button("打开系统设置", action: openSystemSettings)
                                .buttonStyle(MotoSecondaryButtonStyle())
                        }
                    }
                }
                Text("圆屏保持开机并靠近 iPhone。连接成功后，当前导航会自动同步。")
                    .font(.system(.footnote))
                    .foregroundStyle(palette.inkMuted)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }

            VStack(spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "02", text: "圆屏显示")
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        MotoSegment(
                            options: [
                                (DeviceSettingsTheme.auto, "自动"),
                                (DeviceSettingsTheme.day, "白天"),
                                (DeviceSettingsTheme.night, "夜间"),
                            ],
                            selection: displayThemeBinding,
                            identifierPrefix: "device-theme"
                        )
                        .padding(MotoMetrics.s3)
                        MotoRule()
                        MotoToggleRow(
                            title: "显示建筑轮廓",
                            isOn: showBuildingsBinding,
                            identifier: "device-buildings-toggle"
                        )
                    }
                }
                Text("自动跟随手机所在地的日出与日落。建筑轮廓默认关闭：圆形屏上密集的楼块会挤掉骑行时真正要看清的道路。")
                    .font(.system(.footnote))
                    .foregroundStyle(palette.inkMuted)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }

            VStack(spacing: MotoMetrics.s3) {
                MotoSectionTitle(index: "03", text: "定位")
                MotoPanel(padding: 0) {
                    VStack(spacing: 0) {
                        MotoRow(title: "搜索位置") {
                            MotoLabel(text: searchLocationStatus, tone: .muted, uppercased: false)
                        }
                        if model.searchLocationStatus == .permissionDenied {
                            MotoRule()
                            Button("前往设置开启定位", action: openSystemSettings)
                                .buttonStyle(MotoSecondaryButtonStyle())
                        }
                    }
                }
            }
        }
        .motoTheme(theme)
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
    }

    private func statusRow(_ title: String, symbol: String, value: String, color: Color) -> some View {
        LabeledContent {
            Text(value).foregroundStyle(color).multilineTextAlignment(.trailing)
        } label: {
            Label(title, systemImage: symbol).foregroundStyle(Color.primary)
        }
        .font(.body)
        .padding(.vertical, 4)
    }

    private func failureMessage(_ message: String) -> some View {
        Label(message, systemImage: "exclamationmark.triangle")
            .font(.subheadline)
            .foregroundStyle(Color.secondary)
            .fixedSize(horizontal: false, vertical: true)
            .padding(.vertical, 6)
    }

    private func openSystemSettings() {
        guard let url = URL(string: UIApplication.openSettingsURLString) else { return }
        openURL(url)
    }

    // MARK: - User-facing state

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

    private var deviceStatus: String {
        switch model.device.connection {
        case .connected: return model.deviceReady ? "已连接" : "正在准备"
        case .scanning: return "正在寻找圆屏"
        case .connecting: return "正在连接"
        case .failed: return "连接未成功"
        case .bluetoothUnavailable: return "蓝牙不可用"
        case .idle: return "未连接"
        }
    }

    private var deviceName: String {
        switch model.device.connection {
        case let .connected(name), let .connecting(name): return name
        default: return "MOTO GPS"
        }
    }

    private var deviceDetail: String {
        switch model.device.connection {
        case .connected:
            return model.deviceReady ? "圆屏已就绪，可以接收导航指引。" : "正在准备导航同步，请稍候。"
        case .failed:
            return "暂时无法连接。请确认圆屏已开机并靠近手机，再试一次。"
        case .bluetoothUnavailable:
            return "请开启手机蓝牙，并允许 MOTO GPS 使用蓝牙。"
        case .scanning, .connecting:
            return "请将已开机的圆屏放在手机附近。"
        case .idle:
            return "连接你的圆屏，在车把上查看导航。"
        }
    }

    private var connectionActionTitle: String {
        switch model.device.connection {
        case .connected: return "断开连接"
        case .scanning: return "停止搜索"
        case .connecting: return "取消连接"
        case .failed: return "重新连接"
        case .idle, .bluetoothUnavailable: return "连接圆屏"
        }
    }

    private var searchLocationStatus: String {
        switch model.searchLocationStatus {
        case .available: return "已获取"
        case .preparing: return "获取中"
        case .permissionDenied: return "未获授权"
        case .unavailable: return "暂不可用"
        }
    }

    private var navigationTitle: String {
        if model.navigationFailure != nil { return "导航需要处理" }
        switch model.navigation.stateName {
        case "acquiring": return "正在获取位置"
        case "planning": return "正在规划路线"
        case "rerouting": return "正在重新规划"
        case "arrived": return "已到达目的地"
        case "navigating": return model.isDemoActive ? "正在演示" : "正在导航"
        default: return "准备出发"
        }
    }

    private var navigationDetail: String {
        if let failure = model.navigationFailure { return failure }
        if model.navigation.stateName == "navigating" {
            if model.isDemoActive { return "预览行驶过程与圆屏上的导航指引。" }
            return model.deviceReady
                ? "圆屏已连接，手机可锁屏收好。"
                : "圆屏连接后，导航指引会自动同步。"
        }
        return model.phaseDetail
    }

    private var navigationSymbol: String {
        if model.navigationFailure != nil { return "exclamationmark.triangle" }
        switch model.navigation.stateName {
        case "arrived": return "flag.checkered"
        case "rerouting": return "arrow.triangle.2.circlepath"
        case "navigating": return "location.north.fill"
        default: return "location.magnifyingglass"
        }
    }

    private var hasNavigationEstimate: Bool {
        ["navigating", "rerouting", "arrived"].contains(model.navigation.stateName)
    }

    private struct RideMap {
        var route: [CGPoint] = []
        var roads: [[CGPoint]] = []
        var buildings: [[CGPoint]] = []
        var progress: Double?
    }

    /// Route, streets and footprints share one rider-centred, north-up
    /// projection, so the phone radar is the same picture as the handlebar
    /// instead of a decorative grid with a route dropped on top.
    private var rideMap: RideMap {
        let snapshot = model.navigation
        var map = RideMap()
        if snapshot.totalDistanceM > 0 {
            map.progress = min(max(snapshot.routeProgressM / snapshot.totalDistanceM, 0), 1)
        }
        guard snapshot.hasRouteView else { return map }

        let metresPerDegree = 111_320.0
        let originLat = snapshot.routeViewOriginLatitudeDeg
        let originLon = snapshot.routeViewOriginLongitudeDeg
        let cosLat = cos(originLat * .pi / 180)
        func offset(latitude: Double, longitude: Double) -> CGPoint {
            CGPoint(
                x: (longitude - originLon) * metresPerDegree * cosLat,
                y: (latitude - originLat) * metresPerDegree
            )
        }

        let routeOffsets = snapshot.routeViewPoints.map {
            offset(latitude: $0.latitudeDeg, longitude: $0.longitudeDeg)
        }
        let window = model.surroundingMap.latestWindow
        var reach = routeOffsets.reduce(0.0) { max($0, hypot($1.x, $1.y)) }
        if let window {
            // Never zoom past the fetched window: the streets are only real
            // inside its radius.
            reach = max(reach, Double(window.radiusM) * 0.55)
        }
        guard reach > 1 else { return map }
        // 0.42 keeps the whole window inside the ring instead of clipping it.
        let scale = 0.42 / reach
        func normalise(_ point: CGPoint) -> CGPoint {
            CGPoint(x: 0.5 + point.x * scale, y: 0.5 - point.y * scale)
        }

        map.route = routeOffsets.map(normalise)
        if let window {
            map.roads = window.roads.map { road in
                road.points.map {
                    normalise(offset(
                        latitude: Double($0.latitudeE6) / 1_000_000,
                        longitude: Double($0.longitudeE6) / 1_000_000
                    ))
                }
            }
            map.buildings = window.buildings.map { building in
                building.points.map {
                    normalise(offset(
                        latitude: Double($0.latitudeE6) / 1_000_000,
                        longitude: Double($0.longitudeE6) / 1_000_000
                    ))
                }
            }
        }
        return map
    }

    private var remainingDuration: String {
        hasNavigationEstimate ? model.remainingDurationText : "—"
    }

    private var remainingDistance: String {
        hasNavigationEstimate ? model.remainingDistanceText : "—"
    }

    private var remainingDistanceValue: String {
        guard hasNavigationEstimate else { return "—" }
        let metres = model.navigation.remainingDistanceM
        guard metres.isFinite, metres > 0 else { return "—" }
        return metres >= 1_000
            ? String(format: "%.1f", metres / 1_000)
            : "\(Int(metres.rounded()))"
    }

    private var remainingDistanceUnit: String {
        guard hasNavigationEstimate else { return "" }
        let metres = model.navigation.remainingDistanceM
        guard metres.isFinite, metres > 0 else { return "" }
        return metres >= 1_000 ? "km" : "m"
    }

    private var remainingDurationValue: String {
        guard hasNavigationEstimate else { return "—" }
        let seconds = Int(model.navigation.remainingDurationS)
        guard seconds > 0 else { return "—" }
        return "\(max(1, Int((Double(seconds) / 60).rounded())))"
    }

    private var remainingDurationUnit: String {
        hasNavigationEstimate && model.navigation.remainingDurationS > 0 ? "分钟" : ""
    }

    private var rideClockText: String {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm"
        return formatter.string(from: Date())
    }

    private var speedValue: String {
        let metresPerSecond = model.navigation.speedMPS
        guard metresPerSecond.isFinite, metresPerSecond >= 0 else { return "—" }
        return "\(Int((metresPerSecond * 3.6).rounded()))"
    }

    private var locationStatus: String {
        if model.isDemoActive { return "演示位置" }
        return model.navigation.hasUsableFix ? "已获取" : "获取中"
    }

    private var trafficStatus: String {
        if model.isDemoActive { return "演示中" }
        if model.navigation.trafficRequestInFlight { return "更新中" }
        return model.navigation.networkName == "online" ? "在线" : "离线"
    }

    private func trafficTint(_ candidate: RoutePreviewCandidate) -> Color {
        switch candidate.trafficSummary {
        case "拥堵较多": return .red
        case "部分路段缓行": return .orange
        case "路况顺畅": return .green
        default: return .secondary
        }
    }
}
