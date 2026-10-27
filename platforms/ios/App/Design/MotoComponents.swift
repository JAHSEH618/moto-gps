import SwiftUI
import UIKit

// MARK: - Hairlines and panels

/// 1px rule. Day uses a heavier line than night; Increase Contrast thickens both.
struct MotoRule: View {
    var axis: Axis = .horizontal
    var weight: CGFloat = MotoMetrics.hairline
    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        Rectangle()
            .fill(palette.line(contrast: contrast))
            .frame(
                width: axis == .vertical ? weight : nil,
                height: axis == .horizontal ? weight : nil
            )
    }
}

/// A square etched plane. Radius 0, 1px border, never a shadow.
struct MotoPanel<Content: View>: View {
    var padding: CGFloat = MotoMetrics.s4
    @ViewBuilder var content: () -> Content
    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        content()
            .padding(padding)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(palette.surface)
            .overlay(
                Rectangle()
                    .strokeBorder(palette.line(contrast: contrast), lineWidth: MotoMetrics.hairline)
            )
    }
}

// MARK: - Labels

/// Caps mono label. Tracking is part of the spec, not a per-call decision.
struct MotoLabel: View {
    let text: String
    var tone: Tone = .muted
    var token: MotoText = .label
    /// Units stay lowercase; system labels are set in caps.
    var uppercased = true
    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast
    @Environment(\.motoType) private var type

    enum Tone { case muted, ink, accent, link, ok, abort, onSlab }

    var body: some View {
        Text(uppercased ? text.uppercased() : text)
            .font(type.mono(token))
            .tracking(token == .micro ? MotoMetrics.microTracking : MotoMetrics.labelTracking)
            .foregroundStyle(color)
            .lineLimit(1)
    }

    private var color: Color {
        switch tone {
        case .muted: return palette.inkMuted(contrast: contrast)
        case .ink: return palette.ink
        case .accent: return palette.accent
        case .link: return palette.link
        case .ok: return palette.ok
        case .abort: return palette.abort
        case .onSlab: return palette.onSlab
        }
    }
}

/// Large condensed numeral. Tabular so widths never jump while the value changes.
struct MotoNumeral: View {
    let value: String
    var token: MotoText = .displayMD
    var tone: Tone = .ink
    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type
    @Environment(\.motoTheme) private var theme

    enum Tone { case ink, accent, muted }

    var body: some View {
        Text(value)
            .font(type.display(token))
            .tracking(MotoMetrics.displayTracking)
            .monospacedDigit()
            .foregroundStyle(color)
            .lineLimit(1)
            .minimumScaleFactor(0.6)
            .motoHudShadow(theme.hudShadow)
            // A 76pt numeral must not reflow the screen at accessibility sizes.
            .dynamicTypeSize(...DynamicTypeSize.accessibility1)
    }

    private var color: Color {
        switch tone {
        case .ink: return palette.hud ?? palette.ink
        case .accent: return palette.accent
        case .muted: return palette.inkMuted
        }
    }
}

/// Label above, numeral below. Separated from its neighbours by a 1px rule.
struct MotoStatCell: View {
    let label: String
    let value: String
    var unit: String?
    var token: MotoText = .displayMD
    var tone: MotoNumeral.Tone = .ink

    var body: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s2) {
            MotoLabel(text: label, tone: .muted, token: .micro)
            HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s1) {
                MotoNumeral(value: value, token: token, tone: tone)
                if let unit {
                    MotoLabel(text: unit, tone: .muted, token: .micro, uppercased: false)
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
    }
}

// MARK: - Buttons

struct MotoPrimaryButtonStyle: ButtonStyle {
    @Environment(\.motoPalette) private var palette

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(.body, weight: .bold))
            .foregroundStyle(palette.onAccent)
            .frame(maxWidth: .infinity, minHeight: 56)
            // The plate darkens and shrinks while held: at arm's length on the
            // bars, a 12% opacity change is invisible, a 4% scale step and a
            // full stop of brightness are not.
            .background(configuration.isPressed ? palette.accent.opacity(0.7) : palette.accent)
            .overlay(
                Rectangle().strokeBorder(palette.onAccent.opacity(configuration.isPressed ? 0.9 : 0),
                                         lineWidth: MotoMetrics.hairline * 2)
            )
            .brightness(configuration.isPressed ? -0.12 : 0)
            .scaleEffect(configuration.isPressed ? 0.955 : 1)
            .animation(.spring(response: 0.16, dampingFraction: 0.5),
                       value: configuration.isPressed)
            .contentShape(Rectangle())
            .onChange(of: configuration.isPressed) { _, pressed in
                guard pressed else { return }
                UIImpactFeedbackGenerator(style: .rigid).impactOccurred()
            }
    }
}

struct MotoSecondaryButtonStyle: ButtonStyle {
    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(.body, weight: .semibold))
            .foregroundStyle(palette.ink)
            .frame(maxWidth: .infinity, minHeight: 52)
            .background(configuration.isPressed ? palette.surface2 : Color.clear)
            .overlay(
                Rectangle().strokeBorder(palette.line(contrast: contrast), lineWidth: MotoMetrics.hairline)
            )
            .contentShape(Rectangle())
    }
}

// MARK: - Status band

struct MotoStatusBand<Right: View>: View {
    let linkText: String
    let linkTone: MotoLabel.Tone
    let fixText: String
    @ViewBuilder var right: () -> Right

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        HStack(spacing: MotoMetrics.s4) {
            HStack(spacing: MotoMetrics.s2) {
                Circle()
                    .fill(dotColor)
                    .frame(width: 6, height: 6)
                MotoLabel(text: linkText, tone: .onSlab, token: .micro)
                    .fixedSize()
            }
            .accessibilityElement(children: .combine)

            MotoRule(axis: .vertical).frame(height: 12)

            // Never ellipsised: at 402 pt the two cells plus the controls fit,
            // and a truncated `FIX` reads as a bug rather than as a state.
            MotoLabel(text: fixText, tone: .onSlab, token: .micro)
                .fixedSize()

            Spacer(minLength: MotoMetrics.s2)

            right()
        }
        .padding(.horizontal, MotoMetrics.s4)
        .frame(height: MotoMetrics.statusBandHeight)
        .background(palette.slab)
        .overlay(alignment: .bottom) { Rectangle().fill(palette.accent).frame(height: 2) }
    }

    private var dotColor: Color {
        switch linkTone {
        case .ok, .link: return palette.ok
        case .abort: return palette.abort
        case .accent: return palette.accent
        default: return palette.inkMuted(contrast: contrast)
        }
    }
}

// MARK: - Radar

/// The signature shape: the handlebar screen, repeated on the phone.
/// Circle only — no card, no shadow. Texture lives inside the clip.
struct MotoRadar: View {
    var size: CGFloat
    /// Normalised points in 0…1, already projected by the caller.
    var route: [CGPoint] = []
    /// Real street context in the same normalised space. When present it
    /// replaces the abstract grid: a live route over invented streets is a lie.
    var roads: [[CGPoint]] = []
    var buildings: [[CGPoint]] = []
    /// 0…1 route progress. Rides the outside of the ring, so it is never
    /// mistaken for a second data layer inside the map.
    var progress: Double?
    var showsSweep = true
    /// Draws the safehouse glyph, the way GTA marks home on the minimap.
    var showsHome = false
    /// Where home is, in radar units: (east, south) with 1 = the rim. `nil`
    /// with `showsHome` draws the glyph at a fixed rim position (no fix yet).
    var homeOffset: CGPoint?
    var showsCompass = true

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoTheme) private var theme
    @Environment(\.motoType) private var type
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        map
            .frame(width: size, height: size)
            .clipShape(Circle())
            .overlay {
                if let progress { progressArc(progress) }
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("雷达")
            .accessibilityValue(accessibilityValue)
    }

    private var map: some View {
        ZStack {
            Circle().fill(roads.isEmpty ? palette.mapLand : palette.mapBlock2)
            texture
            if roads.isEmpty {
                streets
            } else {
                mapContext
            }
            routeStroke
            rings(showsInner: roads.isEmpty)
            sweep
            if showsHome { homeBlip }
            riderDot
            if showsCompass { compassBadge }
        }
    }

    private var accessibilityValue: String {
        guard let progress else { return "路线进度不可用" }
        return "路线进度百分之\(Int((progress.clamped(to: 0...1)) * 100))"
    }

    /// A GTA III radar abstraction of a city, not a map: never enough to be
    /// mistaken for navigation. Land is the road colour and blocks sit on it,
    /// so streets are the gaps; avenues are wider than side streets, a river
    /// with a bridge splits the island, and one park breaks the grid.
    private var streets: some View {
        Canvas { context, size in
            let w = size.width
            let h = size.height
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(palette.mapLand))

            // River: a curved channel from top-right to the right edge, the
            // East River cut that makes the circle read as Liberty City.
            var river = Path()
            river.move(to: CGPoint(x: w * 0.70, y: -h * 0.02))
            river.addCurve(to: CGPoint(x: w * 0.82, y: h * 0.58),
                           control1: CGPoint(x: w * 0.62, y: h * 0.20),
                           control2: CGPoint(x: w * 0.92, y: h * 0.34))
            river.addCurve(to: CGPoint(x: w * 1.04, y: h * 0.86),
                           control1: CGPoint(x: w * 0.74, y: h * 0.74),
                           control2: CGPoint(x: w * 0.90, y: h * 0.84))
            river.addLine(to: CGPoint(x: w * 1.04, y: -h * 0.02))
            river.closeSubpath()

            // Street grid: one avenue every four lots, side streets between.
            let cells = 12
            let cellW = w / CGFloat(cells)
            let cellH = h / CGFloat(cells)
            let avenue = max(3, w / 70)
            let street = max(1.4, w / 190)
            let lot = max(0.8, w / 420)
            var seed: UInt32 = 0x9E3779B9
            func next() -> UInt32 {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5
                return seed
            }
            let park = CGRect(x: w * 0.17, y: h * 0.58, width: cellW * 3, height: cellH * 2)
            for row in 0..<cells {
                for column in 0..<cells {
                    let left = column % 4 == 0 ? avenue : street
                    let top = row % 4 == 0 ? avenue : street
                    let rect = CGRect(
                        x: CGFloat(column) * cellW + left / 2,
                        y: CGFloat(row) * cellH + top / 2,
                        width: cellW - left / 2 - street / 2,
                        height: cellH - top / 2 - street / 2
                    )
                    let centre = CGPoint(x: rect.midX, y: rect.midY)
                    if river.contains(centre) || park.contains(centre) { continue }
                    // Lots: some blocks split once, so the fabric has grain.
                    let roll = next() % 4
                    let tone = next() % 2 == 0 ? palette.mapBlock : palette.mapBlock2
                    if roll == 0 {
                        let a = CGRect(x: rect.minX, y: rect.minY,
                                       width: rect.width * 0.55 - lot / 2, height: rect.height)
                        let b = CGRect(x: rect.minX + rect.width * 0.55 + lot / 2, y: rect.minY,
                                       width: rect.width * 0.45 - lot / 2, height: rect.height)
                        context.fill(Path(a), with: .color(tone))
                        context.fill(Path(b), with: .color(palette.mapBlock2))
                    } else if roll == 1 {
                        let a = CGRect(x: rect.minX, y: rect.minY,
                                       width: rect.width, height: rect.height * 0.5 - lot / 2)
                        let b = CGRect(x: rect.minX, y: rect.midY + lot / 2,
                                       width: rect.width, height: rect.height * 0.5 - lot / 2)
                        context.fill(Path(a), with: .color(palette.mapBlock2))
                        context.fill(Path(b), with: .color(tone))
                    } else {
                        context.fill(Path(rect), with: .color(tone))
                    }
                }
            }

            // Park: one green block with a footpath, the grid's only break.
            let parkInset = park.insetBy(dx: street, dy: street)
            context.fill(Path(parkInset), with: .color(theme == .original ? palette.mapBlock : palette.mapPark))
            var footpath = Path()
            footpath.move(to: CGPoint(x: parkInset.minX, y: parkInset.maxY))
            footpath.addQuadCurve(to: CGPoint(x: parkInset.maxX, y: parkInset.minY),
                                  control: CGPoint(x: parkInset.midX, y: parkInset.maxY))
            context.stroke(footpath, with: .color(palette.mapLand.opacity(0.7)), lineWidth: lot * 1.5)

            // Water over the grid, with a hard shoreline rule.
            context.fill(river, with: .color(theme == .original ? palette.mapBlock2 : palette.mapWater))
            context.stroke(river, with: .color(palette.mapLand), lineWidth: street)

            // Bridge: the avenue at row 4 carried across the river.
            let bridgeY = 4 * cellH
            context.fill(
                Path(CGRect(x: w * 0.60, y: bridgeY - avenue / 2, width: w * 0.42, height: avenue)),
                with: .color(palette.mapLand)
            )
        }
    }

    /// Real roads and building footprints, in the same rider-centred space as
    /// the route. This is the same geometry the handlebar renders.
    private var mapContext: some View {
        Canvas { context, size in
            for footprint in buildings {
                guard footprint.count > 2 else { continue }
                var path = Path()
                path.addLines(footprint.map {
                    CGPoint(x: $0.x * size.width, y: $0.y * size.height)
                })
                path.closeSubpath()
                context.fill(path, with: .color(palette.mapBlock))
            }
            var roadsPath = Path()
            for line in roads {
                guard let first = line.first else { continue }
                roadsPath.move(to: CGPoint(x: first.x * size.width, y: first.y * size.height))
                for point in line.dropFirst() {
                    roadsPath.addLine(to: CGPoint(x: point.x * size.width, y: point.y * size.height))
                }
            }
            // Streets are context, not data: keep them under the route's
            // weight so the route line is the loudest thing in the circle.
            // Radar grammar: light road on a dark casing over the block colour.
            let roadWidth = max(1.5, size.width / 120)
            context.stroke(
                roadsPath,
                with: .color(palette.mapBlock2),
                style: StrokeStyle(lineWidth: roadWidth + 2, lineCap: .round, lineJoin: .round)
            )
            context.stroke(
                roadsPath,
                with: .color(palette.mapLand),
                style: StrokeStyle(lineWidth: roadWidth, lineCap: .round, lineJoin: .round)
            )
        }
    }


    /// GTA III's radar bezel: a heavy black ring with a thin band-colour line
    /// inside it, so the circle reads as hardware on any ground.
    private func rings(showsInner: Bool) -> some View {
        let bezel = max(4, size * 0.028)
        return ZStack {
            Circle().strokeBorder(Color.black, lineWidth: bezel)
            Circle()
                .strokeBorder(palette.band, lineWidth: 1.5)
                .padding(bezel)
        }
    }

    @ViewBuilder
    private var texture: some View {
        switch theme.radarTexture {
        case .scanlines(let pitch, let opacity):
            ScanlineTexture(pitch: pitch, color: palette.lineSoft.opacity(opacity * 6))
        case .none:
            // Day carries no texture inside the circle: glare turns thin bands
            // into haze and the map stops reading as a map.
            EmptyView()
        }
    }

    private var routeStroke: some View {
        GeometryReader { proxy in
            let path = Path { path in
                let points = route.map {
                    CGPoint(x: $0.x * proxy.size.width, y: $0.y * proxy.size.height)
                }
                guard let first = points.first else { return }
                path.move(to: first)
                for point in points.dropFirst() { path.addLine(to: point) }
            }
            let width = max(3, size * 0.023)
            ZStack {
                // 1pt casing in the paper colour separates the line from blocks.
                path.stroke(
                    palette.bg,
                    style: StrokeStyle(lineWidth: width + 2.5, lineCap: .round, lineJoin: .round)
                )
                path.stroke(
                    palette.route,
                    style: StrokeStyle(lineWidth: width, lineCap: .round, lineJoin: .round)
                )
            }
        }
    }

    /// The ring is the gauge: the completed part of the route replaces a
    /// segment of the same 2pt rim. A separate track ring around it read as a
    /// halo, not as progress.
    private func progressArc(_ value: Double) -> some View {
        Circle()
            .trim(from: 0, to: value.clamped(to: 0...1))
            .stroke(
                palette.accent,
                style: StrokeStyle(lineWidth: 3, lineCap: .butt)
            )
            .rotationEffect(.degrees(-90))
            .accessibilityHidden(true)
    }

    @ViewBuilder
    private var sweep: some View {
        if showsSweep && !reduceMotion {
            TimelineView(.animation) { timeline in
                let period = theme.sweepDuration
                let t = timeline.date.timeIntervalSinceReferenceDate
                    .truncatingRemainder(dividingBy: period) / period
                Circle()
                    .fill(
                        AngularGradient(
                            stops: [
                                .init(color: .clear, location: 0.00),
                                .init(color: .clear, location: 0.1285),
                                .init(color: palette.accent.opacity(theme == .day ? 0.20 : 0.24), location: 0.1300),
                                .init(color: .clear, location: 0.1315)
                            ],
                            center: .center
                        )
                    )
                    .rotationEffect(.degrees(t * 360 - 90))
            }
            .allowsHitTesting(false)
        }
    }

    private var compassBadge: some View {
        let badge = max(20, size * 0.085)
        return Text("N")
            .font(type.mono(.micro))
            .tracking(1)
            .foregroundStyle(palette.bg)
            .frame(width: badge, height: badge)
            .background(Circle().fill(palette.ink))
            .offset(y: -size / 2 + badge / 2)
            .accessibilityHidden(true)
    }

    /// The safehouse: HUD-orange house on a black casing, at its real
    /// bearing. A home past the rim keeps its bearing and rides the rim, as
    /// GTA pins far blips to the radar edge.
    private var homeBlip: some View {
        let rim = size * 0.40
        let position: CGPoint = {
            guard let offset = homeOffset else {
                let angle = 208.0 * Double.pi / 180
                return CGPoint(x: rim * cos(angle), y: rim * sin(angle))
            }
            let length = hypot(offset.x, offset.y)
            let scale = length > 1 ? 1 / length : 1
            return CGPoint(x: offset.x * scale * rim, y: offset.y * scale * rim)
        }()
        // The rider arrow's size, so the house reads as a house, not a speck;
        // the same eaves-and-door glyph the round screen draws.
        let glyph = size * 0.09
        return HomeGlyph()
            .fill(palette.route, style: FillStyle(eoFill: true))
            .frame(width: glyph, height: glyph)
            .overlay(HomeGlyph().stroke(Color.black, lineWidth: 2))
            .offset(x: position.x, y: position.y)
            .accessibilityHidden(true)
    }

    /// GTA echoes a white arrow, not a dot: heading is part of the readout.
    private var riderDot: some View {
        RiderArrow()
            .fill(palette.ink)
            .frame(width: size * 0.075, height: size * 0.095)
            .overlay(
                RiderArrow().stroke(palette.bg, lineWidth: 1.5)
            )
    }
}

private extension Double {
    func clamped(to range: ClosedRange<Double>) -> Double {
        min(max(self, range.lowerBound), range.upperBound)
    }
}

/// A house with overhanging eaves and a door, drawn even-odd so the door is
/// a hole the casing shows through.
private struct HomeGlyph: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        let eaves = rect.minY + rect.height * 0.48
        let wall = rect.width * 0.16
        path.move(to: CGPoint(x: rect.midX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: eaves))
        path.addLine(to: CGPoint(x: rect.maxX - wall, y: eaves))
        path.addLine(to: CGPoint(x: rect.maxX - wall, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX + wall, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX + wall, y: eaves))
        path.addLine(to: CGPoint(x: rect.minX, y: eaves))
        path.closeSubpath()
        let door = rect.width * 0.12
        path.addRect(CGRect(x: rect.midX - door, y: rect.maxY - rect.height * 0.34,
                            width: door * 2, height: rect.height * 0.34))
        return path
    }
}

private struct RiderArrow: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.midX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY * 0.72))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        path.closeSubpath()
        return path
    }
}

private struct SweepWedge: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        let centre = CGPoint(x: rect.midX, y: rect.midY)
        path.move(to: centre)
        path.addArc(
            center: centre,
            radius: max(rect.width, rect.height) / 2,
            startAngle: .degrees(0),
            endAngle: .degrees(52),
            clockwise: false
        )
        path.closeSubpath()
        return path
    }
}

private struct ScanlineTexture: View {
    let pitch: CGFloat
    let color: Color

    var body: some View {
        Canvas { context, size in
            var y: CGFloat = 0
            while y < size.height {
                context.fill(
                    Path(CGRect(x: 0, y: y, width: size.width, height: 1)),
                    with: .color(color)
                )
                y += pitch
            }
        }
    }
}

// MARK: - Screen background

struct MotoScreenBackground: View {
    @Environment(\.motoPalette) private var palette
    @Environment(\.motoTheme) private var theme

    var body: some View {
        ZStack {
            palette.bg
            switch theme {
            case .night, .day:
                // The cover lives in the home hero (HomeScreen.cover) so it
                // scrolls with the content; the screen itself is plain paper.
                EmptyView()
            case .original:
                // The upstream look is flat black: no grid, no dot screen.
                EmptyView()
            }
        }
        .ignoresSafeArea()
        .allowsHitTesting(false)
    }
}

// MARK: - Instruction card (GTA VI turn-by-turn)

/// Two-tier card: a quiet strip carrying the destination, a heavy panel carrying
/// the instruction, and a solid arrow tile on the right. The tile is the only
/// coloured element so the instruction always wins the eye.
struct MotoInstructionCard: View {
    let destination: String
    let instruction: String
    let detail: String
    let symbol: String

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        VStack(spacing: 1) {
            HStack(spacing: MotoMetrics.s2) {
                MotoLabel(text: destination, tone: .muted, token: .micro)
                Spacer(minLength: 0)
            }
            .padding(.horizontal, MotoMetrics.s3)
            .frame(height: 28)
            .frame(maxWidth: .infinity)
            .background(palette.surface2)

            HStack(spacing: 0) {
                VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                    Text(instruction)
                        .font(.system(.title2, weight: .bold))
                        .foregroundStyle(palette.ink)
                        .lineLimit(2)
                        .minimumScaleFactor(0.7)
                    MotoLabel(text: detail, tone: .muted, token: .micro)
                }
                .padding(MotoMetrics.s3)
                .frame(maxWidth: .infinity, alignment: .leading)

                Image(systemName: symbol)
                    .font(.system(size: 26, weight: .bold))
                    .foregroundStyle(palette.bg)
                    .frame(width: 64)
                    .frame(maxHeight: .infinity)
                    .background(palette.route)
            }
            .fixedSize(horizontal: false, vertical: true)
            .background(palette.surface)
        }
        .overlay(
            Rectangle().strokeBorder(palette.line(contrast: contrast), lineWidth: MotoMetrics.hairline)
        )
        .accessibilityElement(children: .combine)
    }
}

// MARK: - Meter

/// Framed bar. 2px ink frame, flat fill, no radius — the GTA status bar.
struct MotoMeter: View {
    let label: String
    let value: Double
    let maxValue: Double
    var tone: Color?

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    private var fraction: CGFloat {
        guard maxValue > 0 else { return 0 }
        return CGFloat(min(max(value / maxValue, 0), 1))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s1) {
            HStack {
                MotoLabel(text: label, tone: .muted, token: .micro)
                Spacer(minLength: 0)
                MotoLabel(text: String(format: "%.0f", value), tone: .ink, token: .micro)
            }
            GeometryReader { proxy in
                ZStack(alignment: .leading) {
                    Rectangle().fill(palette.surface2)
                    Rectangle()
                        .fill(tone ?? palette.accent)
                        .frame(width: proxy.size.width * fraction)
                }
                .overlay(Rectangle().strokeBorder(palette.ink, lineWidth: 2))
            }
            .frame(height: 14)
        }
        .accessibilityElement(children: .combine)
    }
}

// MARK: - Speed limit sign

/// The one white surface in the system. Reads at a glance in sunlight.
struct MotoSpeedSign: View {
    let limit: Int

    @Environment(\.motoPalette) private var palette

    var body: some View {
        VStack(spacing: 0) {
            Text("SPEED LIMIT")
                .font(.system(size: 9, weight: .heavy))
                .tracking(0.4)
                .foregroundStyle(Color(hex: 0x111111))
            Text("\(limit)")
                .font(.custom("BarlowCondensed-ExtraBold", size: 34, relativeTo: .title))
                .monospacedDigit()
                .foregroundStyle(Color(hex: 0x111111))
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(Color.white)
        .overlay(Rectangle().strokeBorder(Color(hex: 0x111111), lineWidth: 3))
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("限速 \(limit)")
    }
}

// MARK: - HUD type

/// The GTA HUD trick: type survives any background because it carries its own
/// outline. SwiftUI has no text-stroke, so stack offset shadows at radius 0.
struct MotoOutline: ViewModifier {
    var color: Color
    var width: CGFloat

    func body(content: Content) -> some View {
        content
            .shadow(color: color, radius: 0, x: width, y: 0)
            .shadow(color: color, radius: 0, x: -width, y: 0)
            .shadow(color: color, radius: 0, x: 0, y: width)
            .shadow(color: color, radius: 0, x: 0, y: -width)
            .shadow(color: color, radius: 0, x: width * 0.71, y: width * 0.71)
            .shadow(color: color, radius: 0, x: -width * 0.71, y: width * 0.71)
            .shadow(color: color, radius: 0, x: width * 0.71, y: -width * 0.71)
            .shadow(color: color, radius: 0, x: -width * 0.71, y: -width * 0.71)
    }
}

extension View {
    func motoOutline(_ color: Color, width: CGFloat = 2) -> some View {
        modifier(MotoOutline(color: color, width: width))
    }

    /// GTA III's HUD drop: solid black, down-right, no blur. `nil` draws none.
    @ViewBuilder
    func motoHudShadow(_ offset: CGFloat?) -> some View {
        if let offset {
            shadow(color: .black, radius: 0, x: offset, y: offset)
        } else {
            self
        }
    }
}

/// All-caps wordmark. Anton + outline + a 3pt hard drop is the poster voice.
struct MotoWordmark: View {
    let text: String
    var size: CGFloat = 76
    var fill: Color
    var outline: Color
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.motoTheme) private var theme

    var body: some View {
        Text(text.uppercased())
            .font(theme.wordmarkFont(size: size, relativeTo: .largeTitle))
            .tracking(-0.5)
            .foregroundStyle(fill)
            // A wordmark never wraps: at 132pt "GO" would break into two lines
            // the moment the row runs out of width.
            .fixedSize()
            .lineLimit(1)
            .motoOutline(outline, width: max(2, size * 0.035))
            // Hard drop, always dark: a shadow in the outline colour turns into
            // a smear on light fills.
            .shadow(color: .black.opacity(theme.hudShadow != nil ? 1 : 0.35), radius: 0, x: size * 0.03, y: size * 0.035)
            .dynamicTypeSize(...DynamicTypeSize.accessibility1)
            .accessibilityAddTraits(.isHeader)
    }
}
