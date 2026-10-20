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
 * The returned pixels keep the sprite's straight-alpha colours; call
 * `premultiplyRgba` before handing them to the launcher.
 *
 * @param art A `kOverlayIconUnits` square RGBA sprite, 4 bytes per pixel.
 */
[[nodiscard]] RgbaImage cropOverlayArt(
    std::span<const unsigned char> art, float left, float right,
    int pixelsPerUnit = kOverlayPixelsPerUnit);

/**
 * Converts straight-alpha RGBA pixels in place to premultiplied alpha.
 *
 * The launcher registers an image by calling `Bitmap.copyPixelsFromBuffer` on
 * an `ARGB_8888` bitmap, which Android always treats as *premultiplied*.  The
 * copy itself does not premultiply, so straight-alpha art that stores a colour
 * in a fully transparent pixel is invalid premultiplied data: Skia then blends
 * it with `dst + src * (1 - a)`, i.e. the colour is *added* to the screen even
 * though the pixel is invisible.  On a device whose renderer keeps that value
 * the effect is a solid bright rectangle instead of an outline (and a white
 * seam wherever two registered crops meet).  Premultiplying before
 * registration makes the image correct under both interpretations.
 *
 * Pixels whose alpha is zero become fully transparent black; every component
 * is rounded to the nearest value and can never exceed alpha.
 *
 * @param pixels RGBA bytes, row-major; a trailing partial pixel is ignored.
 */
void premultiplyRgba(std::span<unsigned char> pixels) noexcept;

} // namespace melonrind
