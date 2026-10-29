import MotoNavigationCore
import SwiftUI

/// The round screen's GTA map ground, for the phone. The values are the
/// handlebar's own (`kGtaDayPalette` / `kGtaNightPalette` in
/// shared/nav_ui/src/moto_nav_ui_gta.cpp, and the classic tree's three road
/// bands for Original), so the two maps read as one map at two sizes. Keep
/// them in step with the C++ when either changes.
struct MotoMapStyle {
    var land: Color
    var block: Color
    /// Motorway, primary, secondary, residential, service, other: the order of
    /// `RoadContextClass` and the gateway's `class` strings.
    var roads: [Color]
    /// Water, green, pedestrian, parking. Original draws no areas.
    var areas: [Color]?
    var casing: Color
    var route: Color
    var routeCasing: Color
    /// The P plate: the street casing's black with the primary road's light.
    var plate: Color
    var plateInk: Color

    /// Design widths of the six classes (DESIGN_V2 §3: 8/7/6/5/3/3 at 360).
    static let roadWidthDesign: [CGFloat] = [8, 7, 6, 5, 3, 3]
    static let roadClasses = ["motorway", "primary", "secondary", "residential", "service", "other"]
    static let areaClasses = ["water", "green", "pedestrian", "parking"]

    static func forTheme(_ theme: MotoThemeName) -> MotoMapStyle {
        switch theme {
        case .day:
            return MotoMapStyle(
                land: Color(hex: 0x4E4E4A), block: Color(hex: 0x40403C),
                roads: [0xF34DAA, 0xEFECE4, 0xD6D2C8, 0xB4B0A6, 0x8E8A82, 0x5E5B55].map { Color(hex: $0) },
                areas: [0x3B5878, 0x4E6A3E, 0x625F57, 0x45484D].map { Color(hex: $0) },
                casing: Color(hex: 0x0A0A0A), route: Color(hex: 0xF08A3C),
                routeCasing: Color(hex: 0x0A0A0A),
                plate: Color(hex: 0x0A0A0A), plateInk: Color(hex: 0xEFECE4)
            )
        case .night:
            return MotoMapStyle(
                land: Color(hex: 0x222222), block: Color(hex: 0x2E2E2E),
                roads: [0xF34DAA, 0xD6D2C8, 0xB4B0A6, 0x8E8A82, 0x5E5B55, 0x4B463E].map { Color(hex: $0) },
                areas: [0x182A40, 0x1F3A22, 0x2C2A26, 0x1C1E22].map { Color(hex: $0) },
                casing: Color(hex: 0x000000), route: Color(hex: 0xF08A3C),
                routeCasing: Color(hex: 0x000000),
                plate: Color(hex: 0x000000), plateInk: Color(hex: 0xD6D2C8)
            )
        case .original:
            // The classic tree: grey bands on black, a white route, no ground.
            return MotoMapStyle(
                land: .black, block: Color(hex: 0x1B1B1B),
                roads: [0x62696D, 0x62696D, 0x42474B, 0x42474B, 0x2B3033, 0x2B3033].map { Color(hex: $0) },
                areas: nil,
                casing: .black, route: .white, routeCasing: .black,
                plate: Color(hex: 0x42474B), plateInk: .white
            )
        }
    }
}

/// A GCJ-02 position, the frame the map tiles, the route window and the
/// round screen all draw in.
struct MotoMapPoint: Equatable {
    var latitudeDeg: Double
    var longitudeDeg: Double
}

/// The phone's live map: a rectangle of real ground, the way the handlebar
/// draws its disc. Everything on it comes from the scene the gateway's tiles
/// gave (OpenStreetMap via Protomaps); with no scene it is bare land, never an
/// invented city.
///
/// Heading-up when `headingDeg` is set (the ride, like the round screen),
/// north-up otherwise (the home cover, where there is no course to follow).
struct MotoLiveMap: View {
    var scene: OfflineMapSceneDocument?
    var center: MotoMapPoint?
    var headingDeg: Double?
    /// Metres from the centre to the nearer edge.
    var halfSpanM: Double
    var route: [MotoMapPoint] = []
    var home: MotoMapPoint?
    /// 0…1, along the bottom edge.
    var progress: Double?
    /// North-up only: which way the rider is going, if known.
    var riderCourseDeg: Double?
    var showBuildings = false

    @Environment(\.motoTheme) private var theme
    @Environment(\.motoPalette) private var palette

    var body: some View {
        Canvas { context, size in
            draw(in: &context, size: size)
        }
        .overlay(alignment: .bottom) {
            if let progress {
                GeometryReader { proxy in
                    Rectangle()
                        .fill(palette.accent)
                        .frame(width: proxy.size.width * min(max(progress, 0), 1), height: 3)
                        .frame(maxHeight: .infinity, alignment: .bottom)
                }
            }
        }
        .clipped()
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("地图")
        .accessibilityValue(accessibilityValue)
    }

    private var accessibilityValue: String {
        guard center != nil else { return "没有定位" }
        guard let scene, !scene.roads.isEmpty else { return "附近道路未加载" }
        var parts = ["附近 \(scene.roads.count) 条道路"]
        if !route.isEmpty { parts.append("显示路线") }
        if let progress { parts.append("已完成 \(Int((progress * 100).rounded()))%") }
        return parts.joined(separator: "，")
    }

    // MARK: - Projection

    private struct Projection {
        let origin: MotoMapPoint
        let size: CGSize
        let pointsPerMetre: Double
        let cosHeading: Double
        let sinHeading: Double
        let metresPerLongitude: Double
        static let metresPerLatitude = 111_320.0

        init(origin: MotoMapPoint, size: CGSize, halfSpanM: Double, headingDeg: Double?) {
            self.origin = origin
            self.size = size
            pointsPerMetre = Double(min(size.width, size.height)) / 2 / max(halfSpanM, 1)
            let heading = (headingDeg ?? 0) * .pi / 180
            cosHeading = cos(heading)
            sinHeading = sin(heading)
            metresPerLongitude = Self.metresPerLatitude * cos(origin.latitudeDeg * .pi / 180)
        }

        /// East/north metres from the origin, rotated so the heading points up.
        func screen(latitudeE6: Int32, longitudeE6: Int32) -> CGPoint {
            screen(latitudeDeg: Double(latitudeE6) / 1_000_000, longitudeDeg: Double(longitudeE6) / 1_000_000)
        }

        func screen(latitudeDeg: Double, longitudeDeg: Double) -> CGPoint {
            let east = (longitudeDeg - origin.longitudeDeg) * metresPerLongitude
            let north = (latitudeDeg - origin.latitudeDeg) * Self.metresPerLatitude
            let right = east * cosHeading - north * sinHeading
            let up = east * sinHeading + north * cosHeading
            return CGPoint(x: Double(size.width) / 2 + right * pointsPerMetre,
                           y: Double(size.height) / 2 - up * pointsPerMetre)
        }
    }

    /// A feature whose points all lie beyond this reach of the centre cannot
    /// touch the rectangle; it is skipped before any path is built.
    private func visible(_ points: [OfflineMapPointE6], _ projection: Projection, reach: Double) -> Bool {
        let metresPerE6Latitude = Projection.metresPerLatitude / 1_000_000
        let metresPerE6Longitude = projection.metresPerLongitude / 1_000_000
        let latitude = Int32((projection.origin.latitudeDeg * 1_000_000).rounded())
        let longitude = Int32((projection.origin.longitudeDeg * 1_000_000).rounded())
        var minimumNorth = Double.infinity, maximumNorth = -Double.infinity
        var minimumEast = Double.infinity, maximumEast = -Double.infinity
        for point in points {
            let north = Double(point.latitudeE6 - latitude) * metresPerE6Latitude
            let east = Double(point.longitudeE6 - longitude) * metresPerE6Longitude
            minimumNorth = min(minimumNorth, north); maximumNorth = max(maximumNorth, north)
            minimumEast = min(minimumEast, east); maximumEast = max(maximumEast, east)
        }
        return maximumNorth >= -reach && minimumNorth <= reach &&
            maximumEast >= -reach && minimumEast <= reach
    }

    private func path(_ points: [OfflineMapPointE6], _ projection: Projection, closed: Bool) -> Path {
        var path = Path()
        for (index, point) in points.enumerated() {
            let at = projection.screen(latitudeE6: point.latitudeE6, longitudeE6: point.longitudeE6)
            if index == 0 { path.move(to: at) } else { path.addLine(to: at) }
        }
        if closed { path.closeSubpath() }
        return path
    }

    // MARK: - Drawing

    private func draw(in context: inout GraphicsContext, size: CGSize) {
        let style = MotoMapStyle.forTheme(theme)
        let bounds = CGRect(origin: .zero, size: size)
        context.fill(Path(bounds), with: .color(style.land))
        guard let center else { return }
        let projection = Projection(origin: center, size: size, halfSpanM: halfSpanM, headingDeg: headingDeg)
        // The rectangle's half-diagonal, in metres, plus a street's width.
        let reach = Double(hypot(size.width, size.height)) / 2 / projection.pointsPerMetre + 20
        // Widths scale with the map the way the round screen's do with its
        // 466 px disc, so a 200 pt map is not drawn with handlebar-thick lines.
        let unit = max(0.45, Double(min(size.width, size.height)) / 360)

        if let scene {
            if let areaColors = style.areas {
                for area in scene.areas where visible(area.points, projection, reach: reach) {
                    guard let index = MotoMapStyle.areaClasses.firstIndex(of: area.areaClass) else { continue }
                    context.fill(path(area.points, projection, closed: true),
                                 with: .color(areaColors[index]), style: FillStyle(eoFill: true))
                }
            }
            if showBuildings {
                var blocks = Path()
                for building in scene.buildings where visible(building.points, projection, reach: reach) {
                    blocks.addPath(path(building.points, projection, closed: true))
                }
                context.fill(blocks, with: .color(style.block))
            }
            // One casing pass under every street, then the streets from the
            // quietest class up, so an arterial crosses over a lane.
            var byClass = Array(repeating: Path(), count: MotoMapStyle.roadClasses.count)
            for road in scene.roads where visible(road.points, projection, reach: reach) {
                let index = MotoMapStyle.roadClasses.firstIndex(of: road.roadClass) ?? 5
                byClass[index].addPath(path(road.points, projection, closed: false))
            }
            let line = { (width: Double) in
                StrokeStyle(lineWidth: width, lineCap: .round, lineJoin: .round)
            }
            for index in byClass.indices.reversed() {
                let width = Double(MotoMapStyle.roadWidthDesign[index]) * unit
                context.stroke(byClass[index], with: .color(style.casing), style: line(width + 4 * unit))
            }
            for index in byClass.indices.reversed() {
                let width = Double(MotoMapStyle.roadWidthDesign[index]) * unit
                context.stroke(byClass[index], with: .color(style.roads[index]), style: line(width))
            }
            drawParking(scene.pois, projection, style: style, unit: unit, in: &context, bounds: bounds)
        }

        if route.count >= 2 {
            var line = Path()
            for (index, point) in route.enumerated() {
                let at = projection.screen(latitudeDeg: point.latitudeDeg, longitudeDeg: point.longitudeDeg)
                if index == 0 { line.move(to: at) } else { line.addLine(to: at) }
            }
            context.stroke(line, with: .color(style.routeCasing),
                           style: StrokeStyle(lineWidth: 15 * unit, lineCap: .round, lineJoin: .round))
            context.stroke(line, with: .color(style.route),
                           style: StrokeStyle(lineWidth: 9 * unit, lineCap: .round, lineJoin: .round))
        }

        if let home {
            drawHome(projection.screen(latitudeDeg: home.latitudeDeg, longitudeDeg: home.longitudeDeg),
                     style: style, unit: unit, in: &context, bounds: bounds)
        }
        drawRider(style: style, unit: unit, in: &context, bounds: bounds)
        drawNorth(unit: unit, in: &context, bounds: bounds)
    }

    private func drawParking(
        _ pois: [OfflineMapPoi], _ projection: Projection, style: MotoMapStyle, unit: Double,
        in context: inout GraphicsContext, bounds: CGRect
    ) {
        let side = 17 * unit
        let font = theme.displayFontName.map { Font.custom($0, size: side * 0.9) } ??
            .system(size: side * 0.8, weight: .heavy)
        let letter = context.resolve(Text("P").font(font).foregroundStyle(style.plateInk))
        for poi in pois where poi.poiClass == "parking" {
            let at = projection.screen(latitudeE6: poi.point.latitudeE6, longitudeE6: poi.point.longitudeE6)
            let plate = CGRect(x: at.x - side / 2, y: at.y - side / 2, width: side, height: side)
            guard bounds.intersects(plate) else { continue }
            context.fill(Path(plate), with: .color(style.plate))
            context.draw(letter, at: at, anchor: .center)
        }
    }

    /// The safehouse, at its real bearing; past the edge it rides the edge,
    /// as GTA pins a far blip to the radar rim.
    private func drawHome(
        _ point: CGPoint, style: MotoMapStyle, unit: Double, in context: inout GraphicsContext, bounds: CGRect
    ) {
        let glyph = 28 * unit
        let inset = bounds.insetBy(dx: glyph * 0.7, dy: glyph * 0.7)
        let pinned = edgePoint(towards: point, in: inset)
        let rect = CGRect(x: pinned.x - glyph / 2, y: pinned.y - glyph / 2, width: glyph, height: glyph)
        let house = HomeGlyph().path(in: rect)
        context.stroke(house, with: .color(style.casing), lineWidth: 3 * unit)
        context.fill(house, with: .color(style.route), style: FillStyle(eoFill: true))
    }

    /// The rider at the centre: a fixed up-arrow when the map turns with the
    /// heading; on a north-up map the arrow turns instead, and with no course
    /// it is a plain plate, because an arrow would claim a direction.
    private func drawRider(style: MotoMapStyle, unit: Double, in context: inout GraphicsContext, bounds: CGRect) {
        let centre = CGPoint(x: bounds.midX, y: bounds.midY)
        let width = 27 * unit, height = 29 * unit
        let rect = CGRect(x: centre.x - width / 2, y: centre.y - height / 2, width: width, height: height)
        let course: Double? = headingDeg != nil ? 0 : riderCourseDeg
        guard let course else {
            let side = 13 * unit
            let plate = Path(CGRect(x: centre.x - side / 2, y: centre.y - side / 2, width: side, height: side))
            context.stroke(plate, with: .color(style.casing), lineWidth: 3 * unit)
            context.fill(plate, with: .color(palette.ink))
            return
        }
        var arrow = RiderArrow().path(in: rect)
        let turn = CGAffineTransform(translationX: centre.x, y: centre.y)
            .rotated(by: course * .pi / 180)
            .translatedBy(x: -centre.x, y: -centre.y)
        arrow = arrow.applying(turn)
        context.stroke(arrow, with: .color(style.casing), lineWidth: 3 * unit)
        context.fill(arrow, with: .color(palette.ink))
    }

    /// The north badge rides the edge at north's bearing; on a north-up map
    /// that is the top edge, middle.
    private func drawNorth(unit: Double, in context: inout GraphicsContext, bounds: CGRect) {
        let side = max(18, 22 * unit)
        let heading = (headingDeg ?? 0) * .pi / 180
        let centre = CGPoint(x: bounds.midX, y: bounds.midY)
        let far = CGPoint(x: centre.x - sin(heading) * 10_000, y: centre.y - cos(heading) * 10_000)
        let at = edgePoint(towards: far, in: bounds.insetBy(dx: side * 0.75, dy: side * 0.75))
        let plate = CGRect(x: at.x - side / 2, y: at.y - side / 2, width: side, height: side)
        context.fill(Path(plate), with: .color(palette.slab))
        let font = theme.displayFontName.map { Font.custom($0, size: side * 0.8) } ??
            .system(size: side * 0.65, weight: .heavy)
        context.draw(Text("N").font(font).foregroundStyle(palette.onSlab), at: at, anchor: .center)
    }

    /// `point` itself when it is inside `rect`, else where the line from the
    /// centre towards it leaves `rect`.
    private func edgePoint(towards point: CGPoint, in rect: CGRect) -> CGPoint {
        if rect.contains(point) { return point }
        let centre = CGPoint(x: rect.midX, y: rect.midY)
        let dx = point.x - centre.x, dy = point.y - centre.y
        let scaleX = dx == 0 ? CGFloat.infinity : (rect.width / 2) / abs(dx)
        let scaleY = dy == 0 ? CGFloat.infinity : (rect.height / 2) / abs(dy)
        let scale = min(scaleX, scaleY)
        return CGPoint(x: centre.x + dx * scale, y: centre.y + dy * scale)
    }
}
