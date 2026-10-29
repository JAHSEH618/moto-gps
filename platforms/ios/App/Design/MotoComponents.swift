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

    enum Tone { case muted, ink, accent, link, ok, abort, onSlab, paper }

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
        // Type on an ink counter plate: the paper colour, so the plate reads
        // as a hole punched in the page.
        case .paper: return palette.bg
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

    /// `ink` is the HUD colour (pink by night); `plain` is the page ink, for
    /// numerals that index something rather than measure it.
    enum Tone { case ink, plain, accent, muted }

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
        case .plain: return palette.ink
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
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(.body, weight: .heavy))
            .foregroundStyle(palette.onAccent)
            .multilineTextAlignment(.center)
            .padding(.horizontal, MotoMetrics.s4)
            .frame(maxWidth: .infinity, minHeight: 56)
            // The plate darkens and takes an inner rule while held: at arm's
            // length on the bars a 12% opacity change is invisible, a full
            // stop of brightness and a 2pt frame are not. No scale: the
            // poster never moves its panels.
            .background(palette.accent)
            .overlay(
                Rectangle().strokeBorder(palette.onAccent.opacity(configuration.isPressed ? 0.9 : 0),
                                         lineWidth: MotoMetrics.hairline * 2)
            )
            .brightness(configuration.isPressed ? -0.14 : 0)
            .opacity(isEnabled ? 1 : 0.4)
            .contentShape(Rectangle())
            .onChange(of: configuration.isPressed) { _, pressed in
                guard pressed else { return }
                UIImpactFeedbackGenerator(style: .rigid).impactOccurred()
            }
    }
}

/// Outlined in ink, not in the hairline colour: on a panel a 1px grey frame
/// read as one more text field. Disabled drops the whole plate to 40%.
struct MotoSecondaryButtonStyle: ButtonStyle {
    @Environment(\.motoPalette) private var palette
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(.body, weight: .bold))
            .foregroundStyle(palette.ink)
            .multilineTextAlignment(.center)
            .padding(.horizontal, MotoMetrics.s4)
            .frame(maxWidth: .infinity, minHeight: 52)
            .background(configuration.isPressed ? palette.surface2 : Color.clear)
            .overlay(
                Rectangle().strokeBorder(palette.ink, lineWidth: MotoMetrics.hairline * 2)
            )
            .opacity(isEnabled ? 1 : 0.4)
            .contentShape(Rectangle())
    }
}

// MARK: - Status band

struct MotoStatusBand<Right: View>: View {
    let linkText: String
    let linkTone: MotoLabel.Tone
    let fixText: String
    /// The `ETA|CLOCK` cell. It is the first thing the band gives up when the
    /// reader's text size leaves no room: the system status bar above it
    /// already carries the time.
    var clock: String?
    @ViewBuilder var right: () -> Right

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        // The band must never be wider than the screen: an overflowing bar
        // widens the whole page behind it. Full row first, then without the
        // clock.
        ViewThatFits(in: .horizontal) {
            row(showsClock: true)
            row(showsClock: false)
        }
        .padding(.horizontal, MotoMetrics.s4)
        .frame(minHeight: MotoMetrics.statusBandHeight)
        // A bar, like the system's own: it stops growing at the largest
        // standard size, and its controls offer the large content viewer
        // instead of pushing the cells off the edge.
        .dynamicTypeSize(...DynamicTypeSize.xxxLarge)
        .background(palette.slab)
        .overlay(alignment: .bottom) { Rectangle().fill(palette.accent).frame(height: 2) }
    }

    private func row(showsClock: Bool) -> some View {
        HStack(spacing: MotoMetrics.s4) {
            HStack(spacing: MotoMetrics.s2) {
                Circle()
                    .fill(dotColor)
                    .frame(width: 6, height: 6)
                MotoLabel(text: linkText, tone: .onSlab, token: .micro)
                    .fixedSize()
            }
            .accessibilityElement(children: .combine)
            .accessibilityShowsLargeContentViewer()

            // The page's hairline is ink at 24% by day, which vanishes on the
            // black slab; the divider is the slab's own ink.
            Rectangle()
                .fill(palette.onSlab.opacity(0.32))
                .frame(width: MotoMetrics.hairline, height: 12)
                .accessibilityHidden(true)

            // Never ellipsised: a truncated `FIX` reads as a bug rather than
            // as a state.
            MotoLabel(text: fixText, tone: .onSlab, token: .micro)
                .fixedSize()
                .accessibilityShowsLargeContentViewer()

            Spacer(minLength: MotoMetrics.s2)

            HStack(spacing: MotoMetrics.s1) {
                right()
                if showsClock, let clock {
                    MotoLabel(text: clock, tone: .onSlab, token: .micro)
                        .fixedSize()
                }
            }
        }
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

// MARK: - Map glyphs

/// A house with overhanging eaves and a door, drawn even-odd so the door is
/// a hole the casing shows through.
struct HomeGlyph: Shape {
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

struct RiderArrow: Shape {
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
                        .font(.system(.title2, weight: .heavy))
                        .foregroundStyle(palette.ink)
                        .lineLimit(2)
                        .minimumScaleFactor(0.7)
                    Text(detail)
                        .font(.system(.subheadline))
                        .foregroundStyle(palette.inkMuted(contrast: contrast))
                        .fixedSize(horizontal: false, vertical: true)
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
