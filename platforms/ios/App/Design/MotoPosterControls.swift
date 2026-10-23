import SwiftUI

// MARK: - Band plate

/// The magenta plate. On the round screen the band is brand, not state: it never
/// changes between day and night and it is never a warning colour. It appears
/// here for the same reason — one filled surface that says "this is chosen",
/// carrying ink rather than white.
struct MotoBandFill: View {
    var body: some View {
        Rectangle()
            .fill(MotoPalette.band)
            .accessibilityHidden(true)
    }
}

// MARK: - Segment control

/// Three-way poster selector. A stock `Picker` with `.segmented` styling brings
/// its own rounded pill and system blue; this one is square, uses the band for
/// the selection and keeps the labels at body size so Chinese stays readable.
struct MotoSegment<Value: Hashable>: View {
    let options: [(value: Value, title: String)]
    @Binding var selection: Value
    var identifierPrefix: String

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        HStack(spacing: 0) {
            ForEach(Array(options.enumerated()), id: \.offset) { index, option in
                if index > 0 {
                    Rectangle()
                        .fill(palette.line(contrast: contrast))
                        .frame(width: MotoMetrics.hairline)
                }
                segment(option)
            }
        }
        .overlay(
            Rectangle()
                .strokeBorder(palette.line(contrast: contrast), lineWidth: MotoMetrics.hairline)
        )
    }

    private func segment(_ option: (value: Value, title: String)) -> some View {
        let isSelected = option.value == selection
        return Button {
            selection = option.value
        } label: {
            Text(option.title)
                .font(.system(.body, weight: .semibold))
                .foregroundStyle(isSelected ? MotoPalette.onBand : palette.ink)
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(isSelected ? MotoPalette.band : palette.surface)
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
    @ViewBuilder var trailing: () -> Trailing

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s3) {
            VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                Text(title)
                    .font(.system(.body))
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

/// Toggle row. The switch keeps the platform shape — HIG conformance beats a
/// square knob the rider has never seen — but it is tinted with the band.
struct MotoToggleRow: View {
    let title: String
    @Binding var isOn: Bool
    var identifier: String

    @Environment(\.motoPalette) private var palette

    var body: some View {
        MotoRow(title: title) {
            Toggle("", isOn: $isOn)
                .labelsHidden()
                .tint(MotoPalette.band)
                .accessibilityIdentifier(identifier)
                .accessibilityLabel(title)
        }
        // No row-level tap: the switch already handles its own gesture, and a
        // second one on the row would toggle twice and appear dead.
    }
}

// MARK: - Sheet scaffold

/// Poster sheet chrome: a full-bleed magenta band carrying the title, then
/// paper. The band is the same object as the instrument's rim — brand, not
/// state — and it is why the sheet has no navigation bar and no large title.
struct MotoPosterSheet<Actions: View, Content: View>: View {
    let kicker: String
    let title: String
    let sheetIdentifier: String
    @ViewBuilder var actions: () -> Actions
    @ViewBuilder var content: () -> Content

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type

    var body: some View {
        VStack(spacing: 0) {
            header
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
        .accessibilityIdentifier(sheetIdentifier)
    }

    private var header: some View {
        HStack(alignment: .bottom, spacing: MotoMetrics.s3) {
            VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                Text(kicker)
                    .font(type.mono(.micro))
                    .tracking(MotoMetrics.microTracking)
                    .foregroundStyle(MotoPalette.onBand.opacity(0.72))
                Text(title)
                    .font(.system(.largeTitle, weight: .heavy))
                    .foregroundStyle(MotoPalette.onBand)
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
            }
            Spacer(minLength: MotoMetrics.s2)
            actions()
                .padding(.bottom, MotoMetrics.s1)
        }
        .padding(.horizontal, MotoMetrics.s4)
        .padding(.top, MotoMetrics.s5)
        .padding(.bottom, MotoMetrics.s3)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(MotoPalette.band)
    }
}

extension MotoPosterSheet where Actions == MotoBandButton {
    init(
        kicker: String,
        title: String,
        sheetIdentifier: String,
        dismissTitle: String,
        dismissIdentifier: String,
        onDismiss: @escaping () -> Void,
        @ViewBuilder content: @escaping () -> Content
    ) {
        self.init(
            kicker: kicker,
            title: title,
            sheetIdentifier: sheetIdentifier,
            actions: { MotoBandButton(dismissTitle, identifier: dismissIdentifier, action: onDismiss) },
            content: content
        )
    }
}

/// A header action. On the band, so it is ink type rather than a second plate:
/// one filled surface per screen already.
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
                .font(.system(.subheadline, weight: .bold))
                .foregroundStyle(MotoPalette.onBand)
                .padding(.vertical, MotoMetrics.s2)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(identifier)
    }
}

/// Header text action, for a sheet that has two.
struct MotoHeaderButton: View {
    let title: String
    let identifier: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(.subheadline, weight: .medium))
                .foregroundStyle(MotoPalette.onBand.opacity(0.8))
                .padding(.vertical, MotoMetrics.s2)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(identifier)
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

/// Numbered section head. The index is a poster numeral, not decoration: it is
/// how the pause menu reads, and it makes four groups of prose scannable without
/// a tracked eyebrow above each one.
struct MotoSectionTitle: View {
    var index: String?
    let text: String

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s2) {
            HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s3) {
                if let index {
                    Text(index)
                        .font(type.display(.displayMD))
                        .tracking(MotoMetrics.displayTracking)
                        .foregroundStyle(MotoPalette.band)
                        .accessibilityHidden(true)
                }
                Text(text)
                    .font(.system(.headline, weight: .bold))
                    .foregroundStyle(palette.ink)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            MotoRule()
        }
    }
}

// MARK: - Hero

/// One enormous word per sheet. The instrument's rule is that nothing competes
/// with the hero, so this is the only thing above `title` size on the screen.
struct MotoHeroWord: View {
    let word: String
    var detail: String?
    var tone: Tone = .ink

    enum Tone { case ink, band }

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type

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
                    .font(type.mono(.label))
                    .tracking(MotoMetrics.labelTracking)
                    .foregroundStyle(palette.inkMuted)
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
