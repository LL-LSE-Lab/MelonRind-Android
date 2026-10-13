#pragma once

namespace melonrind {

/**
 * Vanilla HUD bar layout, in UI units.
 *
 * Measured against the shipped 1.26.51.1 client: the health bar's owner origin
 * sits 101 units left of the screen centre and the hunger bar's 100 units right
 * of it, while both origins sit 41 units above the bottom edge.  The two bars
 * are therefore symmetric about the centre, which is what makes the unit scale
 * recoverable from the owner positions alone.
 *
 * The renderers publish their icon origins as *local* offsets, so the owner's
 * cached position (the `glm::vec2` at `::UIControl + 0x10` that both render
 * entry points pass through `0x968bc9c`) supplies the missing origin, and the
 * screen size in units supplies the pixel-per-unit factor.
 */
struct HudLayoutConstants {
    float heartCenterOffset{101.0f};
    float hungerCenterOffset{100.0f};
    float bottomMargin{41.0f};
};

/** Everything the calibration reads, already copied out of the game. */
struct HudCalibrationInput {
    bool hungerOrigin{};
    bool heartOrigin{};
    float hungerX{};
    float hungerY{};
    float heartX{};
    float heartY{};
    /** Launcher overlay size in pixels, from `getHudSurfaceSize()`. */
    float surfaceWidth{};
    float surfaceHeight{};
};

/**
 * Unit scale (pixels per UI unit) derived from the bar layout.
 *
 * `fromWidth` uses the two owner origins, `fromHeight` the shared vertical
 * origin; `combined` is the least-squares fit of both, and `consistent` reports
 * whether they agree closely enough to trust the layout constants.
 */
struct HudScaleEstimate {
    bool valid{};
    float fromWidth{};
    float fromHeight{};
    float combined{};
    bool consistent{};
};

[[nodiscard]] HudScaleEstimate estimateHudUnitScale(
    const HudCalibrationInput &input, const HudLayoutConstants &layout = {});

} // namespace melonrind
