import MotoNavigationCore
import SwiftUI
import UIKit

// MARK: - Theme

/// Three looks, one per round-screen family: GTA III's Liberty City by night
/// and by day, and the black-and-white interface this project was forked from
/// (Original). The rider picks the theme once; the phone wears what the round
/// screen shows. See DESIGN.md → "Theme switch".
enum MotoThemeName: String, CaseIterable, Identifiable {
    /// Liberty City after dark: true black, box-art white, neon pink HUD.
    case night
    /// The cover's white field: newsprint paper, black ink, readable in sun.
    case day
    /// The upstream look: white on true black, system type, no poster devices.
    case original

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .night: return "Liberty Night"
        case .day: return "Liberty Day"
        case .original: return "Original"
        }
    }

    /// One-line description for the settings row.
    var summary: String {
        switch self {
        case .night: return "GTA III · 夜间"
        case .day: return "GTA III · 白天"
        case .original: return "原版 · 黑底白线"
        }
    }

    var palette: MotoPalette {
        switch self {
        case .night: return .night
        case .day: return .day
        case .original: return .original
        }
    }

    /// Dark themes drive the system chrome; the light theme must also override it.
    var colorScheme: ColorScheme {
        switch self {
        case .night, .original: return .dark
        case .day: return .light
        }
    }

    /// The HUD numeral: one heavy condensed weight in both GTA themes. Original
    /// has no bundled face: it sets numerals in the system font, as the
    /// upstream app did.
    var displayFontName: String? {
        switch self {
        case .night, .day: return "BarlowCondensed-ExtraBold"
        case .original: return nil
        }
    }

    /// Wordmark face: Anton, the closest OFL match to the GTA III logo weight.
    /// Original sets its headings in the system face instead.
    var wordmarkFontName: String? { self == .original ? nil : "Anton-Regular" }

    func wordmarkFont(size: CGFloat, relativeTo style: Font.TextStyle) -> Font {
        guard let name = wordmarkFontName else {
            let scaled = UIFontMetrics(forTextStyle: style.uiTextStyle).scaledValue(for: size)
            return .system(size: scaled, weight: .heavy)
        }
        return .custom(name, size: size, relativeTo: style)
    }

    /// GTA III's HUD caps are wide and square, not monospaced. SF Pro Expanded
    /// Heavy is the system face that carries that voice; Original keeps mono.
    var usesHudCaps: Bool { self != .original }

    /// The HUD's hard drop: black, offset down-right, no blur. Night only; on
    /// the day paper ink is already black and the drop would only thicken it.
    var hudShadow: CGFloat? { self == .night ? 3 : nil }

    var sweepDuration: Double {
        switch self {
        case .night, .original: return 4
        case .day: return 6
        }
    }

    /// GTA III's radar is flat: no scanlines in either theme.
    var radarTexture: RadarTexture { .none }

    /// Body copy floor. Day never drops secondary text below `body` size.
    var minimumSecondaryTextSize: CGFloat {
        switch self {
        case .night, .original: return 11
        case .day: return 15
        }
    }
}

enum RadarTexture {
    case scanlines(pitch: CGFloat, opacity: Double)
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
    /// Text and glyphs on top of `accent`. Differs per theme because the
    /// accent fill does: black on night pink, paper on day pink.
    let onAccent: Color
    /// Route stroke. Not the UI accent, so the line reads as data instead of
    /// as chrome.
    let route: Color
    let link: Color
    let ok: Color
    let abort: Color
    /// The instrument band and the ink on it: the cover's panel gutter, white
    /// by night and black by day. Original uses white, as its round screen does.
    var band = Color(hex: 0xF2EEE4)
    var onBand = Color(hex: 0x0A0A0A)
    /// HUD numeral colour. GTA prints its readouts in colour, never in plain
    /// white; here that colour is the handlebar band's neon pink. By day the
    /// paper needs black ink instead.
    var hud: Color? = nil
    /// Radar map. Land is the road colour: streets are the gaps between
    /// blocks, which is how GTA III's radar draws a city. Two block tones split
    /// lots; water and park are the only hues inside the circle.
    var mapLand = Color(hex: 0x3A3A3A)
    var mapBlock = Color(hex: 0x222222)
    var mapBlock2 = Color(hex: 0x1B1B1B)
    var mapWater = Color(hex: 0x14202E)
    var mapPark = Color(hex: 0x28311F)

    /// Liberty Night — GTA III's HUD on true black. Ink is the box-art white;
    /// the accent is neon pink, the round screen's band (`#F34DAA`), so the
    /// phone and the handlebar wear one accent. Link is the money steel-blue,
    /// route the health orange, ok the armour olive. Each clears AA on black:
    /// ink 17.1:1, muted 7.8, pink 6.1, route 7.9, link 7.9, abort 6.1.
    static let night = MotoPalette(
        bg: Color(hex: 0x0A0A0A),
        surface: Color(hex: 0x141414),
        surface2: Color(hex: 0x1D1D1D),
        line: Color(hex: 0x333333),
        lineSoft: Color(white: 1, opacity: 0.08),
        ink: Color(hex: 0xF2EEE4),
        inkMuted: Color(hex: 0xA8A294),
        slab: Color(hex: 0x000000),
        onSlab: Color(hex: 0xF2EEE4),
        accent: Color(hex: 0xF34DAA),
        onAccent: Color(hex: 0x0A0A0A),
        route: Color(hex: 0xF08A3C),
        link: Color(hex: 0x86A6CF),
        ok: Color(hex: 0xA5B97A),
        abort: Color(hex: 0xF0614A),
        // The cover's panel gutters: white bands on black.
        band: Color(hex: 0xF2EEE4),
        onBand: Color(hex: 0x0A0A0A),
        hud: Color(hex: 0xF34DAA)
    )

    /// Liberty Day — the cover's white field for sunlight: newsprint paper and
    /// black ink. The HUD hues are darkened until they read as text on paper;
    /// neon does not survive sun, so the pink is the band's hue at text
    /// strength: ink 16.0:1, muted 7.9, pink 6.0 (4.7 on surface-2), route 4.7,
    /// link 6.4, abort 6.0.
    static let day = MotoPalette(
        bg: Color(hex: 0xEFECE4),
        surface: Color(hex: 0xE4E0D6),
        surface2: Color(hex: 0xD8D3C7),
        line: Color(hex: 0x111111).opacity(0.24),
        lineSoft: Color(hex: 0x111111).opacity(0.12),
        ink: Color(hex: 0x111111),
        inkMuted: Color(hex: 0x4B463E),
        slab: Color(hex: 0x111111),
        onSlab: Color(hex: 0xEFECE4),
        accent: Color(hex: 0xAA1068),
        onAccent: Color(hex: 0xEFECE4),
        route: Color(hex: 0xB04716),
        link: Color(hex: 0x35567F),
        ok: Color(hex: 0x4A5C2A),
        abort: Color(hex: 0xA8281C),
        // Black bands on white: the inverse gutter, the logo panel's colour.
        band: Color(hex: 0x111111),
        onBand: Color(hex: 0xEFECE4),
        // The radar stays a grey city by day, lifted so it reads on paper.
        mapLand: Color(hex: 0xC9C5BB),
        mapBlock: Color(hex: 0x8F8B83),
        mapBlock2: Color(hex: 0x9C988F),
        mapWater: Color(hex: 0x6E879F),
        mapPark: Color(hex: 0x7D8A5B)
    )

    /// Original — the round screen's upstream colours on the phone: true black,
    /// warm white ink and route, graphite rules. White is the accent, so plates
    /// carry black ink (19:1). No HUD colours anywhere.
    static let original = MotoPalette(
        bg: Color(hex: 0x050607),
        surface: Color(hex: 0x0E1012),
        surface2: Color(hex: 0x16191C),
        line: Color(hex: 0x303539),
        lineSoft: Color(white: 1, opacity: 0.08),
        ink: Color(hex: 0xF3F4EF),
        inkMuted: Color(hex: 0xAEB2B0),
        slab: Color(hex: 0x000000),
        onSlab: Color(hex: 0xF3F4EF),
        accent: Color(hex: 0xF3F4EF),
        onAccent: Color(hex: 0x050607),
        route: Color(hex: 0xF3F4EF),
        link: Color(hex: 0xB8EDF5),
        ok: Color(hex: 0x69D494),
        abort: Color(hex: 0xFF4B43),
        band: Color(hex: 0xF3F4EF),
        onBand: Color(hex: 0x050607),
        mapLand: Color(hex: 0x2A2D30),
        mapBlock: Color(hex: 0x16191C),
        mapBlock2: Color(hex: 0x121416),
        mapWater: Color(hex: 0x101418),
        mapPark: Color(hex: 0x16191C)
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
        guard let name = theme.displayFontName else {
            let scaled = UIFontMetrics(forTextStyle: .largeTitle).scaledValue(for: size)
            return .system(size: scaled, weight: .semibold)
        }
        return .custom(name, size: size, relativeTo: .largeTitle)
    }

    /// Caps labels. The GTA themes set them in SF Pro Expanded Heavy, the
    /// HUD's wide square caps; Original keeps the mono it shipped with.
    func mono(_ token: MotoText) -> Font {
        guard theme.usesHudCaps else {
            return .system(token.monoStyle, design: .monospaced).weight(.medium)
        }
        return .system(token.monoStyle).weight(.heavy).width(.expanded)
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
        /// The circle is the product: on a 393pt phone it takes two thirds of
        /// the width instead of reading as a badge.
        static let hero: CGFloat = 300
        static let ride: CGFloat = 176
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
    static let defaultValue = MotoThemeName.night
}

private struct MotoPaletteKey: EnvironmentKey {
    static let defaultValue = MotoPalette.night
}

private struct MotoTypeKey: EnvironmentKey {
    static let defaultValue = MotoTypography(theme: .night)
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

    /// The instrument band. Brand, not state: never a warning colour. Plates
    /// on it carry `onBand` type (17:1 in both GTA themes). It is
    /// read statically all over the app, so it follows the live theme through
    /// `MotoAppearance` rather than through the environment.
    @MainActor static var band: Color { MotoAppearance.shared.theme.palette.band }
    @MainActor static var onBand: Color { MotoAppearance.shared.theme.palette.onBand }
}

// MARK: - Live theme

/// The phone's current look. One source rather than an environment value,
/// because presented sheets apply their own `.motoTheme` and the brand band is
/// read statically; both need the live theme without a view hierarchy above
/// them. `AppModel` is the only writer.
@MainActor
final class MotoAppearance: ObservableObject {
    static let shared = MotoAppearance()

    @Published private(set) var theme: MotoThemeName = .night

    func apply(_ theme: MotoThemeName) {
        guard theme != self.theme else { return }
        self.theme = theme
    }
}

private extension Font.TextStyle {
    var uiTextStyle: UIFont.TextStyle {
        switch self {
        case .largeTitle: return .largeTitle
        case .title: return .title1
        case .title2: return .title2
        case .title3: return .title3
        case .headline: return .headline
        case .subheadline: return .subheadline
        case .callout: return .callout
        case .footnote: return .footnote
        case .caption: return .caption1
        case .caption2: return .caption2
        default: return .body
        }
    }
}

extension MotoThemeName {
    init(look: PhoneThemeLook) {
        switch look {
        case .original: self = .original
        case .gtaDay: self = .day
        case .gtaNight: self = .night
        }
    }
}

// MARK: - Theme menu

extension DeviceSettingsTheme {
    /// The order the theme sheet lists the choices in.
    static let menuOrder: [DeviceSettingsTheme] = [.auto, .day, .night, .official]

    var menuTitle: String {
        switch self {
        case .official: return "原版"
        case .auto: return "GTA · 自动"
        case .day: return "GTA · 白天"
        case .night: return "GTA · 夜间"
        }
    }

    var menuDetail: String {
        switch self {
        case .official: return "项目最初的黑底白线界面"
        case .auto: return "跟随手机所在地的日出与日落切换"
        case .day: return "Liberty City 白天：白纸黑字，强光下也看得清"
        case .night: return "Liberty City 夜间：纯黑底、霓虹粉 HUD"
        }
    }
}
