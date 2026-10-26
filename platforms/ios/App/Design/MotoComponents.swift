import SwiftUI

// MARK: - Hairlines and panels

/// 1px rule. Heat uses a heavier line than Vice; Increase Contrast thickens both.
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

    enum Tone { case ink, accent, muted }

    var body: some View {
        Text(value)
            .font(type.display(token))
            .tracking(MotoMetrics.displayTracking)
            .monospacedDigit()
            .foregroundStyle(color)
            .lineLimit(1)
            .minimumScaleFactor(0.6)
            // A 76pt numeral must not reflow the screen at accessibility sizes.
            .dynamicTypeSize(...DynamicTypeSize.accessibility1)
    }

    private var color: Color {
        switch tone {
        case .ink: return palette.ink
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
            .font(.system(.body, weight: .semibold))
            .foregroundStyle(palette.onAccent)
            .frame(maxWidth: .infinity, minHeight: 52)
            .background(configuration.isPressed ? palette.accent.opacity(0.82) : palette.accent)
            .contentShape(Rectangle())
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
            Circle().fill(palette.surface)
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

    /// A poster abstraction of a street grid, not a map. Enough geometry for the
    /// circle to read as a minimap; never enough to be mistaken for navigation.
    ///
    /// The light theme needs heavier ink: dark-on-cream at 10% disappears, while
    /// light-on-black at 10% reads. Same structure, different strength.
    private var streets: some View {
        Canvas { context, size in
            let w = size.width
            let h = size.height
            let roadWidth = max(1, size.width / 150)
            var path = Path()
            for i in 1...4 {
                let y = h * CGFloat(i) / 5
                path.move(to: CGPoint(x: w * 0.08, y: y))
                path.addLine(to: CGPoint(x: w * 0.92, y: y))
            }
            for i in 1...3 {
                let x = w * CGFloat(i) / 4
                path.move(to: CGPoint(x: x, y: h * 0.10))
                path.addLine(to: CGPoint(x: x, y: h * 0.90))
            }
            path.move(to: CGPoint(x: w * 0.05, y: h * 0.82))
            path.addLine(to: CGPoint(x: w * 0.95, y: h * 0.18))
            context.stroke(
                path,
                with: .color(palette.ink.opacity(roadOpacity)),
                lineWidth: roadWidth
            )

            // Blocks, not roads: the shape GTA uses to say "city".
            let blocks: [CGRect] = [
                CGRect(x: w * 0.10, y: h * 0.12, width: w * 0.16, height: h * 0.12),
                CGRect(x: w * 0.33, y: h * 0.10, width: w * 0.12, height: h * 0.18),
                CGRect(x: w * 0.60, y: h * 0.22, width: w * 0.18, height: h * 0.10),
                CGRect(x: w * 0.12, y: h * 0.44, width: w * 0.20, height: h * 0.14),
                CGRect(x: w * 0.55, y: h * 0.46, width: w * 0.14, height: h * 0.16),
                CGRect(x: w * 0.30, y: h * 0.70, width: w * 0.22, height: h * 0.12),
                CGRect(x: w * 0.66, y: h * 0.66, width: w * 0.18, height: h * 0.20)
            ]
            // Park and water: the two colours that make a minimap read as GTA.
            context.fill(
                Path(CGRect(x: w * 0.05, y: h * 0.60, width: w * 0.30, height: h * 0.26)),
                // Original has no GTA colour code: parks read as grey blocks.
                with: .color((theme == .original ? palette.inkMuted : palette.ok).opacity(parkOpacity))
            )
            context.fill(
                Path(CGRect(x: w * 0.68, y: h * 0.02, width: w * 0.34, height: h * 0.22)),
                with: .color((theme == .original ? palette.inkMuted : palette.link).opacity(waterOpacity))
            )
            for block in blocks {
                context.fill(Path(block), with: .color(palette.ink.opacity(blockOpacity)))
            }
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
                context.fill(path, with: .color(palette.ink.opacity(realBuildingOpacity)))
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
            // weight so the magenta line is the loudest thing in the circle.
            context.stroke(
                roadsPath,
                with: .color(palette.ink.opacity(realRoadOpacity)),
                style: StrokeStyle(lineWidth: max(1, size.width / 190), lineCap: .round, lineJoin: .round)
            )
        }
    }

    private var realBuildingOpacity: Double { theme == .heat ? 0.10 : 0.14 }
    private var realRoadOpacity: Double { theme == .heat ? 0.34 : 0.55 }
    private var blockOpacity: Double { theme == .heat ? 0.17 : 0.10 }
    private var roadOpacity: Double { theme == .heat ? 0.62 : 0.24 }
    private var parkOpacity: Double { theme == .heat ? 0.34 : 0.24 }
    private var waterOpacity: Double { theme == .heat ? 0.30 : 0.22 }

    private func rings(showsInner: Bool) -> some View {
        ZStack {
            Circle().strokeBorder(palette.ink, lineWidth: 2)
            if showsInner {
                Circle()
                    .strokeBorder(palette.line(contrast: contrast), lineWidth: MotoMetrics.hairline)
                    .padding(size * 0.10)
                Circle()
                    .strokeBorder(palette.line(contrast: contrast), lineWidth: MotoMetrics.hairline)
                    .padding(size * 0.22)
            }
        }
    }

    @ViewBuilder
    private var texture: some View {
        switch theme.radarTexture {
        case .scanlines(let pitch, let opacity):
            ScanlineTexture(pitch: pitch, color: palette.lineSoft.opacity(opacity * 6))
        case .none:
            // Heat carries no texture inside the circle: glare turns thin bands
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
                                .init(color: palette.accent.opacity(theme == .heat ? 0.20 : 0.24), location: 0.1300),
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

    private var homeBlip: some View {
        let radius = size * 0.44
        let angle = 208.0 * Double.pi / 180
        return HomeGlyph()
            .fill(palette.ink)
            .frame(width: size * 0.085, height: size * 0.080)
            .overlay(HomeGlyph().stroke(palette.bg, lineWidth: 1))
            .offset(
                x: CGFloat(radius * cos(angle)),
                y: CGFloat(radius * sin(angle))
            )
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

private struct HomeGlyph: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        let roofTop = CGPoint(x: rect.midX, y: rect.minY)
        path.move(to: roofTop)
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.midY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.midY))
        path.closeSubpath()
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
            case .vice:
                Canvas { context, size in
                    let pitch: CGFloat = 44
                    let stroke = palette.ink.opacity(0.045)
                    var x: CGFloat = 0
                    while x < size.width {
                        context.fill(
                            Path(CGRect(x: x, y: 0, width: 1, height: size.height)),
                            with: .color(stroke)
                        )
                        x += pitch
                    }
                    var y: CGFloat = 0
                    while y < size.height {
                        context.fill(
                            Path(CGRect(x: 0, y: y, width: size.width, height: 1)),
                            with: .color(stroke)
                        )
                        y += pitch
                    }
                }
                // The grid belongs to the hero, not to the copy. Fade it out
                // before the first list row so text never sits on a texture.
                .mask(
                    LinearGradient(
                        stops: [
                            .init(color: .black, location: 0),
                            .init(color: .black, location: 0.42),
                            .init(color: .clear, location: 0.58)
                        ],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
            case .heat:
                Canvas { context, size in
                    let pitch: CGFloat = 8
                    let stroke = palette.ink.opacity(0.05)
                    var y: CGFloat = 0
                    while y < size.height {
                        var x: CGFloat = 0
                        while x < size.width {
                            context.fill(
                                Path(ellipseIn: CGRect(x: x, y: y, width: 1.4, height: 1.4)),
                                with: .color(stroke)
                            )
                            x += pitch
                        }
                        y += pitch
                    }
                }
                .mask(
                    LinearGradient(
                        stops: [
                            .init(color: .black, location: 0),
                            .init(color: .black, location: 0.42),
                            .init(color: .clear, location: 0.58)
                        ],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
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
            .shadow(color: .black.opacity(0.35), radius: 0, x: size * 0.03, y: size * 0.035)
            .dynamicTypeSize(...DynamicTypeSize.accessibility1)
            .accessibilityAddTraits(.isHeader)
    }
}
