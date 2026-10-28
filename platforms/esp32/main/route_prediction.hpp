#pragma once

#include <cmath>
#include <cstddef>

#include "port_api/nav_types.hpp"

namespace moto::device {

// Where the rider is between two phone fixes. The phone sends a position
// about once a second; holding the map on it until the next one showed the
// rider up to a second behind (17 m at 60 km/h), so the device advances the
// last fix by speed x time, along the route when the rider is on it.

// Metres on a plane tangent at a reference point: good to centimetres over
// the few hundred metres a route window spans.
struct LocalMetres {
  float east = 0.0F;
  float north = 0.0F;
};

inline constexpr double kPredictionMetresPerDegree =
    6'371'000.0 * 3.14159265358979323846 / 180.0;

inline LocalMetres to_local(const moto::nav::Gcj02Point& point,
                            const moto::nav::Gcj02Point& reference) {
  const double cos_latitude =
      std::cos(reference.latitude_deg * 3.14159265358979323846 / 180.0);
  return {
      static_cast<float>((point.longitude_deg - reference.longitude_deg) *
                         kPredictionMetresPerDegree * cos_latitude),
      static_cast<float>((point.latitude_deg - reference.latitude_deg) *
                         kPredictionMetresPerDegree),
  };
}

inline moto::nav::Gcj02Point from_local(const LocalMetres& offset,
                                        const moto::nav::Gcj02Point& reference) {
  const double cos_latitude =
      std::cos(reference.latitude_deg * 3.14159265358979323846 / 180.0);
  return {
      reference.latitude_deg + offset.north / kPredictionMetresPerDegree,
      reference.longitude_deg +
          offset.east / (kPredictionMetresPerDegree * cos_latitude),
  };
}

inline float metres_between(const moto::nav::Gcj02Point& a,
                            const moto::nav::Gcj02Point& b) {
  const LocalMetres offset = to_local(a, b);
  return std::hypot(offset.east, offset.north);
}

// `origin` moved on by `distance_m`. When the origin lies within `on_route_m`
// of the route window, the move follows the route from the origin's
// projection on it, and the rider keeps their offset from the line: a
// prediction must not snap the map sideways onto the route. Past the
// window's last point the route continues straight on. Anywhere else, or
// with on_route_m <= 0, the move is straight along `heading_deg`.
inline moto::nav::Gcj02Point advance_position(
    const moto::nav::Gcj02Point& origin, float distance_m, float heading_deg,
    const moto::nav::Gcj02Point* route, std::size_t route_count,
    float on_route_m) {
  if (!(distance_m > 0.0F)) return origin;
  if (route != nullptr && route_count >= 2 && on_route_m > 0.0F) {
    std::size_t best = route_count;
    float best_distance2 = on_route_m * on_route_m;
    LocalMetres best_point{};
    LocalMetres from = to_local(route[0], origin);
    for (std::size_t i = 0; i + 1 < route_count; ++i) {
      const LocalMetres to = to_local(route[i + 1], origin);
      const float dx = to.east - from.east;
      const float dy = to.north - from.north;
      const float length2 = dx * dx + dy * dy;
      if (length2 > 1e-6F) {
        // The origin is (0, 0) in these coordinates.
        float t = -(from.east * dx + from.north * dy) / length2;
        t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
        const LocalMetres on = {from.east + dx * t, from.north + dy * t};
        const float distance2 = on.east * on.east + on.north * on.north;
        if (distance2 <= best_distance2) {
          best = i;
          best_distance2 = distance2;
          best_point = on;
        }
      }
      from = to;
    }
    if (best < route_count) {
      float remaining = distance_m;
      LocalMetres at = best_point;
      LocalMetres direction{};
      for (std::size_t i = best; i + 1 < route_count; ++i) {
        const LocalMetres to = to_local(route[i + 1], origin);
        const float dx = to.east - at.east;
        const float dy = to.north - at.north;
        const float length = std::hypot(dx, dy);
        if (length <= 1e-3F) continue;
        direction = {dx / length, dy / length};
        if (length >= remaining) {
          at = {at.east + direction.east * remaining,
                at.north + direction.north * remaining};
          remaining = 0.0F;
          break;
        }
        remaining -= length;
        at = to;
      }
      at = {at.east + direction.east * remaining,
            at.north + direction.north * remaining};
      return from_local({at.east - best_point.east, at.north - best_point.north},
                        origin);
    }
  }
  const float heading = heading_deg * 3.14159265F / 180.0F;
  return from_local({distance_m * std::sin(heading),
                     distance_m * std::cos(heading)},
                    origin);
}

}  // namespace moto::device
