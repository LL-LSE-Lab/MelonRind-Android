#pragma once

#include <span>
#include <vector>

namespace melonrind {

/** A raw RGBA image with its origin at the top left. */
struct RgbaImage {
    int width{};
    int height{};
    std::vector<unsigned char> pixels;

    [[nodiscard]] bool empty() const noexcept { return pixels.empty(); }
};

/** Pixels per UI unit baked into every registered overlay image. */
constexpr int kOverlayPixelsPerUnit = 6;
/** Vanilla HUD icon sprites are 9 UI units square (drawn on an 8-unit pitch). */
constexpr int kOverlayIconUnits = 9;

/**
 * Builds one registered overlay image: the sub-rect `[left, right]` of a
 * 9-unit RGBA sprite, upscaled to `pixelsPerUnit`.
 *
 * The launcher's draw command has no source rectangle, so a fractional fill can
 * only be expressed by registering the crop itself.  Nearest-neighbour sampling
 * keeps the pixel-art edges on the crop boundary, which is what makes the
 * quantised fill land exactly where the plan put it.
 *
 * @param art A `kOverlayIconUnits` square RGBA sprite, 4 bytes per pixel.
 */
[[nodiscard]] RgbaImage cropOverlayArt(
    std::span<const unsigned char> art, float left, float right,
    int pixelsPerUnit = kOverlayPixelsPerUnit);

} // namespace melonrind
