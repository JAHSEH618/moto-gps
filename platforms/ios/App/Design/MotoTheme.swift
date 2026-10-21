import SwiftUI

// MARK: - Theme

/// Two themes, chosen by ambient light rather than taste.
/// See DESIGN.md → "Theme switch".
enum MotoThemeName: String, CaseIterable, Identifiable {
    /// Dark garage, night ride. Instrument console. Amber on near-black.
    case vice
    /// Daylight with glare. Poster. Near-black on sunlit paper.
    case heat

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .vice: return "Vice"
        case .heat: return "Heat"
        }
    }

    /// One-line description for the settings row.
    var summary: String {
        switch self {
        case .vice: return "夜间仪表盘 · 暗底琥珀"
        case .heat: return "日光海报 · 亮底黑橙"
        }
    }

    var palette: MotoPalette {
        switch self {
        case .vice: return .vice
        case .heat: return .heat
        }
    }

    /// Dark themes drive the system chrome; the light theme must also override it.
    var colorScheme: ColorScheme {
        switch self {
        case .vice: return .dark
        case .heat: return .light
        }
    }

    /// Display weight differs per theme: the console is semi-bold, the poster is extra-bold.
    var displayFontName: String {
        switch self {
        case .vice: return "BarlowCondensed-SemiBold"
        case .heat: return "BarlowCondensed-ExtraBold"
        }
    }

    /// Wordmark face: Anton, the closest OFL match to the GTA logo weight.
    var wordmarkFontName: String { "Anton-Regular" }

    var sweepDuration: Double {
        switch self {
        case .vice: return 4
        case .heat: return 6
        }
    }

    /// Texture inside the radar circle. Heat drops scanlines because they fight glare legibility.
    var radarTexture: RadarTexture {
        switch self {
        case .vice: return .scanlines(pitch: 4, opacity: 0.10)
        case .heat: return .bands(height: 7, opacity: 0.10)
        }
    }

    /// Body copy floor. Heat never drops secondary text below `body` size.
    var minimumSecondaryTextSize: CGFloat {
        switch self {
        case .vice: return 11
        case .heat: return 15
        }
    }
}

enum RadarTexture {
    case scanlines(pitch: CGFloat, opacity: Double)
    case bands(height: CGFloat, opacity: Double)
    case none
}

// MARK: - Palette

struct MotoPalette {
    let bg: Color
    let surface: Color
    let surface2: Color
    let line: Color
    let lineSoft: Color
    let ink: Color
    let inkMuted: Color
    /// HUD slab: the solid black bar GTA puts behind top-of-screen readouts.
    /// It stays near-black in both themes so the slab reads as hardware.
    let slab: Color
    let onSlab: Color
    let accent: Color
    /// Route stroke. Magenta on purpose: GTA draws the line in pink, not in the
    /// UI accent, so the line reads as data instead of as chrome.
    let route: Color
    let link: Color
    let ok: Color
    let abort: Color

    /// Vice — amber on near-black. Body text 17.4:1, accent 11.6:1.
    static let vice = MotoPalette(
        bg: Color(hex: 0x060808),
        surface: Color(hex: 0x0E1211),
        surface2: Color(hex: 0x141918),
        line: Color(hex: 0x232B2A),
        lineSoft: Color(white: 1, opacity: 0.10),
        ink: Color(hex: 0xF2F4EE),
        inkMuted: Color(hex: 0x93A09C),
        slab: Color(hex: 0x101514),
        onSlab: Color(hex: 0xF2F4EE),
        accent: Color(hex: 0xFFB72E),
        route: Color(hex: 0xFF2D7A),
        link: Color(hex: 0x5CC7FF),
        ok: Color(hex: 0x73E6A4),
        abort: Color(hex: 0xFF655F)
    )

    /// Heat — near-black on sunlit paper. Built for direct daylight, not for mood.
    /// Body text 15.1:1, secondary 6.8:1, accent fill with ink text 5.0:1.
    /// Accent is never used for body-size text: 3.9:1 only clears AA-large.
    static let heat = MotoPalette(
        bg: Color(hex: 0xF2E6CE),
        surface: Color(hex: 0xEBDBBC),
        surface2: Color(hex: 0xE3D0AB),
        line: Color(hex: 0x17110D).opacity(0.24),
        lineSoft: Color(hex: 0x17110D).opacity(0.12),
        ink: Color(hex: 0x17110D),
        inkMuted: Color(hex: 0x5C4A38),
        slab: Color(hex: 0x17110D),
        onSlab: Color(hex: 0xF6E7CE),
        accent: Color(hex: 0xE8500F),
        route: Color(hex: 0xD6115C),
        link: Color(hex: 0x0A5C78),
        ok: Color(hex: 0x1D6B3C),
        abort: Color(hex: 0xB3241C)
    )
}

extension Color {
    init(hex: UInt32) {
        self.init(
            .sRGB,
            red: Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >> 8) & 0xFF) / 255,
            blue: Double(hex & 0xFF) / 255,
            opacity: 1
        )
    }
}

// MARK: - Type scale

/// Semantic scale. Body sizes map onto system text styles so Dynamic Type works;
/// display numerals use the bundled condensed face relative to `.largeTitle`.
/// See DESIGN.md → Typography.
enum MotoText {
    case displayXL   // 76 — remaining distance, speed
    case displayLG   // 56 — run timer, ETA
    case displayMD   // 34 — panel headline number
    case title       // 22 — screen title
    case body        // 17 — prose (HIG default iOS size)
    case secondary   // 15 — metadata
    case label       // 12, caps mono
    case micro       // 11, caps mono (HIG minimum iOS size)
}

struct MotoTypography {
    let theme: MotoThemeName

    func display(_ token: MotoText) -> Font {
        guard let size = token.displaySize else {
            return .system(.body)
        }
        // `relativeTo:` keeps the instrument numeral honouring Dynamic Type;
        // the host view caps the range so a 76pt numeral cannot reflow the screen.
        return .custom(theme.displayFontName, size: size, relativeTo: .largeTitle)
    }

    func mono(_ token: MotoText) -> Font {
        .system(token.monoStyle, design: .monospaced).weight(.medium)
    }

    func prose(_ token: MotoText) -> Font {
        .system(token.proseStyle, weight: token.proseWeight)
    }
}

private extension MotoText {
    var displaySize: CGFloat? {
        switch self {
        case .displayXL: return 76
        case .displayLG: return 56
        case .displayMD: return 34
        default: return nil
        }
    }

    var monoStyle: Font.TextStyle {
        switch self {
        case .micro: return .caption2   // 11pt default
        case .label: return .caption    // 12pt default
        case .secondary: return .subheadline
        case .body: return .callout
        case .title: return .headline
        default: return .caption2
        }
    }

    var proseStyle: Font.TextStyle {
        switch self {
        case .displayXL, .displayLG, .displayMD: return .largeTitle
        case .title: return .title2
        case .body: return .body
        case .secondary: return .subheadline
        case .label: return .caption
        case .micro: return .caption2
        }
    }

    var proseWeight: Font.Weight {
        switch self {
        case .title: return .semibold
        case .body, .secondary, .label, .micro: return .regular
        default: return .semibold
        }
    }
}

// MARK: - Metrics

enum MotoMetrics {
    /// `4 · 8 · 12 · 18 · 27 · 40 · 64`. Jump two steps for rhythm.
    static let s1: CGFloat = 4
    static let s2: CGFloat = 8
    static let s3: CGFloat = 12
    static let s4: CGFloat = 18
    static let s5: CGFloat = 27
    static let s6: CGFloat = 40
    static let s7: CGFloat = 64

    /// Panels, sheets, buttons and bars are square. Only dots are round.
    static let panelRadius: CGFloat = 0
    static let chipRadius: CGFloat = 2
    static let dotRadius: CGFloat = 999

    static let labelTracking: CGFloat = 1.9   // +0.16em at 12pt
    static let microTracking: CGFloat = 1.8
    static let displayTracking: CGFloat = -0.4

    static let statusBandHeight: CGFloat = 54
    static let hairline: CGFloat = 1
    /// HIG minimum hit target. Any control smaller than this is a defect.
    static let minTapTarget: CGFloat = 44

    enum RadarSize {
        static let hero: CGFloat = 220
        static let ride: CGFloat = 120
        static let band: CGFloat = 44
    }
}

// MARK: - Motion

enum MotoMotion {
    static let digit = Animation.timingCurve(0.16, 1, 0.3, 1, duration: 0.18)
    static let interface = Animation.easeOut(duration: 0.22)
    static let reduced = Animation.linear(duration: 0.12)
    static let routeDraw = Animation.easeOut(duration: 0.6)

    static func digit(reduceMotion: Bool) -> Animation {
        reduceMotion ? reduced : digit
    }
}

// MARK: - Environment

private struct MotoThemeKey: EnvironmentKey {
    static let defaultValue = MotoThemeName.vice
}

private struct MotoPaletteKey: EnvironmentKey {
    static let defaultValue = MotoPalette.vice
}

private struct MotoTypeKey: EnvironmentKey {
    static let defaultValue = MotoTypography(theme: .vice)
}

extension EnvironmentValues {
    var motoTheme: MotoThemeName {
        get { self[MotoThemeKey.self] }
        set { self[MotoThemeKey.self] = newValue }
    }

    var motoPalette: MotoPalette {
        get { self[MotoPaletteKey.self] }
        set { self[MotoPaletteKey.self] = newValue }
    }

    var motoType: MotoTypography {
        get { self[MotoTypeKey.self] }
        set { self[MotoTypeKey.self] = newValue }
    }
}

extension View {
    /// Apply a theme once at the root. Sets the derived palette and type at the
    /// same time so leaf views never have to recompute them.
    func motoTheme(_ theme: MotoThemeName) -> some View {
        self
            .environment(\.motoTheme, theme)
            .environment(\.motoPalette, theme.palette)
            .environment(\.motoType, MotoTypography(theme: theme))
            .environment(\.colorScheme, theme.colorScheme)
    }
}

// MARK: - Increased contrast

/// HIG § Color: custom colours must survive Increase Contrast.
/// Rules get heavier and secondary text moves toward `ink`.
extension MotoPalette {
    func line(contrast: ColorSchemeContrast) -> Color {
        contrast == .increased ? ink.opacity(0.55) : line
    }

    func inkMuted(contrast: ColorSchemeContrast) -> Color {
        contrast == .increased ? ink : inkMuted
    }
}
