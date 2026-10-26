import Charts
import MotoNavigationCore
import SwiftUI

/// MENU → 08. What the round screen has measured over time: every finished
/// 0-100, the time spent at each lean, and the G-G envelope, for today, the
/// last seven days or everything. Not a live display: the handlebar is the
/// live instrument; this is the logbook. Every value is a recorded one, and a
/// scope with nothing recorded says so instead of drawing zeros.
struct RaceDashboardView: View {
    @ObservedObject var model: AppModel
    let onDismiss: () -> Void

    @State private var scope: RaceLogScope = .week
    @State private var confirmsClear = false
    @Environment(\.motoPalette) private var palette

    var body: some View {
        let _ = model.raceLogRevision
        let summary = model.raceLog.summary(scope, now: Date())
        MotoPosterSheet(
            kicker: "RACE LOG",
            title: "赛道看板",
            sheetIdentifier: "race-dashboard-sheet",
            dismissTitle: "完成",
            dismissIdentifier: "race-dashboard-done",
            onDismiss: onDismiss
        ) {
            MotoSegment(
                options: [(.today, "今天"), (.week, "7 天"), (.all, "全部")],
                selection: $scope,
                identifierPrefix: "race-scope"
            )
            if summary.isEmpty {
                emptyState
            } else {
                RaceLaunchSection(summary: summary)
                RaceLeanSection(summary: summary)
                RaceGSection(summary: summary)
                footer(summary)
            }
        }
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
        .confirmationDialog("清除全部赛道记录？", isPresented: $confirmsClear, titleVisibility: .visible) {
            Button("清除全部记录", role: .destructive) { model.clearRaceLog() }
            Button("取消", role: .cancel) {}
        } message: {
            Text("0-100 成绩、倾角与 G 值记录会从这台手机上删除，无法恢复。圆屏不受影响。")
        }
    }

    private var emptyState: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            Text(scope == .all ? "还没有记录" : "这段时间没有记录")
                .font(.system(.title2, weight: .bold))
                .foregroundStyle(palette.ink)
            Text("连接圆屏后骑行，倾角与 G 值会自动累积在这里；在圆屏的 0-100 页停稳后起步，完成的成绩也会记下。")
                .font(.system(.body))
                .foregroundStyle(palette.inkMuted)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.vertical, MotoMetrics.s5)
    }

    private func footer(_ summary: RaceSummary) -> some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s4) {
            MotoLabel(
                text: String(format: "已记录 %@ 遥测 · 圆屏连接时累积", Self.duration(summary.seconds)),
                tone: .muted,
                token: .micro,
                uppercased: false
            )
            Button("清除全部记录") { confirmsClear = true }
                .buttonStyle(MotoSecondaryButtonStyle())
                .accessibilityIdentifier("race-clear-log")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    static func duration(_ seconds: Double) -> String {
        if seconds >= 3_600 { return String(format: "%.1f 小时", seconds / 3_600) }
        return String(format: "%.0f 分钟", max(1, seconds / 60))
    }
}

// MARK: - Shared pieces

/// A counter slab: ink plate, label and figure in `onSlab`, the way the round
/// screen parks BEST and KM/H.
private struct RaceSlab: View {
    let label: String
    let value: String

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s2) {
            MotoLabel(text: label, tone: .onSlab, token: .micro, uppercased: false)
            Text(value)
                .font(type.display(.displayMD))
                .monospacedDigit()
                .foregroundStyle(palette.onSlab)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .dynamicTypeSize(...DynamicTypeSize.accessibility1)
        }
        .padding(.horizontal, MotoMetrics.s3)
        .padding(.vertical, MotoMetrics.s1)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(palette.slab)
        .accessibilityElement(children: .combine)
    }
}

/// The section's one figure, at a size below the page's poster.
private struct RaceFigure: View {
    let value: String
    let unit: String
    var token: MotoText = .displayLG

    @Environment(\.motoPalette) private var palette
    @Environment(\.motoType) private var type
    @Environment(\.motoTheme) private var theme

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s1) {
            Text(value)
                .font(type.display(token))
                .tracking(MotoMetrics.displayTracking)
                .monospacedDigit()
                .foregroundStyle(palette.hud ?? palette.ink)
                .motoHudShadow(theme.hudShadow)
                .lineLimit(1)
                .dynamicTypeSize(...DynamicTypeSize.accessibility1)
            MotoLabel(text: unit, tone: .muted, token: .label, uppercased: false)
        }
    }
}

private extension View {
    /// The value axis in the system's mono caps, on its rules: no plot
    /// background, no system blue. Each chart owns its own x axis, because the
    /// modifier nearest the chart wins.
    func raceYAxis(_ palette: MotoPalette) -> some View {
        chartYAxis {
            AxisMarks(position: .leading) { _ in
                AxisGridLine(stroke: StrokeStyle(lineWidth: 1))
                    .foregroundStyle(palette.line)
                AxisValueLabel()
                    .font(.system(.caption2, design: .monospaced, weight: .medium))
                    .foregroundStyle(palette.inkMuted)
            }
        }
    }
}

// MARK: - 0-100

/// Every finished run in the scope, oldest first: the trend is the story, the
/// best is the poster. Times come from the round screen's clock.
private struct RaceLaunchSection: View {
    let summary: RaceSummary

    @Environment(\.motoPalette) private var palette

    private struct Point: Identifiable {
        let id: Int
        let seconds: Double
        let isBest: Bool
    }

    /// Every run up to twelve, then about six evenly spaced ones.
    static func runTicks(_ count: Int) -> [Int] {
        guard count > 12 else { return Array(1...max(count, 1)) }
        let step = Int((Double(count) / 6).rounded(.up))
        return Array(stride(from: 1, through: count, by: step))
    }

    var body: some View {
        let best = summary.bestRun
        let points = summary.runs.enumerated().map { index, run in
            Point(id: index + 1, seconds: run.seconds, isBest: run == best)
        }
        VStack(alignment: .leading, spacing: MotoMetrics.s4) {
            MotoSectionTitle(text: "0-\(best?.targetKph ?? 100) km/h")
            if let best {
                HStack(alignment: .lastTextBaseline) {
                    VStack(alignment: .leading, spacing: 0) {
                        MotoLabel(text: "BEST", tone: .accent, token: .micro)
                        RaceFigure(value: String(format: "%.2f", best.seconds), unit: "s", token: .displayXL)
                    }
                    Spacer(minLength: MotoMetrics.s3)
                    VStack(spacing: MotoMetrics.s2) {
                        RaceSlab(label: "平均", value: summary.averageRunSeconds.map { String(format: "%.2f", $0) } ?? "--")
                        RaceSlab(label: "次数", value: "\(summary.runs.count)")
                    }
                    .frame(width: 150)
                }
                if points.count > 1 {
                    Chart {
                        if let average = summary.averageRunSeconds {
                            RuleMark(y: .value("平均", average))
                                .foregroundStyle(palette.inkMuted)
                                .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 4]))
                        }
                        ForEach(points) { point in
                            LineMark(x: .value("第几次", point.id), y: .value("秒", point.seconds))
                                .foregroundStyle(palette.ink)
                                .lineStyle(StrokeStyle(lineWidth: 2))
                            PointMark(x: .value("第几次", point.id), y: .value("秒", point.seconds))
                                .foregroundStyle(point.isBest ? palette.accent : palette.ink)
                                .symbolSize(point.isBest ? 140 : 40)
                        }
                    }
                    .chartYScale(domain: .automatic(includesZero: false))
                    .chartXScale(domain: 0.5...(Double(points.count) + 0.5))
                    .raceYAxis(palette)
                    // Runs are counted from one; label the ones that exist.
                    .chartXAxis {
                        AxisMarks(values: Self.runTicks(points.count)) { value in
                            AxisValueLabel {
                                if let run = value.as(Int.self) {
                                    Text("#\(run)")
                                        .font(.system(.caption2, design: .monospaced, weight: .medium))
                                        .foregroundStyle(palette.inkMuted)
                                }
                            }
                        }
                    }
                    .frame(height: 170)
                    .accessibilityLabel(String(format: "%d 次 0-100，最佳 %.2f 秒", points.count, best.seconds))
                }
            } else {
                Text("这段时间没有完成的 0-100。")
                    .font(.system(.subheadline))
                    .foregroundStyle(palette.inkMuted)
            }
        }
    }
}

// MARK: - Lean

/// Time at each lean, left mirrored against right, 5° bins from 5° out; the
/// first ±5° is reported as a share instead, because upright time would flatten
/// every corner into the baseline.
private struct RaceLeanSection: View {
    let summary: RaceSummary

    @Environment(\.motoPalette) private var palette

    private struct Bin: Identifiable {
        let id: Int
        /// Signed bin centre: left negative.
        let degrees: Double
        let minutes: Double
        let hot: Bool
    }

    var body: some View {
        let bins = (1..<RaceLog.leanBins).flatMap { index -> [Bin] in
            let centre = (Double(index) + 0.5) * RaceLog.leanBinDegrees
            let hot = Double(index) * RaceLog.leanBinDegrees >= 45
            return [
                Bin(id: -index, degrees: -centre, minutes: summary.leftSeconds[index] / 60, hot: hot),
                Bin(id: index, degrees: centre, minutes: summary.rightSeconds[index] / 60, hot: hot),
            ]
        }
        let deepest = [summary.maxLeftDegrees, summary.maxRightDegrees].compactMap { $0 }.max()
        VStack(alignment: .leading, spacing: MotoMetrics.s4) {
            MotoSectionTitle(text: "倾角")
            HStack(alignment: .lastTextBaseline) {
                VStack(alignment: .leading, spacing: 0) {
                    MotoLabel(text: "最深", tone: .muted, token: .micro, uppercased: false)
                    RaceFigure(value: deepest.map { String(format: "%.0f°", $0) } ?? "--", unit: "")
                }
                Spacer(minLength: MotoMetrics.s3)
                VStack(spacing: MotoMetrics.s2) {
                    RaceSlab(label: "左", value: summary.maxLeftDegrees.map { String(format: "%.0f°", $0) } ?? "--")
                    RaceSlab(label: "右", value: summary.maxRightDegrees.map { String(format: "%.0f°", $0) } ?? "--")
                }
                .frame(width: 150)
            }
            Chart(bins) { bin in
                BarMark(
                    x: .value("倾角", bin.degrees),
                    y: .value("分钟", bin.minutes),
                    width: .fixed(11)
                )
                .foregroundStyle(bin.hot ? palette.abort : palette.route)
            }
            .chartXScale(domain: -60...60)
            .chartXAxis {
                AxisMarks(values: [-60, -45, -30, -15, 0, 15, 30, 45, 60]) { value in
                    AxisTick().foregroundStyle(palette.line)
                    AxisValueLabel {
                        if let degrees = value.as(Int.self) {
                            Text(degrees == 0 ? "0" : "\(degrees < 0 ? "L" : "R")\(abs(degrees))")
                                .font(.system(.caption2, design: .monospaced, weight: .medium))
                                .foregroundStyle(palette.inkMuted)
                        }
                    }
                }
            }
            .raceYAxis(palette)
            .frame(height: 170)
            .accessibilityLabel(accessibilityText)
            if let upright = summary.uprightShare {
                MotoLabel(
                    text: String(format: "纵轴为分钟 · ±5° 以内的直立时间占 %.0f%%", upright * 100),
                    tone: .muted,
                    token: .micro,
                    uppercased: false
                )
            }
        }
    }

    private var accessibilityText: String {
        let left = summary.maxLeftDegrees.map { "左侧最深 \(Int($0.rounded())) 度" } ?? "左侧无记录"
        let right = summary.maxRightDegrees.map { "右侧最深 \(Int($0.rounded())) 度" } ?? "右侧无记录"
        return "倾角分布，\(left)，\(right)"
    }
}

// MARK: - G

/// The friction circle's envelope, kept over the scope: the most the rides
/// asked of the tyres in each of 16 directions. Rings at 0.5 / 1.0 / 1.5 g.
private struct RaceGSection: View {
    let summary: RaceSummary

    @Environment(\.motoPalette) private var palette

    private static let fullScale = 1.5

    var body: some View {
        let peak = summary.envelope.max().flatMap { $0 > 0 ? $0 : nil }
        VStack(alignment: .leading, spacing: MotoMetrics.s4) {
            MotoSectionTitle(text: "G 值")
            HStack(alignment: .lastTextBaseline) {
                VStack(alignment: .leading, spacing: 0) {
                    MotoLabel(text: "最大合成", tone: .muted, token: .micro, uppercased: false)
                    RaceFigure(value: peak.map { String(format: "%.2f", $0) } ?? "--", unit: "G")
                }
                Spacer(minLength: 0)
            }
            Canvas { context, size in draw(in: &context, size: size) }
                .aspectRatio(1, contentMode: .fit)
                .frame(maxWidth: 300)
                .frame(maxWidth: .infinity)
                .accessibilityHidden(true)
            Grid(horizontalSpacing: MotoMetrics.s2, verticalSpacing: MotoMetrics.s2) {
                GridRow {
                    RaceSlab(label: "加速", value: Self.g(summary.maxAccelerationG))
                    RaceSlab(label: "制动", value: Self.g(summary.maxBrakingG))
                }
                GridRow {
                    RaceSlab(label: "左", value: Self.g(summary.maxLateralLeftG))
                    RaceSlab(label: "右", value: Self.g(summary.maxLateralRightG))
                }
            }
        }
        .accessibilityElement(children: .contain)
    }

    static func g(_ value: Double?) -> String {
        value.map { String(format: "%.2f", $0) } ?? "--"
    }

    private func draw(in context: inout GraphicsContext, size: CGSize) {
        let radius = min(size.width, size.height) / 2 - 16
        let center = CGPoint(x: size.width / 2, y: size.height / 2)
        let perG = radius / Self.fullScale

        for g in [0.5, 1.0, 1.5] {
            let r = perG * g
            let ring = Path(ellipseIn: CGRect(x: center.x - r, y: center.y - r, width: 2 * r, height: 2 * r))
            context.stroke(ring, with: .color(g == 1.0 ? palette.ink.opacity(0.55) : palette.line),
                           lineWidth: g == 1.5 ? 2 : 1)
        }
        var cross = Path()
        cross.move(to: CGPoint(x: center.x - radius, y: center.y))
        cross.addLine(to: CGPoint(x: center.x + radius, y: center.y))
        cross.move(to: CGPoint(x: center.x, y: center.y - radius))
        cross.addLine(to: CGPoint(x: center.x, y: center.y + radius))
        context.stroke(cross, with: .color(palette.line), lineWidth: 1)
        for (text, point) in [
            ("ACC", CGPoint(x: center.x, y: center.y - radius - 9)),
            ("BRK", CGPoint(x: center.x, y: center.y + radius + 9)),
            ("L", CGPoint(x: center.x - radius - 9, y: center.y)),
            ("R", CGPoint(x: center.x + radius + 9, y: center.y)),
        ] {
            context.draw(
                Text(text).font(.system(.caption2, design: .monospaced, weight: .bold))
                    .foregroundStyle(palette.inkMuted),
                at: point
            )
        }

        // A closed shape through every recorded direction; casing first, then
        // the pink, as on the round screen.
        let recorded = summary.envelope.enumerated().compactMap { index, g -> CGPoint? in
            guard g > 0 else { return nil }
            let angle = 2 * Double.pi * Double(index) / Double(RaceLog.sectors)
            let r = perG * min(g, Self.fullScale)
            return CGPoint(x: center.x + cos(angle) * r, y: center.y - sin(angle) * r)
        }
        guard recorded.count > 1 else { return }
        var shape = Path()
        shape.addLines(recorded)
        shape.closeSubpath()
        context.fill(shape, with: .color(palette.accent.opacity(0.14)))
        context.stroke(shape, with: .color(palette.slab), style: StrokeStyle(lineWidth: 6, lineJoin: .round))
        context.stroke(shape, with: .color(palette.accent), style: StrokeStyle(lineWidth: 3, lineJoin: .round))
    }
}
