import SwiftUI
import UIKit

// MARK: - Counter plate

/// A counter plate: a square ink block carrying one numeral or one glyph in
/// the paper colour, the way GTA parks cash and the round screen parks BEST.
/// It indexes a section, numbers a sheet after its place in the menu, and
/// marks the chosen route. On the band it inverts against the band instead,
/// the way the cover's logo panel sits in its white gutter.
struct MotoCounterPlate: View {
    enum Mark {
        case number(String)
        case symbol(String)
    }

    enum Size { case section, header }

    let mark: Mark
    var size: Size = .section
    var onBand = false

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type

    var body: some View {
        glyph
            .foregroundStyle(onBand ? MotoPalette.band : palette.bg)
            .lineLimit(1)
            .minimumScaleFactor(0.6)
            .padding(.horizontal, size == .header ? MotoMetrics.s3 : MotoMetrics.s2)
            .frame(minWidth: side, minHeight: side)
            .background(onBand ? MotoPalette.onBand : palette.ink)
            // A plate is a fixed object, like the instrument's counters; it
            // grows a little with the text and then holds.
            .dynamicTypeSize(...DynamicTypeSize.xxxLarge)
            .accessibilityHidden(true)
    }

    @ViewBuilder
    private var glyph: some View {
        switch mark {
        case .number(let value):
            Text(value)
                .font(type.display(size == .header ? .displayLG : .displayMD))
                .tracking(MotoMetrics.displayTracking)
                .monospacedDigit()
        case .symbol(let name):
            Image(systemName: name)
                .font(.system(size == .header ? .title : .title3, weight: .heavy))
        }
    }

    private var side: CGFloat { size == .header ? 64 : MotoMetrics.minTapTarget }
}

// MARK: - Segment control

/// Three-way poster selector. A stock `Picker` with `.segmented` styling brings
/// its own rounded pill and system blue; this one is square, fills the chosen
/// cell with the accent (the colour of an active selection) and keeps the
/// labels at body size so Chinese stays readable.
struct MotoSegment<Value: Hashable>: View {
    let options: [(value: Value, title: String)]
    @Binding var selection: Value
    var identifierPrefix: String

    @Environment(\.motoPalette) private var palette

    var body: some View {
        HStack(spacing: 0) {
            ForEach(Array(options.enumerated()), id: \.offset) { index, option in
                if index > 0 {
                    Rectangle()
                        .fill(palette.ink)
                        .frame(width: MotoMetrics.hairline * 2)
                }
                segment(option)
            }
        }
        .overlay(
            Rectangle()
                .strokeBorder(palette.ink, lineWidth: MotoMetrics.hairline * 2)
        )
    }

    private func segment(_ option: (value: Value, title: String)) -> some View {
        let isSelected = option.value == selection
        return Button {
            selection = option.value
        } label: {
            Text(option.title)
                .font(.system(.body, weight: isSelected ? .heavy : .semibold))
                .foregroundStyle(isSelected ? palette.onAccent : palette.ink)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .padding(.horizontal, MotoMetrics.s2)
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(isSelected ? palette.accent : palette.surface)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(isSelected ? [.isButton, .isSelected] : .isButton)
        .accessibilityIdentifier("\(identifierPrefix)-\(option.title)")
    }
}

// MARK: - Rows

/// A poster row: label left, value right, 1px rule underneath. No rounded
/// container, no chevron unless the row actually navigates.
struct MotoRow<Trailing: View>: View {
    let title: String
    var subtitle: String?
    /// Text values sit on the title's baseline; a control (a switch) centres.
    var alignment: VerticalAlignment = .firstTextBaseline
    @ViewBuilder var trailing: () -> Trailing

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        HStack(alignment: alignment, spacing: MotoMetrics.s3) {
            VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                Text(title)
                    .font(.system(.body, weight: .semibold))
                    .foregroundStyle(palette.ink)
                if let subtitle {
                    Text(subtitle)
                        .font(.system(.subheadline))
                        .foregroundStyle(palette.inkMuted(contrast: contrast))
                }
            }
            Spacer(minLength: MotoMetrics.s2)
            trailing()
        }
        .padding(.horizontal, MotoMetrics.s4)
        .padding(.vertical, MotoMetrics.s3)
        .frame(minHeight: 52)
    }
}

/// Toggle row. The switch keeps the platform shape (HIG conformance beats a
/// square knob the rider has never seen) and takes the theme's switch tint,
/// so its white thumb is never set on a white track.
struct MotoToggleRow: View {
    let title: String
    @Binding var isOn: Bool
    var identifier: String

    @Environment(\.motoTheme) private var theme

    var body: some View {
        MotoRow(title: title, alignment: .center) {
            Toggle("", isOn: $isOn)
                .labelsHidden()
                .tint(theme.switchTint)
                .accessibilityIdentifier(identifier)
                .accessibilityLabel(title)
        }
        // No row-level tap: the switch already handles its own gesture, and a
        // second one on the row would toggle twice and appear dead.
    }
}

// MARK: - Pause-menu rows

private struct MotoRowPressedKey: EnvironmentKey {
    static let defaultValue = false
}

extension EnvironmentValues {
    /// True while a pause-menu row is held, so its content can flip to the
    /// accent's own ink.
    var motoRowPressed: Bool {
        get { self[MotoRowPressedKey.self] }
        set { self[MotoRowPressedKey.self] = newValue }
    }
}

/// GTA's pause menu lights the item under the cursor with a solid bar. Held,
/// a row does the same: it fills with the accent and its type turns to the
/// accent's ink. A fill, never a scale; one light tick on the press.
struct MotoMenuRowStyle: ButtonStyle {
    @Environment(\.motoPalette) private var palette

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? palette.accent : Color.clear)
            .environment(\.motoRowPressed, configuration.isPressed)
            .contentShape(Rectangle())
            .onChange(of: configuration.isPressed) { _, pressed in
                guard pressed else { return }
                UIImpactFeedbackGenerator(style: .light).impactOccurred()
            }
    }
}

/// One pause-menu item: the index in the condensed face at panel size, the
/// title heavy, one line of detail, and a trailing glyph. The row carries
/// its own inset so the pressed bar runs past the text the way GTA's does;
/// the list that holds it pulls it back out by the same inset.
struct MotoMenuRow: View {
    let number: String
    let title: String
    var detail: String?
    var symbol: String?
    var symbolTint: Color?
    var selected = false

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type
    @Environment(\.colorSchemeContrast) private var contrast
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    @Environment(\.motoRowPressed) private var pressed

    /// The bar's overhang past the text column.
    static let inset = MotoMetrics.s3

    var body: some View {
        HStack(spacing: MotoMetrics.s3) {
            // Page ink, not the HUD pink: eight indices in the HUD colour
            // would be eight readings competing with the one that matters.
            Text(number)
                .font(type.display(.displayMD))
                .tracking(MotoMetrics.displayTracking)
                .monospacedDigit()
                .foregroundStyle(pressed ? palette.onAccent : (selected ? palette.accent : palette.ink))
                .lineLimit(1)
                .dynamicTypeSize(...DynamicTypeSize.accessibility1)
                .frame(minWidth: 44, alignment: .leading)

            VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                Text(title)
                    .font(.system(.title3, weight: .heavy))
                    .foregroundStyle(pressed ? palette.onAccent : palette.ink)
                    .fixedSize(horizontal: false, vertical: true)
                if let detail {
                    Text(detail)
                        .font(.system(.subheadline))
                        .foregroundStyle(pressed ? palette.onAccent : palette.inkMuted(contrast: contrast))
                        .lineLimit(dynamicTypeSize.isAccessibilitySize ? nil : 2)
                }
            }
            Spacer(minLength: 0)
            if let symbol {
                Image(systemName: symbol)
                    .font(.system(.body, weight: .semibold))
                    .foregroundStyle(pressed ? palette.onAccent : (symbolTint ?? palette.inkMuted))
                    .accessibilityHidden(true)
            }
        }
        .padding(.horizontal, Self.inset)
        .padding(.vertical, MotoMetrics.s3)
        .frame(minHeight: 72)
    }
}

// MARK: - Sheet scaffold

/// The band header shared by sheets and pushed pages: the cover's gutter run
/// full bleed, a counter plate carrying the sheet's number in the menu (or a
/// glyph for a sheet one level deeper), the title heavy beside it, and the
/// actions on the band as boxed ink type. A 2pt accent rule closes it, the
/// same rule that closes the home status band. No eyebrow above the title:
/// the plate carries the order.
private struct MotoPosterHeader<Leading: View, Actions: View>: View {
    let plate: MotoCounterPlate.Mark?
    let title: String
    @ViewBuilder var leading: () -> Leading
    @ViewBuilder var actions: () -> Actions

    @Environment(\.motoPalette) private var palette

    var body: some View {
        HStack(alignment: .center, spacing: MotoMetrics.s3) {
            leading()
            if let plate {
                MotoCounterPlate(mark: plate, size: .header, onBand: true)
            }
            Text(title)
                .font(.system(.largeTitle, weight: .heavy))
                .foregroundStyle(MotoPalette.onBand)
                .lineLimit(2)
                .minimumScaleFactor(0.7)
                .frame(maxWidth: .infinity, alignment: .leading)
                .accessibilityAddTraits(.isHeader)
            actions()
        }
        .padding(.leading, MotoMetrics.s4)
        .padding(.trailing, MotoMetrics.s3)
        .padding(.top, MotoMetrics.s5)
        .padding(.bottom, MotoMetrics.s4)
        .frame(maxWidth: .infinity, alignment: .leading)
        // The header is the sheet's title bar: it grows with the reader's
        // size up to a point, then holds so the content keeps the screen.
        .dynamicTypeSize(...DynamicTypeSize.accessibility2)
        .background(MotoPalette.band)
        .overlay(alignment: .bottom) {
            Rectangle().fill(palette.accent).frame(height: 2)
        }
    }
}

/// Poster sheet chrome: the band header, then paper. The band is the same
/// object as the instrument's rim (brand, not state), which is why the sheet
/// has no navigation bar and no large title, and why its corners are square.
struct MotoPosterSheet<Actions: View, Content: View>: View {
    let plate: MotoCounterPlate.Mark?
    let title: String
    let sheetIdentifier: String
    @ViewBuilder var actions: () -> Actions
    @ViewBuilder var content: () -> Content

    @Environment(\.motoPalette) private var palette

    var body: some View {
        VStack(spacing: 0) {
            MotoPosterHeader(plate: plate, title: title, leading: { EmptyView() }, actions: actions)
            ScrollView {
                VStack(spacing: MotoMetrics.s6) {
                    content()
                }
                .padding(.horizontal, MotoMetrics.s4)
                .padding(.top, MotoMetrics.s5)
                .padding(.bottom, MotoMetrics.s7)
            }
        }
        .background(palette.bg)
        .presentationCornerRadius(MotoMetrics.chipRadius)
        .accessibilityIdentifier(sheetIdentifier)
    }
}

extension MotoPosterSheet where Actions == MotoBandButton {
    init(
        plate: MotoCounterPlate.Mark?,
        title: String,
        sheetIdentifier: String,
        dismissTitle: String,
        dismissIdentifier: String,
        onDismiss: @escaping () -> Void,
        @ViewBuilder content: @escaping () -> Content
    ) {
        self.init(
            plate: plate,
            title: title,
            sheetIdentifier: sheetIdentifier,
            actions: { MotoBandButton(dismissTitle, identifier: dismissIdentifier, action: onDismiss) },
            content: content
        )
    }
}

/// A header action: ink type on the band, boxed in a 2pt ink frame so it reads
/// as a control and not as a caption. Never a second filled plate.
struct MotoBandButton: View {
    let title: String
    let identifier: String
    let action: () -> Void

    init(_ title: String, identifier: String, action: @escaping () -> Void) {
        self.title = title
        self.identifier = identifier
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(.body, weight: .heavy))
        }
        .buttonStyle(MotoBandPressStyle(boxed: true))
        .accessibilityIdentifier(identifier)
    }
}

/// The quieter header action beside a boxed one (Cancel next to Save): the
/// same ink, no frame.
struct MotoHeaderButton: View {
    let title: String
    let identifier: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(.body, weight: .semibold))
        }
        .buttonStyle(MotoBandPressStyle(boxed: false))
        .accessibilityIdentifier(identifier)
    }
}

/// Press feedback for the band's actions. Held, the action inverts into a
/// small ink plate with band-colour type, the menu's highlight in miniature;
/// disabled, it drops to 40%. Every action keeps a 44pt target.
struct MotoBandPressStyle: ButtonStyle {
    var boxed: Bool

    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        let pressed = configuration.isPressed
        configuration.label
            .foregroundStyle(pressed ? MotoPalette.band : MotoPalette.onBand)
            .lineLimit(1)
            .padding(.horizontal, MotoMetrics.s3)
            .frame(minWidth: MotoMetrics.minTapTarget, minHeight: MotoMetrics.minTapTarget)
            .background(pressed ? MotoPalette.onBand : Color.clear)
            .overlay(
                Rectangle().strokeBorder(
                    MotoPalette.onBand.opacity(boxed ? 1 : 0),
                    lineWidth: MotoMetrics.hairline * 2
                )
            )
            .opacity(isEnabled ? 1 : 0.4)
            .contentShape(Rectangle())
            .onChange(of: pressed) { _, pressed in
                guard pressed else { return }
                UIImpactFeedbackGenerator(style: .light).impactOccurred()
            }
    }
}

/// A pushed poster page: the same band header, with a back chevron at the
/// leading edge in place of a dismiss.
struct MotoPosterPage<Content: View>: View {
    let plate: MotoCounterPlate.Mark?
    let title: String
    let backIdentifier: String
    let onBack: () -> Void
    @ViewBuilder var content: () -> Content

    @Environment(\.motoPalette) private var palette

    var body: some View {
        VStack(spacing: 0) {
            MotoPosterHeader(
                plate: plate,
                title: title,
                leading: {
                    Button(action: onBack) {
                        Image(systemName: "chevron.left")
                            .font(.system(.title3, weight: .heavy))
                    }
                    .buttonStyle(MotoBandPressStyle(boxed: false))
                    .padding(.leading, -MotoMetrics.s3)
                    .accessibilityLabel("返回")
                    .accessibilityIdentifier(backIdentifier)
                },
                actions: { EmptyView() }
            )
            ScrollView {
                VStack(spacing: MotoMetrics.s6) {
                    content()
                }
                .padding(.horizontal, MotoMetrics.s4)
                .padding(.top, MotoMetrics.s5)
                .padding(.bottom, MotoMetrics.s7)
            }
        }
        .background(palette.bg)
        .navigationBarBackButtonHidden(true)
        .toolbar(.hidden, for: .navigationBar)
    }
}

// MARK: - Prose

/// Body copy on paper. Sentence case, real line spacing, never mono: the poster
/// grammar puts monospace on caps labels and data, and prose in the reading face.
struct MotoProse: View {
    let text: String
    var tone: Tone = .ink

    enum Tone { case ink, muted }

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        Text(text)
            .font(.system(.body))
            .foregroundStyle(tone == .ink ? palette.ink : palette.inkMuted(contrast: contrast))
            .lineSpacing(3)
            .fixedSize(horizontal: false, vertical: true)
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}

// MARK: - Sections

/// Numbered section head. The index sits on an ink counter plate that stands
/// on a 2pt ink rule, like a tab on a file: the number is order, and the
/// plate makes four groups of prose scannable without an eyebrow above each.
struct MotoSectionTitle: View {
    var index: String?
    let text: String

    @Environment(\.motoPalette) private var palette

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .bottom, spacing: MotoMetrics.s3) {
                if let index {
                    MotoCounterPlate(mark: .number(index))
                }
                Text(text)
                    .font(.system(.title3, weight: .heavy))
                    .foregroundStyle(palette.ink)
                    .padding(.bottom, MotoMetrics.s2)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .accessibilityAddTraits(.isHeader)
            }
            Rectangle()
                .fill(palette.ink)
                .frame(height: MotoMetrics.hairline * 2)
        }
    }
}

// MARK: - Hero

/// One enormous word per sheet. The instrument's rule is that nothing competes
/// with the hero, so this is the only thing above `title` size on the screen.
/// The line under it is a sentence, so it is set in the reading face.
struct MotoHeroWord: View {
    let word: String
    var detail: String?
    var tone: Tone = .ink

    enum Tone { case ink, band }

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s2) {
            Text(word)
                .font(type.display(.displayXL))
                .tracking(MotoMetrics.displayTracking)
                .foregroundStyle(tone == .band ? MotoPalette.band : palette.ink)
                .lineLimit(1)
                .minimumScaleFactor(0.5)
                .dynamicTypeSize(...DynamicTypeSize.accessibility1)
            if let detail {
                Text(detail)
                    .font(.system(.body, weight: .medium))
                    .foregroundStyle(palette.inkMuted(contrast: contrast))
                    .lineSpacing(2)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
    }
}

// MARK: - Link row

/// Third-party link or a disclosure that leaves the sheet. The arrow is the
/// band colour because it is an action, not data.
struct MotoLinkRow: View {
    let title: String
    let destination: URL
    var identifier: String?

    @Environment(\.motoPalette) private var palette
    @Environment(\.openURL) private var openURL

    var body: some View {
        Button {
            openURL(destination)
        } label: {
            HStack(spacing: MotoMetrics.s3) {
                Text(title)
                    .font(.system(.body))
                    .foregroundStyle(palette.ink)
                    .multilineTextAlignment(.leading)
                Spacer(minLength: MotoMetrics.s2)
                Image(systemName: "arrow.up.right")
                    .font(.system(.footnote, weight: .bold))
                    .foregroundStyle(MotoPalette.band)
                    .accessibilityHidden(true)
            }
            .padding(.horizontal, MotoMetrics.s4)
            .frame(minHeight: 52)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(identifier ?? "poster-link-\(title)")
    }
}
