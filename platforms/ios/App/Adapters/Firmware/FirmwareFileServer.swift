import Foundation
import MotoNavigationCore
import Network

/// Serves one firmware file over the iPhone hotspot so the round screen can
/// fetch it (WAVESHARE_OPTIMIZATION_PLAN §4.4). The image never crosses BLE:
/// the phone already has the bytes, so handing over a URL on the link both
/// ends already share is the shortest path.
///
/// The listener answers exactly one request — a `GET /moto-gps.bin` from the
/// device the update was sent to — and stops itself when the update ends, the
/// rider cancels, or the sheet closes. It binds to the hotspot bridge address
/// only, so it is not reachable from the rest of the phone's networks.
///
/// Not `@MainActor`: the receive loop belongs on its own serial queue, and the
/// only hop to the UI is the "bytes served" note.
final class FirmwareFileServer {
    enum ServerError: LocalizedError {
        case noHotspotAddress
        case listenerFailed(String)

        var errorDescription: String? {
            switch self {
            case .noHotspotAddress:
                "找不到个人热点地址。请先在「设置 → 个人热点」打开热点（iPhone 12 及以后要打开「最大兼容性」）。"
            case let .listenerFailed(reason):
                "固件服务启动失败：\(reason)"
            }
        }
    }

    private let queue = DispatchQueue(label: "MotoGPS.FirmwareFileServer")
    private var listener: NWListener?
    private var image = Data()
    private var onServed: ((UInt32) -> Void)?

    /// The address the hotspot hands the phone: `bridge100` on iOS, in
    /// `172.20.10.0/28`. Nil when the hotspot is off, which is a state the
    /// screen can explain rather than guessing an address nothing answers on.
    static func hotspotAddress() -> String? {
        var address: String?
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0, let first = head else { return nil }
        defer { freeifaddrs(head) }
        var pointer: UnsafeMutablePointer<ifaddrs>? = first
        while let entry = pointer {
            pointer = entry.pointee.ifa_next
            let name = String(cString: entry.pointee.ifa_name)
            guard name.hasPrefix("bridge"), let sa = entry.pointee.ifa_addr,
                  sa.pointee.sa_family == UInt8(AF_INET)
            else { continue }
            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            if getnameinfo(sa, socklen_t(sa.pointee.sa_len), &host,
                           socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0 {
                let text = String(cString: host)
                if text.hasPrefix("172.20.10.") {
                    address = text
                    break
                }
            }
        }
        return address
    }

    /// Starts the listener and returns the URL the device should fetch.
    @discardableResult
    func start(image: Data, onServed: @escaping (UInt32) -> Void) throws -> String {
        guard let host = Self.hotspotAddress() else {
            throw ServerError.noHotspotAddress
        }
        let port = NWEndpoint.Port(rawValue: FirmwareUpdate.servingPort) ?? 8123
        let parameters = NWParameters.tcp
        parameters.allowLocalEndpointReuse = true
        // Bind to the hotspot address only: the rest of the phone's networks
        // (carrier, home WiFi) must not be able to reach a firmware server.
        parameters.requiredLocalEndpoint = .hostPort(
            host: NWEndpoint.Host(host), port: port)
        let listener: NWListener
        do {
            listener = try NWListener(using: parameters, on: port)
        } catch {
            throw ServerError.listenerFailed(error.localizedDescription)
        }
        stop()
        queue.sync {
            self.image = image
            self.onServed = onServed
        }
        listener.newConnectionHandler = { [weak self] connection in
            self?.handle(connection)
        }
        listener.stateUpdateHandler = { state in
            if case let .failed(error) = state {
                NSLog("MotoGPS firmware server failed: %@", error.localizedDescription)
            }
        }
        listener.start(queue: queue)
        self.listener = listener
        return FirmwareUpdate.servingURL(host: host)
    }

    func stop() {
        listener?.cancel()
        listener = nil
        queue.sync {
            image = Data()
            onServed = nil
        }
    }

    private func handle(_ connection: NWConnection) {
        connection.start(queue: queue)
        receiveRequest(connection, buffer: Data())
    }

    private func receiveRequest(_ connection: NWConnection, buffer: Data) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 16 * 1024) {
            [weak self] chunk, _, isComplete, error in
            guard let self else { return }
            var request = buffer
            if let chunk { request.append(chunk) }
            if error != nil {
                connection.cancel()
                return
            }
            guard let headerEnd = request.range(of: Data("\r\n\r\n".utf8)) else {
                if isComplete {
                    connection.cancel()
                } else {
                    self.receiveRequest(connection, buffer: request)
                }
                return
            }
            let head = String(
                decoding: request[request.startIndex..<headerEnd.lowerBound],
                as: UTF8.self)
            let wantsImage = head.hasPrefix("GET \(FirmwareUpdate.servingPath)")
            self.sendResponse(connection, found: wantsImage)
        }
    }

    private func sendResponse(_ connection: NWConnection, found: Bool) {
        let body = found ? image : Data("not found\n".utf8)
        let head = found
            ? "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: \(body.count)\r\nConnection: close\r\n\r\n"
            : "HTTP/1.1 404 Not Found\r\nContent-Length: \(body.count)\r\nConnection: close\r\n\r\n"
        var response = Data(head.utf8)
        response.append(body)
        connection.send(content: response, completion: .contentProcessed {
            [weak self] error in
            guard let self else { return }
            if error == nil, found {
                let served = UInt32(clamping: body.count)
                queue.async {
                    let callback = self.onServed
                    DispatchQueue.main.async { callback?(served) }
                }
            }
            connection.cancel()
        })
    }
}
