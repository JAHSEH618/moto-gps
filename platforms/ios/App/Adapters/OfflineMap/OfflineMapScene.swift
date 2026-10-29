import Foundation

struct OfflineMapPointE6: Codable, Equatable, Hashable, Sendable {
    let latitudeE6: Int32
    let longitudeE6: Int32

    init(latitudeE6: Int32, longitudeE6: Int32) {
        self.latitudeE6 = latitudeE6
        self.longitudeE6 = longitudeE6
    }

    init(from decoder: Decoder) throws {
        var values = try decoder.unkeyedContainer()
        latitudeE6 = try values.decode(Int32.self)
        longitudeE6 = try values.decode(Int32.self)
        guard values.isAtEnd else {
            throw DecodingError.dataCorruptedError(
                in: values,
                debugDescription: "MapScene point must contain exactly latitudeE6 and longitudeE6"
            )
        }
    }

    func encode(to encoder: Encoder) throws {
        var values = encoder.unkeyedContainer()
        try values.encode(latitudeE6)
        try values.encode(longitudeE6)
    }
}

struct OfflineMapRoad: Codable, Equatable, Sendable {
    let osmWayID: Int64?
    let roadClass: String
    let points: [OfflineMapPointE6]

    private enum CodingKeys: String, CodingKey {
        case osmWayID = "osm_way_id"
        case roadClass = "class"
        case points = "points_e6"
    }
}

struct OfflineMapBuilding: Codable, Equatable, Sendable {
    let osmWayID: Int64?
    let name: String?
    let buildingClass: String
    let points: [OfflineMapPointE6]

    private enum CodingKeys: String, CodingKey {
        case osmWayID = "osm_way_id"
        case name
        case buildingClass = "class"
        case points = "points_e6"
    }
}

/// Water, green space, pedestrian paving or a surface car park: the ground
/// the streets are cut through. The outer ring only, implicitly closed.
struct OfflineMapArea: Codable, Equatable, Sendable {
    static let classes: Set<String> = ["water", "green", "pedestrian", "parking"]

    let areaClass: String
    let points: [OfflineMapPointE6]

    private enum CodingKeys: String, CodingKey {
        case areaClass = "class"
        case points = "points_e6"
    }
}

/// A car park the basemap only has as a point (its pois layer); drawn as a P.
/// A lot with an outline is an `OfflineMapArea`, a garage a building.
struct OfflineMapPoi: Codable, Equatable, Hashable, Sendable {
    static let classes: Set<String> = ["parking"]

    let poiClass: String
    let point: OfflineMapPointE6

    private enum CodingKeys: String, CodingKey {
        case poiClass = "class"
        case point = "point_e6"
    }
}

struct OfflineMapSource: Codable, Equatable, Sendable {
    let provider: String
    let licence: String
    let attributionURL: String
    let retrievedAt: String
    let bboxWGS84: [Double]?

    private enum CodingKeys: String, CodingKey {
        case provider
        case licence
        case attributionURL = "attribution_url"
        case retrievedAt = "retrieved_at"
        case bboxWGS84 = "bbox_wgs84"
    }
}

/// Mirrors `kMaxMapScene*` in `ble_protocol.hpp` and the shared nav_core /
/// nav_ui capacities. The circle is a local window, not a city database: this
/// is what the 466 px device can draw without every frame becoming a
/// full-screen repaint.
enum OfflineMapSceneLimits {
    static let roads = 40
    static let roadPoints = 255
    static let buildings = 24
    static let buildingPoints = 192
    static let areas = 12
    static let areaPoints = 128
    static let pois = 16
}

struct OfflineMapSceneDocument: Codable, Equatable, Sendable {
    let schemaVersion: Int
    let coordinateSystem: String
    let sceneRevision: UInt32
    let viewOrigin: OfflineMapPointE6
    let radiusM: UInt16
    let roads: [OfflineMapRoad]
    let buildings: [OfflineMapBuilding]
    /// Absent from documents written before areas existed.
    var areas: [OfflineMapArea] = []
    /// Absent from documents written before car-park points existed.
    var pois: [OfflineMapPoi] = []
    let source: OfflineMapSource

    fileprivate enum CodingKeys: String, CodingKey {
        case schemaVersion = "schema_version"
        case coordinateSystem = "coordinate_system"
        case sceneRevision = "scene_revision"
        case viewOrigin = "view_origin_e6"
        case radiusM = "radius_m"
        case roads
        case buildings
        case areas
        case pois
        case source
    }

    static func decode(_ data: Data) throws -> Self {
        let value = try JSONDecoder().decode(Self.self, from: data)
        guard value.schemaVersion == 1, value.coordinateSystem == "GCJ-02" else {
            throw OfflineMapSceneError.unsupportedFormat
        }
        guard value.roads.count <= OfflineMapSceneLimits.roads,
              value.roads.reduce(0, { $0 + $1.points.count }) <= OfflineMapSceneLimits.roadPoints,
              value.buildings.count <= OfflineMapSceneLimits.buildings,
              value.buildings.reduce(0, { $0 + $1.points.count }) <= OfflineMapSceneLimits.buildingPoints,
              value.roads.allSatisfy({ $0.points.count >= 2 }),
              value.buildings.allSatisfy({
                  $0.points.count >= 3 && $0.points.first != $0.points.last
              })
        else { throw OfflineMapSceneError.capacityExceeded }
        return value
    }
}

extension OfflineMapSceneDocument {
    // In an extension so the memberwise initializer survives.
    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        schemaVersion = try values.decode(Int.self, forKey: .schemaVersion)
        coordinateSystem = try values.decode(String.self, forKey: .coordinateSystem)
        sceneRevision = try values.decode(UInt32.self, forKey: .sceneRevision)
        viewOrigin = try values.decode(OfflineMapPointE6.self, forKey: .viewOrigin)
        radiusM = try values.decode(UInt16.self, forKey: .radiusM)
        roads = try values.decode([OfflineMapRoad].self, forKey: .roads)
        buildings = try values.decode([OfflineMapBuilding].self, forKey: .buildings)
        areas = try values.decodeIfPresent([OfflineMapArea].self, forKey: .areas) ?? []
        pois = try values.decodeIfPresent([OfflineMapPoi].self, forKey: .pois) ?? []
        source = try values.decode(OfflineMapSource.self, forKey: .source)
    }
}

enum OfflineMapSceneError: Error, Equatable {
    case resourceMissing
    case unsupportedFormat
    case capacityExceeded
}

struct OfflineMapSceneWindow: Equatable, Sendable {
    let revision: UInt32
    let origin: OfflineMapPointE6
    let radiusM: UInt16
    let roads: [OfflineMapRoad]
    let buildings: [OfflineMapBuilding]
    /// Already clipped to the window and simplified, within the wire budget.
    var areas: [OfflineMapArea] = []
    /// The nearest car parks inside the window's square, within the budget.
    var pois: [OfflineMapPoi] = []
}

protocol OfflineMapSceneQuerying: Sendable {
    func query(
        around origin: OfflineMapPointE6,
        radiusM: UInt16,
        revision: UInt32
    ) -> OfflineMapSceneWindow
}

/// A replaceable spatial-index implementation for the bundled EVT fixture.
/// It deliberately keeps no SQLite dependency in the app target.  The full
/// Jinan pack can replace this behind OfflineMapSceneQuerying with SQLite
/// R-tree without changing BLE or AppModel code.
struct InMemoryOfflineMapSceneIndex: OfflineMapSceneQuerying {
    private struct Bounds: Sendable {
        let minimumLatitudeE6: Int32
        let maximumLatitudeE6: Int32
        let minimumLongitudeE6: Int32
        let maximumLongitudeE6: Int32

        init(points: [OfflineMapPointE6]) {
            minimumLatitudeE6 = points.map(\.latitudeE6).min() ?? 0
            maximumLatitudeE6 = points.map(\.latitudeE6).max() ?? 0
            minimumLongitudeE6 = points.map(\.longitudeE6).min() ?? 0
            maximumLongitudeE6 = points.map(\.longitudeE6).max() ?? 0
        }

        func intersects(origin: OfflineMapPointE6, radiusM: Double) -> Bool {
            let metresPerLatitudeE6 = 111_195.0 / 1_000_000
            let latitudeRadians = Double(origin.latitudeE6) / 1_000_000 * .pi / 180
            let metresPerLongitudeE6 = metresPerLatitudeE6 * cos(latitudeRadians)
            let nearestLatitude = min(max(origin.latitudeE6, minimumLatitudeE6), maximumLatitudeE6)
            let nearestLongitude = min(max(origin.longitudeE6, minimumLongitudeE6), maximumLongitudeE6)
            let northM = Double(nearestLatitude - origin.latitudeE6) * metresPerLatitudeE6
            let eastM = Double(nearestLongitude - origin.longitudeE6) * metresPerLongitudeE6
            return hypot(northM, eastM) <= radiusM
        }
    }

    private struct IndexedRoad: Sendable {
        let value: OfflineMapRoad
        let bounds: Bounds
    }

    private struct IndexedBuilding: Sendable {
        let value: OfflineMapBuilding
        let bounds: Bounds
    }

    private struct IndexedArea: Sendable {
        let value: OfflineMapArea
        let bounds: Bounds
    }

    private let roads: [IndexedRoad]
    private let buildings: [IndexedBuilding]
    private let areas: [IndexedArea]
    private let pois: [OfflineMapPoi]

    init(document: OfflineMapSceneDocument) {
        roads = document.roads.map { IndexedRoad(value: $0, bounds: Bounds(points: $0.points)) }
        buildings = document.buildings.map {
            IndexedBuilding(value: $0, bounds: Bounds(points: $0.points))
        }
        areas = document.areas.map { IndexedArea(value: $0, bounds: Bounds(points: $0.points)) }
        pois = document.pois
    }

    func query(
        around origin: OfflineMapPointE6,
        radiusM: UInt16,
        revision: UInt32
    ) -> OfflineMapSceneWindow {
        let radius = Double(max(500, min(800, radiusM)))
        let roadCandidates = roads
            .filter { $0.bounds.intersects(origin: origin, radiusM: radius) }
            .flatMap { indexed in
                Self.clippedRuns(indexed.value, origin: origin, radiusM: radius).map { run in
                    let simplified = OfflineMapRoad(
                        osmWayID: run.osmWayID,
                        roadClass: run.roadClass,
                        points: Self.simplified(run.points, around: origin)
                    )
                    return (
                        road: simplified,
                        importance: Self.roadImportance(run.roadClass),
                        distanceM: Self.minimumDistanceM(run.points, from: origin)
                    )
                }
            }
            .sorted {
                if $0.distanceM != $1.distanceM { return $0.distanceM < $1.distanceM }
                if $0.importance != $1.importance { return $0.importance < $1.importance }
                return ($0.road.osmWayID ?? .max) < ($1.road.osmWayID ?? .max)
            }

        // Two passes over one nearest-first list. Ranking by class first spent
        // the whole budget on main roads up to 500 m out, so the round screen
        // showed a few arterials and none of the streets around the rider.
        // Arterials that can reach the glass still get a reserved share, then
        // the nearest streets of any class fill the rest.
        var selectedRoads: [OfflineMapRoad] = []
        var selectedIndices = Set<Int>()
        var roadPointCount = 0
        func take(_ index: Int) {
            let road = roadCandidates[index].road
            guard selectedRoads.count < OfflineMapSceneLimits.roads,
                  roadPointCount + road.points.count <= OfflineMapSceneLimits.roadPoints
            else { return }
            selectedRoads.append(road)
            selectedIndices.insert(index)
            roadPointCount += road.points.count
        }
        var arterials = 0
        for (index, candidate) in roadCandidates.enumerated()
        where candidate.importance <= Self.arterialImportance &&
            candidate.distanceM <= Self.visibleRadiusM &&
            arterials < Self.arterialBudget {
            let before = selectedRoads.count
            take(index)
            if selectedRoads.count > before { arterials += 1 }
        }
        for index in roadCandidates.indices where !selectedIndices.contains(index) {
            guard selectedRoads.count < OfflineMapSceneLimits.roads else { break }
            take(index)
        }

        let buildingCandidates = buildings
            .filter { $0.bounds.intersects(origin: origin, radiusM: radius) }
            .filter {
                Self.polygonIntersects($0.value.points, origin: origin, radiusM: radius)
            }
            .map { indexed in
                let distance = Self.minimumDistanceM(indexed.value.points, from: origin)
                let area = Self.polygonAreaM2(indexed.value.points, around: origin)
                // Larger footprints remain visible a little farther away, while
                // nearby buildings still dominate the tiny round viewport.
                let visualScore = distance - min(150, sqrt(area) * 0.75)
                return (building: indexed.value, visualScore: visualScore, areaM2: area)
            }
            .sorted {
                if $0.visualScore != $1.visualScore { return $0.visualScore < $1.visualScore }
                if $0.areaM2 != $1.areaM2 { return $0.areaM2 > $1.areaM2 }
                return ($0.building.osmWayID ?? .max) < ($1.building.osmWayID ?? .max)
            }

        var selectedBuildings: [OfflineMapBuilding] = []
        var buildingPointCount = 0
        for candidate in buildingCandidates {
            guard selectedBuildings.count < OfflineMapSceneLimits.buildings else { break }
            guard buildingPointCount + candidate.building.points.count <= OfflineMapSceneLimits.buildingPoints
            else { continue }
            selectedBuildings.append(candidate.building)
            buildingPointCount += candidate.building.points.count
        }

        return OfflineMapSceneWindow(
            revision: revision,
            origin: origin,
            radiusM: UInt16(radius),
            roads: selectedRoads,
            buildings: selectedBuildings,
            areas: selectedAreas(around: origin, radiusM: radius),
            pois: selectedPois(around: origin, radiusM: radius)
        )
    }

    /// The nearest car parks inside the window's square (the same clip as the
    /// areas), at most the wire budget.
    private func selectedPois(
        around origin: OfflineMapPointE6,
        radiusM: Double
    ) -> [OfflineMapPoi] {
        let metresPerLatitudeE6 = 111_195.0 / 1_000_000
        let metresPerLongitudeE6 =
            metresPerLatitudeE6 * cos(Double(origin.latitudeE6) / 1_000_000 * .pi / 180)
        return pois
            .compactMap { poi -> (poi: OfflineMapPoi, distanceM: Double)? in
                let northM = Double(poi.point.latitudeE6 - origin.latitudeE6) * metresPerLatitudeE6
                let eastM = Double(poi.point.longitudeE6 - origin.longitudeE6) * metresPerLongitudeE6
                guard abs(northM) <= radiusM, abs(eastM) <= radiusM else { return nil }
                return (poi, hypot(northM, eastM))
            }
            .sorted { $0.distanceM < $1.distanceM }
            .prefix(OfflineMapSceneLimits.pois)
            .map(\.poi)
    }

    /// A park or a lake is usually far larger than the window, and its OSM
    /// outline far longer than the whole area budget. Each ring is clipped to
    /// the window's square, then simplified, coarser until it fits; the
    /// largest areas go first because they change the most of the screen.
    private func selectedAreas(
        around origin: OfflineMapPointE6,
        radiusM: Double
    ) -> [OfflineMapArea] {
        let candidates = areas
            .filter { $0.bounds.intersects(origin: origin, radiusM: radiusM * 2.squareRoot()) }
            .compactMap { indexed -> (area: OfflineMapArea, areaM2: Double)? in
                let clipped = Self.clippedRing(indexed.value.points, around: origin, halfSideM: radiusM)
                guard clipped.count >= 3 else { return nil }
                let areaM2 = Self.polygonAreaM2(clipped, around: origin)
                guard areaM2 >= Self.minimumAreaM2 else { return nil }
                return (OfflineMapArea(areaClass: indexed.value.areaClass, points: clipped), areaM2)
            }
            .sorted { $0.areaM2 > $1.areaM2 }

        var selected: [OfflineMapArea] = []
        var pointCount = 0
        for candidate in candidates {
            guard selected.count < OfflineMapSceneLimits.areas else { break }
            let budget = min(Self.areaPointCap, OfflineMapSceneLimits.areaPoints - pointCount)
            guard budget >= 3 else { break }
            var ring: [OfflineMapPointE6] = []
            for tolerance in Self.areaToleranceStepsM {
                ring = Self.simplifiedRing(candidate.area.points, around: origin, toleranceM: tolerance)
                if ring.count <= budget { break }
            }
            guard ring.count >= 3, ring.count <= budget else { continue }
            selected.append(OfflineMapArea(areaClass: candidate.area.areaClass, points: ring))
            pointCount += ring.count
        }
        return selected
    }

    private static func clippedRuns(
        _ road: OfflineMapRoad,
        origin: OfflineMapPointE6,
        radiusM: Double
    ) -> [OfflineMapRoad] {
        guard road.points.count >= 2 else { return [] }
        var result: [OfflineMapRoad] = []
        var run: [OfflineMapPointE6] = []
        for index in 1 ..< road.points.count {
            let start = road.points[index - 1]
            let end = road.points[index]
            if segmentDistanceM(origin, start, end) <= radiusM {
                if run.isEmpty { run.append(start) }
                if run.last != end { run.append(end) }
            } else if run.count >= 2 {
                result.append(OfflineMapRoad(
                    osmWayID: road.osmWayID,
                    roadClass: road.roadClass,
                    points: run
                ))
                run.removeAll(keepingCapacity: true)
            } else {
                run.removeAll(keepingCapacity: true)
            }
        }
        if run.count >= 2 {
            result.append(OfflineMapRoad(
                osmWayID: road.osmWayID,
                roadClass: road.roadClass,
                points: run
            ))
        }
        return result
    }

    private static func polygonIntersects(
        _ points: [OfflineMapPointE6],
        origin: OfflineMapPointE6,
        radiusM: Double
    ) -> Bool {
        guard points.count >= 3 else { return false }
        if points.contains(where: { distanceM(origin, $0) <= radiusM }) { return true }
        for index in points.indices {
            if segmentDistanceM(origin, points[index], points[(index + 1) % points.count]) <= radiusM {
                return true
            }
        }
        return pointInPolygon(origin, points)
    }

    /// 400 m² is about 11 × 11 px on the round screen: smaller is noise.
    private static let minimumAreaM2 = 400.0
    /// No single area may take more than this share of the 128-point budget.
    private static let areaPointCap = 48
    /// An area edge is a fill boundary, not a street the rider follows: 3 m
    /// (under 2 px) to start, coarser only for a ring that would not fit.
    private static let areaToleranceStepsM: [Double] = [3, 6, 12, 24, 48]

    /// Sutherland-Hodgman against the square of half-side `halfSideM` around
    /// the origin, in local metres. The result may run along the square's
    /// edges where the area continues off screen, which is what fills it to
    /// the edge of the window.
    private static func clippedRing(
        _ points: [OfflineMapPointE6],
        around origin: OfflineMapPointE6,
        halfSideM: Double
    ) -> [OfflineMapPointE6] {
        var ring = points.map { localMetres($0, around: origin) }
        let planes: [(inside: ((Double, Double)) -> Bool, cut: ((Double, Double), (Double, Double)) -> (Double, Double))] = [
            ({ $0.0 >= -halfSideM }, { a, b in edgePoint(a, b, x: -halfSideM) }),
            ({ $0.0 <= halfSideM }, { a, b in edgePoint(a, b, x: halfSideM) }),
            ({ $0.1 >= -halfSideM }, { a, b in edgePoint(a, b, y: -halfSideM) }),
            ({ $0.1 <= halfSideM }, { a, b in edgePoint(a, b, y: halfSideM) }),
        ]
        for plane in planes {
            guard let last = ring.last else { break }
            var output: [(Double, Double)] = []
            var previous = last
            for current in ring {
                let currentInside = plane.inside(current)
                if currentInside != plane.inside(previous) {
                    output.append(plane.cut(previous, current))
                }
                if currentInside { output.append(current) }
                previous = current
            }
            ring = output
        }
        var result: [OfflineMapPointE6] = []
        for point in ring.map({ e6(fromLocal: $0, around: origin) }) where result.last != point {
            result.append(point)
        }
        while result.count > 1, result.first == result.last { result.removeLast() }
        return result
    }

    private static func edgePoint(
        _ a: (Double, Double),
        _ b: (Double, Double),
        x: Double? = nil,
        y: Double? = nil
    ) -> (Double, Double) {
        if let x {
            let t = (x - a.0) / (b.0 - a.0)
            return (x, a.1 + t * (b.1 - a.1))
        }
        let y = y ?? 0
        let t = (y - a.1) / (b.1 - a.1)
        return (a.0 + t * (b.0 - a.0), y)
    }

    private static func e6(
        fromLocal point: (Double, Double),
        around origin: OfflineMapPointE6
    ) -> OfflineMapPointE6 {
        let metresPerLatitudeE6 = 111_195.0 / 1_000_000
        let latitudeRadians = Double(origin.latitudeE6) / 1_000_000 * .pi / 180
        let metresPerLongitudeE6 = metresPerLatitudeE6 * cos(latitudeRadians)
        return OfflineMapPointE6(
            latitudeE6: origin.latitudeE6 &+ Int32((point.1 / metresPerLatitudeE6).rounded()),
            longitudeE6: origin.longitudeE6 &+ Int32((point.0 / metresPerLongitudeE6).rounded())
        )
    }

    /// Douglas-Peucker over the closed ring: the first point is repeated at
    /// the end so the closing edge is simplified too, then dropped again.
    private static func simplifiedRing(
        _ points: [OfflineMapPointE6],
        around origin: OfflineMapPointE6,
        toleranceM: Double
    ) -> [OfflineMapPointE6] {
        guard points.count > 3, let first = points.first else { return points }
        var ring = simplified(points + [first], around: origin, toleranceM: toleranceM)
        ring.removeLast()
        return ring
    }

    /// What the round screen can show before the next scene arrives: about
    /// 360 m around the rider (0.57 px/m inside the 205 px ring) plus the
    /// 100 m the rider may travel before the phone refreshes.
    private static let visibleRadiusM = 460.0
    /// primary, secondary and motorway.
    private static let arterialImportance = 2
    private static let arterialBudget = 8
    /// 1.5 m is under one pixel on the round screen (0.57 px/m), so a street
    /// never visibly leaves the route or the rider it runs under; straight
    /// runs still drop their redundant vertices. The pack itself is already
    /// simplified at 1.25 m when it is built.
    private static let simplifyToleranceM = 1.5

    /// Douglas-Peucker in local metres. Endpoints always stay, so clipped
    /// runs still meet the edge of the window where they did.
    private static func simplified(
        _ points: [OfflineMapPointE6],
        around origin: OfflineMapPointE6,
        toleranceM: Double = simplifyToleranceM
    ) -> [OfflineMapPointE6] {
        guard points.count > 2 else { return points }
        let local = points.map { localMetres($0, around: origin) }
        var keep = [Bool](repeating: false, count: points.count)
        keep[0] = true
        keep[points.count - 1] = true
        var stack = [(0, points.count - 1)]
        while let (first, last) = stack.popLast() {
            guard last > first + 1 else { continue }
            let (ax, ay) = local[first]
            let (bx, by) = local[last]
            let dx = bx - ax
            let dy = by - ay
            let length = hypot(dx, dy)
            var farthest = first
            var farthestDistance = 0.0
            for index in (first + 1) ..< last {
                let (px, py) = local[index]
                let distance = length == 0
                    ? hypot(px - ax, py - ay)
                    : abs(dy * (px - ax) - dx * (py - ay)) / length
                if distance > farthestDistance {
                    farthestDistance = distance
                    farthest = index
                }
            }
            if farthestDistance > toleranceM {
                keep[farthest] = true
                stack.append((first, farthest))
                stack.append((farthest, last))
            }
        }
        return points.indices.filter { keep[$0] }.map { points[$0] }
    }

    private static func roadImportance(_ roadClass: String) -> Int {
        switch roadClass {
        case "primary": 0
        case "secondary": 1
        case "motorway": 2
        case "residential": 3
        case "service": 4
        default: 5
        }
    }

    private static func minimumDistanceM(
        _ points: [OfflineMapPointE6],
        from origin: OfflineMapPointE6
    ) -> Double {
        guard !points.isEmpty else { return .infinity }
        if points.count == 1 { return distanceM(origin, points[0]) }
        return (1 ..< points.count).reduce(.infinity) { minimum, index in
            min(minimum, segmentDistanceM(origin, points[index - 1], points[index]))
        }
    }

    private static func polygonAreaM2(
        _ points: [OfflineMapPointE6],
        around origin: OfflineMapPointE6
    ) -> Double {
        guard points.count >= 3 else { return 0 }
        var twiceArea = 0.0
        for index in points.indices {
            let (x1, y1) = localMetres(points[index], around: origin)
            let (x2, y2) = localMetres(points[(index + 1) % points.count], around: origin)
            twiceArea += x1 * y2 - x2 * y1
        }
        return abs(twiceArea) / 2
    }

    private static func pointInPolygon(
        _ point: OfflineMapPointE6,
        _ polygon: [OfflineMapPointE6]
    ) -> Bool {
        var inside = false
        var previous = polygon.last!
        for current in polygon {
            let crosses = (current.latitudeE6 > point.latitudeE6) !=
                (previous.latitudeE6 > point.latitudeE6)
            if crosses {
                let longitude = Double(previous.longitudeE6 - current.longitudeE6) *
                    Double(point.latitudeE6 - current.latitudeE6) /
                    Double(previous.latitudeE6 - current.latitudeE6) +
                    Double(current.longitudeE6)
                if Double(point.longitudeE6) < longitude { inside.toggle() }
            }
            previous = current
        }
        return inside
    }

    private static func segmentDistanceM(
        _ point: OfflineMapPointE6,
        _ start: OfflineMapPointE6,
        _ end: OfflineMapPointE6
    ) -> Double {
        let (px, py) = localMetres(point, around: point)
        let (ax, ay) = localMetres(start, around: point)
        let (bx, by) = localMetres(end, around: point)
        let dx = bx - ax
        let dy = by - ay
        let denominator = dx * dx + dy * dy
        let ratio = denominator == 0 ? 0 :
            max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / denominator))
        return hypot(px - (ax + ratio * dx), py - (ay + ratio * dy))
    }

    private static func distanceM(
        _ lhs: OfflineMapPointE6,
        _ rhs: OfflineMapPointE6
    ) -> Double {
        let (x, y) = localMetres(lhs, around: rhs)
        return hypot(x, y)
    }

    private static func localMetres(
        _ point: OfflineMapPointE6,
        around origin: OfflineMapPointE6
    ) -> (Double, Double) {
        let metresPerLatitudeE6 = 111_195.0 / 1_000_000
        let latitudeRadians = Double(origin.latitudeE6) / 1_000_000 * .pi / 180
        let metresPerLongitudeE6 = metresPerLatitudeE6 * cos(latitudeRadians)
        return (
            Double(point.longitudeE6 - origin.longitudeE6) * metresPerLongitudeE6,
            Double(point.latitudeE6 - origin.latitudeE6) * metresPerLatitudeE6
        )
    }
}

@MainActor
final class OfflineMapSceneCoordinator {
    private let index: any OfflineMapSceneQuerying
    private let radiusM: UInt16
    private let refreshDistanceM: Double
    private var lastOrigin: OfflineMapPointE6?
    private var revision: UInt32 = 0

    init(index: any OfflineMapSceneQuerying, radiusM: UInt16 = 500, refreshDistanceM: Double = 100) {
        self.index = index
        self.radiusM = max(500, min(800, radiusM))
        self.refreshDistanceM = max(25, refreshDistanceM)
    }

    convenience init(bundle: Bundle = .main) throws {
        let databaseName = "jinan-v1"
        var databaseURLs: [URL] = []
        if let supportRoot = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first {
            let downloadedURL = supportRoot
                .appendingPathComponent("OfflineMaps", isDirectory: true)
                .appendingPathComponent("\(databaseName).sqlite")
            databaseURLs.append(downloadedURL)
        }
        if let bundledDatabase = bundle.url(forResource: databaseName, withExtension: "sqlite") {
            databaseURLs.append(bundledDatabase)
        }
        self.init(index: try Self.loadIndex(databaseURLs: databaseURLs, sampleURL: bundle.url(
            forResource: "jinan_map_scene_sample",
            withExtension: "json"
        )))
    }

    static func loadIndex(databaseURLs: [URL], sampleURL: URL?) throws -> any OfflineMapSceneQuerying {
        for url in databaseURLs where FileManager.default.fileExists(atPath: url.path) {
            do {
                let index = try SQLiteOfflineMapSceneIndex(url: url)
                #if DEBUG
                print("[MotoMap] loaded \(url.lastPathComponent)")
                #endif
                return index
            } catch {
                // An incomplete downloaded pack must not disable a later
                // intact pack in the search order.
                #if DEBUG
                print("[MotoMap] invalid map \(url.lastPathComponent): \(error)")
                #endif
            }
        }
        guard let url = sampleURL else { throw OfflineMapSceneError.resourceMissing }
        let document = try OfflineMapSceneDocument.decode(Data(contentsOf: url))
        return InMemoryOfflineMapSceneIndex(document: document)
    }

    func sceneIfNeeded(latitudeDeg: Double, longitudeDeg: Double) -> OfflineMapSceneWindow? {
        guard latitudeDeg.isFinite, longitudeDeg.isFinite else { return nil }
        let origin = OfflineMapPointE6(
            latitudeE6: Int32(clamping: Int64((latitudeDeg * 1_000_000).rounded())),
            longitudeE6: Int32(clamping: Int64((longitudeDeg * 1_000_000).rounded()))
        )
        if let lastOrigin,
           Self.distanceM(lastOrigin, origin) < refreshDistanceM {
            return nil
        }
        revision &+= 1
        if revision == 0 { revision = 1 }
        lastOrigin = origin
        return index.query(around: origin, radiusM: radiusM, revision: revision)
    }

    func reset() {
        lastOrigin = nil
    }

    private static func distanceM(_ lhs: OfflineMapPointE6, _ rhs: OfflineMapPointE6) -> Double {
        let latitudeRadians = Double(rhs.latitudeE6) / 1_000_000 * .pi / 180
        let northM = Double(lhs.latitudeE6 - rhs.latitudeE6) * 111_195 / 1_000_000
        let eastM = Double(lhs.longitudeE6 - rhs.longitudeE6) *
            111_195 * cos(latitudeRadians) / 1_000_000
        return hypot(eastM, northM)
    }
}

extension OfflineMapSceneWindow {
    /// `includeAreas` is the device's CapabilityMapAreas and `includePois` its
    /// CapabilityMapPois: an older firmware refuses a MapScene that carries
    /// either section, so it is sent the scene without.
    func makeBLEInput(includeAreas: Bool = true, includePois: Bool = true) -> MotoBLEMapSceneInput {
        let input = MotoBLEMapSceneInput()
        input.sceneRevision = revision
        input.originLatitudeE6 = origin.latitudeE6
        input.originLongitudeE6 = origin.longitudeE6
        input.radiusM = radiusM
        input.roads = roads.map { road in
            let value = MotoBLEMapRoadInput()
            value.className = road.roadClass
            value.points = road.points.map { point in
                let value = MotoBLEMapPointInput()
                value.latitudeE6 = point.latitudeE6
                value.longitudeE6 = point.longitudeE6
                return value
            }
            return value
        }
        input.buildings = buildings.map { building in
            let value = MotoBLEMapBuildingInput()
            value.className = building.buildingClass
            value.points = building.points.map { point in
                let value = MotoBLEMapPointInput()
                value.latitudeE6 = point.latitudeE6
                value.longitudeE6 = point.longitudeE6
                return value
            }
            return value
        }
        input.areas = includeAreas ? areas.map { area in
            let value = MotoBLEMapAreaInput()
            value.className = area.areaClass
            value.points = area.points.map { point in
                let value = MotoBLEMapPointInput()
                value.latitudeE6 = point.latitudeE6
                value.longitudeE6 = point.longitudeE6
                return value
            }
            return value
        } : []
        input.pois = includePois ? pois.map { poi in
            let value = MotoBLEMapPoiInput()
            value.className = poi.poiClass
            value.latitudeE6 = poi.point.latitudeE6
            value.longitudeE6 = poi.point.longitudeE6
            return value
        } : []
        return input
    }
}
