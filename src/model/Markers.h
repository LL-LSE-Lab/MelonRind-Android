#pragma once

#include <span>
#include <vector>

namespace melonrind {

/** A point expressed in the coordinate space a HUD renderer reported. */
struct MarkerPoint {
    float x{};
    float y{};
};

/**
 * Drops the origins a calibration marker must not be drawn at.
 *
 * The launcher overlay resolves large negative coordinates as anchor sentinels
 * (`x <= -19000` and `x <= -9000` mean "centred" and "right/bottom aligned"), so
 * a raw value in that range would be relocated instead of marking the icon it
 * belongs to.  Non-finite values and anything beyond `coordinateLimit` are
 * dropped for the same reason: they cannot be a HUD icon origin, and a marker
 * derived from them would only obscure the measurement.
 */
[[nodiscard]] std::vector<MarkerPoint> usableMarkerPoints(
    std::span<const MarkerPoint> points, float coordinateLimit);

} // namespace melonrind
