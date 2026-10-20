#include "model/OverlayArt.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace melonrind {
namespace {

constexpr std::size_t kArtBytes =
    static_cast<std::size_t>(kOverlayIconUnits) * kOverlayIconUnits * 4;

} // namespace

RgbaImage cropOverlayArt(std::span<const unsigned char> art, float left,
                         float right, int pixelsPerUnit) {
    RgbaImage result;
    if (art.size() < kArtBytes || pixelsPerUnit <= 0) return result;
    if (!std::isfinite(left) || !std::isfinite(right)) return result;
    left = std::clamp(left, 0.0f, 1.0f);
    right = std::clamp(right, 0.0f, 1.0f);
    if (right <= left) return result;

    const int full = kOverlayIconUnits * pixelsPerUnit;
    result.width = std::max(1, static_cast<int>(std::lround((right - left) * full)));
    result.height = full;
    result.pixels.assign(static_cast<std::size_t>(result.width) * result.height * 4, 0);

    for (int y = 0; y < result.height; ++y) {
        const int sourceY = std::clamp(y / pixelsPerUnit, 0, kOverlayIconUnits - 1);
        for (int x = 0; x < result.width; ++x) {
            // Destination pixel centre -> sprite fraction -> source pixel.
            const float unit = left + (static_cast<float>(x) + 0.5f) /
                                          static_cast<float>(full);
            const int column = std::clamp(
                static_cast<int>(std::floor(unit * kOverlayIconUnits)), 0,
                kOverlayIconUnits - 1);
            const auto source = (static_cast<std::size_t>(sourceY) *
                                     kOverlayIconUnits +
                                 static_cast<std::size_t>(column)) *
                                4;
            auto const target =
                (static_cast<std::size_t>(y) * result.width + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                result.pixels[target + channel] = art[source + channel];
            }
        }
    }
    return result;
}

void premultiplyRgba(std::span<unsigned char> pixels) noexcept {
    const std::size_t count = pixels.size() / 4;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = index * 4;
        const unsigned int alpha = pixels[offset + 3];
        if (alpha == 255) continue;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            // Round to nearest; (255 * 255 + 127) / 255 == 255 keeps the
            // premultiplied invariant `component <= alpha` for every input.
            const unsigned int value = pixels[offset + channel];
            pixels[offset + channel] = static_cast<unsigned char>(
                (value * alpha + 127U) / 255U);
        }
    }
}

} // namespace melonrind
