#include "model/Markers.h"

#include <cmath>

namespace melonrind {

std::vector<MarkerPoint> usableMarkerPoints(std::span<const MarkerPoint> points,
                                            float coordinateLimit) {
    std::vector<MarkerPoint> result;
    if (!std::isfinite(coordinateLimit) || coordinateLimit <= 0.0f) return result;
    result.reserve(points.size());
    for (auto const &point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
        if (std::abs(point.x) >= coordinateLimit ||
            std::abs(point.y) >= coordinateLimit) {
            continue;
        }
        result.push_back(point);
    }
    return result;
}

} // namespace melonrind
