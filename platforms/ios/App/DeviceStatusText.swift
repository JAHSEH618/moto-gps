import Foundation

/// Shared device wording. One source so the home row, the status band and the
/// device sheet can never drift apart.
extension AppModel {
    var deviceStatusText: String {
        switch device.connection {
        case .connected: return deviceReady ? "已连接" : "正在准备"
        case .scanning: return "正在寻找圆屏"
        case .connecting: return "正在连接"
        case .failed: return "连接未成功"
        case .bluetoothUnavailable: return "蓝牙不可用"
        case .idle: return "未连接"
        }
    }
}
