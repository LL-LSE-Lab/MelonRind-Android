#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace melonrind {

/** A horizontal sub-rect of one icon sprite, as fractions (0 = sprite left). */
struct IconCrop {
    float left{};
    float right{1.0f};

    [[nodiscard]] bool empty() const noexcept { return right - left <= 0.0f; }
};

[[nodiscard]] inline bool operator==(const IconCrop &lhs,
                                     const IconCrop &rhs) noexcept {
    return lhs.left == rhs.left && lhs.right == rhs.right;
}

/** One icon that receives overlay art, plus the sub-rect to paint. */
struct IconDraw {
    std::size_t index{};
    IconCrop crop;
};

/** Fine steps the right-anchored fill (hunger, saturation) is quantised to. */
constexpr int kOverlayFillSteps = 16;
/** Quarter-icon grid the preview and regeneration ranges already sit on. */
constexpr int kOverlayQuarterSteps = 4;

/**
 * Quantises a sub-rect to one of the pre-registered crops.
 *
 * The launcher's draw command has no source rectangle, so every fill has to be
 * a registered image.  A crop that reaches the sprite's right edge - which is
 * what the right-to-left hunger and saturation bars always produce - keeps the
 * fine fill resolution, while a crop that ends inside the sprite is snapped
 * outward onto the quarter grid that the preview and regeneration ranges are
 * already aligned to (hunger and health both move in half-point steps, i.e.
 * quarter icons).
 */
[[nodiscard]] IconCrop canonicalIconCrop(float left, float right);

/** Stable key for a crop; also used as the registered image id suffix. */
[[nodiscard]] std::string iconCropKey(const IconCrop &crop);

/** Every crop the renderer pre-registers, in registration order. */
[[nodiscard]] std::vector<IconCrop> registeredIconCrops();

/**
 * Splits a bar range into the per-icon sub-rects that paint it.
 *
 * @param before Value the bar shows now, in bar units (two units per icon).
 * @param after Value after the previewed change.
 * @param iconCount Icons the bar has room for.
 * @param reverseFill True for bars that fill from their right end (hunger and
 *        saturation), false for the left-to-right health bar.
 */
[[nodiscard]] std::vector<IconDraw> planIconDraws(float before, float after,
                                                  std::size_t iconCount,
                                                  bool reverseFill);

} // namespace melonrind
