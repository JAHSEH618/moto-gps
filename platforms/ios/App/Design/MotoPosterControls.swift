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

/// Poster sheet chrome: paper ground, a band-thin rule at the top of the
/// content, square panels and a text-only dismiss in the corner. Replaces
/// `NavigationStack` + `Form`, which brought rounded cards and system blue.
struct MotoPosterSheet<Content: View>: View {
    let title: String
    let dismissTitle: String
    let sheetIdentifier: String
    let dismissIdentifier: String
    let onDismiss: () -> Void
    @ViewBuilder var content: () -> Content

    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        VStack(spacing: 0) {
            header
            MotoRule()
            ScrollView {
                VStack(spacing: MotoMetrics.s5) {
                    content()
                }
                .padding(.horizontal, MotoMetrics.s4)
                .padding(.top, MotoMetrics.s4)
                .padding(.bottom, MotoMetrics.s6)
            }
        }
        .background(palette.bg)
        .accessibilityIdentifier(sheetIdentifier)
    }

    private var header: some View {
        HStack(alignment: .center, spacing: MotoMetrics.s3) {
            Text(title)
                .font(.system(.title3, weight: .semibold))
                .foregroundStyle(palette.ink)
            Spacer(minLength: MotoMetrics.s2)
            Button(action: onDismiss) {
                Text(dismissTitle)
                    .font(.system(.body, weight: .semibold))
                    .foregroundStyle(MotoPalette.onBand)
                    .padding(.horizontal, MotoMetrics.s3)
                    .padding(.vertical, MotoMetrics.s2)
                    .background(MotoPalette.band)
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier(dismissIdentifier)
        }
        .padding(.horizontal, MotoMetrics.s4)
        // Clear of the sheet's rounded corner, which would otherwise clip the
        // band plate and read as a rendering bug.
        .padding(.top, MotoMetrics.s5)
        .padding(.bottom, MotoMetrics.s3)
    }
}

// MARK: - Section label

/// Kicker for a group of rows. Sentence case, ink-muted, no caps tracking: the
/// tracked caps label is reserved for mono data, and an eyebrow above every
/// section is exactly the template look DESIGN.md rejects.
struct MotoSectionTitle: View {
    let text: String
    @Environment(\.motoPalette) private var palette
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        Text(text)
            .font(.system(.subheadline, weight: .semibold))
            .foregroundStyle(palette.inkMuted(contrast: contrast))
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}
