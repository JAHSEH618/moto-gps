import Charts
import MotoNavigationCore
import SwiftUI

/// MENU → 09. Every navigation the phone recorded (ADR 0002): one row a ride,
/// newest first, and the open ride's speed over time. A logbook like the race
/// board beside it; 0-100 runs stay there, a ride only lists the ones that
/// finished during it. No map and no track: the ride never kept a position.
struct RideLogView: View {
    @ObservedObject var model: AppModel
    let onDismiss: () -> Void

    @State private var openRide: UUID?
    @State private var confirmsClear = false
    @Environment(\.motoPalette) private var palette

    var body: some View {
        let rides = model.rideLog.rides
        MotoPosterSheet(
            plate: .number("09"),
            title: "骑行记录",
            sheetIdentifier: "ride-log-sheet",
            dismissTitle: "完成",
            dismissIdentifier: "ride-log-done",
            onDismiss: onDismiss
        ) {
            if rides.isEmpty {
                emptyState
            } else {
                totals(rides)
                VStack(spacing: 0) {
                    ForEach(rides) { ride in
                        RideRow(
                            ride: ride,
                            isOpen: openRide == ride.id,
                            onToggle: { openRide = openRide == ride.id ? nil : ride.id },
                            onDelete: { model.deleteRide(ride.id) }
                        )
                        MotoRule()
                    }
                }
                Button("清除全部骑行记录") { confirmsClear = true }
                    .buttonStyle(MotoSecondaryButtonStyle())
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .accessibilityIdentifier("ride-log-clear")
            }
        }
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
        .confirmationDialog("清除全部骑行记录？", isPresented: $confirmsClear, titleVisibility: .visible) {
            Button("清除全部记录", role: .destructive) { model.clearRideLog() }
            Button("取消", role: .cancel) {}
        } message: {
            Text("所有骑行的距离、时长和车速会从这台手机上删除，无法恢复。赛道看板不受影响。")
        }
    }

    private var emptyState: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            Text("还没有骑行")
                .font(.system(.title2, weight: .bold))
                .foregroundStyle(palette.ink)
            Text("每次开始导航都会自动记一笔：距离、时长、车速，连着圆屏时还有最大倾角和途中完成的 0-100。只记数字，不记路线。")
                .font(.system(.body))
                .foregroundStyle(palette.inkMuted)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.vertical, MotoMetrics.s5)
    }

    private func totals(_ rides: [RideRecord]) -> some View {
        HStack(alignment: .lastTextBaseline) {
            VStack(alignment: .leading, spacing: 0) {
                MotoLabel(text: "TOTAL", tone: .accent, token: .micro)
                RaceFigure(value: RideFormat.kilometres(model.rideLog.totalDistanceM), unit: "km", token: .displayXL)
            }
            Spacer(minLength: MotoMetrics.s3)
            RaceSlab(label: "次数", value: "\(rides.count)")
                .frame(width: 120)
        }
    }
}

enum RideFormat {
    static func kilometres(_ metres: Double) -> String {
        String(format: metres >= 100_000 ? "%.0f" : "%.1f", metres / 1_000)
    }

    static func duration(_ seconds: Double) -> String {
        let minutes = Int((seconds / 60).rounded())
        return minutes >= 60 ? "\(minutes / 60) 小时 \(minutes % 60) 分" : "\(minutes) 分钟"
    }

    static func when(_ date: Date) -> String {
        date.formatted(.dateTime.month().day().hour().minute())
    }
}

private struct RideRow: View {
    let ride: RideRecord
    let isOpen: Bool
    let onToggle: () -> Void
    let onDelete: () -> Void

    @Environment(\.motoPalette) private var palette

    var body: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            Button(action: onToggle) {
                HStack(alignment: .firstTextBaseline, spacing: MotoMetrics.s3) {
                    VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                        Text(ride.destinationName ?? "导航")
                            .font(.system(.body, weight: .semibold))
                            .foregroundStyle(palette.ink)
                            .lineLimit(1)
                        Text(RideFormat.when(ride.startedAt) + (ride.arrived ? " · 已到达" : ""))
                            .font(.system(.footnote))
                            .foregroundStyle(palette.inkMuted)
                    }
                    Spacer(minLength: MotoMetrics.s2)
                    Text("\(RideFormat.kilometres(ride.distanceM)) km")
                        .font(.system(.body, design: .monospaced, weight: .semibold))
                        .foregroundStyle(palette.ink)
                    Image(systemName: isOpen ? "chevron.up" : "chevron.down")
                        .font(.system(.caption2, weight: .bold))
                        .foregroundStyle(palette.inkMuted)
                }
                .frame(minHeight: MotoMetrics.minTapTarget)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityIdentifier("ride-row")

            if isOpen { detail }
        }
        .padding(.vertical, MotoMetrics.s2)
    }

    private var detail: some View {
        VStack(alignment: .leading, spacing: MotoMetrics.s3) {
            let columns = [GridItem(.flexible(), spacing: MotoMetrics.s2), GridItem(.flexible())]
            LazyVGrid(columns: columns, alignment: .leading, spacing: MotoMetrics.s2) {
                RaceSlab(label: "时长", value: RideFormat.duration(ride.durationSeconds))
                RaceSlab(label: "行驶", value: RideFormat.duration(ride.movingSeconds))
                RaceSlab(label: "均速", value: ride.averageMovingSpeedKph.map { String(format: "%.0f", $0) } ?? "--")
                RaceSlab(label: "最高", value: String(format: "%.0f", ride.maxSpeedKph))
                RaceSlab(label: "左倾", value: ride.maxLeanLeftDeg.map { String(format: "%.0f°", $0) } ?? "--")
                RaceSlab(label: "右倾", value: ride.maxLeanRightDeg.map { String(format: "%.0f°", $0) } ?? "--")
            }
            if ride.speedProfile.count > 1 {
                Chart(Array(ride.speedProfile.enumerated()), id: \.offset) { _, sample in
                    LineMark(
                        x: .value("分钟", sample.offsetSeconds / 60),
                        y: .value("km/h", sample.speedKph)
                    )
                    .foregroundStyle(palette.ink)
                    .lineStyle(StrokeStyle(lineWidth: 2))
                }
                .chartYScale(domain: .automatic(includesZero: true))
                .raceYAxis(palette)
                .chartXAxis {
                    AxisMarks { value in
                        AxisValueLabel {
                            if let minute = value.as(Double.self) {
                                Text("\(Int(minute.rounded()))′")
                                    .font(.system(.caption2, design: .monospaced, weight: .medium))
                                    .foregroundStyle(palette.inkMuted)
                            }
                        }
                    }
                }
                .frame(height: 150)
                .accessibilityLabel(String(format: "车速曲线，最高 %.0f 公里每小时", ride.maxSpeedKph))
            }
            if !ride.runs.isEmpty {
                VStack(alignment: .leading, spacing: MotoMetrics.s1) {
                    MotoLabel(text: "途中 0-100", tone: .muted, token: .micro, uppercased: false)
                    ForEach(Array(ride.runs.enumerated()), id: \.offset) { _, run in
                        Text(String(format: "0-%d · %.2f 秒", run.targetKph, Double(run.elapsedMs) / 1_000))
                            .font(.system(.footnote, design: .monospaced))
                            .foregroundStyle(palette.ink)
                    }
                }
            }
            Button("删除这次骑行", role: .destructive, action: onDelete)
                .font(.system(.footnote, weight: .semibold))
                .foregroundStyle(palette.abort)
                .frame(minHeight: MotoMetrics.minTapTarget)
                .accessibilityIdentifier("ride-delete")
        }
    }
}
