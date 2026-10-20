#pragma once

#include <string_view>

namespace melonrind {

/** HUD layouts the calibration understands. */
enum class HudLayout {
    Unknown,
    Classic,
    Pocket,
};

/**
 * Vanilla HUD bar layout, in UI units.
 *
 * Classic UI, measured against the shipped 1.26.51.1 client: the health bar's
 * owner origin sits 101 units left of the screen centre and the hunger bar's
 * 100 units right of it, while both origins sit 41 units above the bottom edge.
 * The two bars are therefore symmetric about the centre, which is what makes
 * the unit scale recoverable from the owner positions alone.
 *
 * Pocket UI (`hud_screen.json` -> `not_centered_gui_elements`) anchors the same
 * two renderer controls at the top corners: the heart control at `[2, 2]` from
 * the top-left, the hunger control at `[-2, 2]` from the top-right.  Both
 * controls are one unit wide.  The cached position is the control's left edge,
 * so the hunger origin is `virtualWidth - 3`; the 9-unit icon extends one unit
 * to the right of it.  Consequently `heartX + hungerX + 1` is still the virtual
 * width, i.e. the existing width baseline works for Pocket too.  Only the
 * Classic vertical baseline (`virtualHeight - 41`) is invalid there.
 *
 * The renderers publish their icon origins as *local* offsets, so the owner's
 * cached position (the `glm::vec2` at `::UIControl + 0x10` that both render
 * entry points pass through `0x968bc9c`) supplies the missing origin, and the
 * screen size in units supplies the pixel-per-unit factor.
 */
struct HudLayoutConstants {
    // Classic bottom-centre layout.
    float heartCenterOffset{101.0f};
    float hungerCenterOffset{100.0f};
    float bottomMargin{41.0f};

    // Pocket top-corner layout detection.  A Pocket row sits ~2 units below the
    // top edge; the Classic row is ~41 units above the bottom edge, so its y
    // origin is always far larger.  The edge fractions additionally reject a
    // Classic pair near the screen centre.
    float pocketEdgeFraction{0.15f};
    float pocketTopFraction{0.10f};
    float pocketRowTolerance{1.0f};
    float pocketTopFloor{8.0f};
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
    HudLayout layout{HudLayout::Unknown};
    float fromWidth{};
    /** Classic vertical baseline; retained in Pocket mode for diagnostics. */
    float fromHeight{};
    float combined{};
    bool consistent{};
};

/**
 * Recovers pixels per UI unit from one frame of bar origins.
 *
 * The width baseline (`heartX + hungerX + asymmetry`) is shared by both
 * layouts.  `combined` is therefore the Classic least-squares fit when the two
 * baselines agree, and `fromWidth` alone when the top-corner Pocket layout is
 * recognised.  Pocket mode leaves `fromHeight` at its Classic value so a log
 * shows why the bottom-edge baseline was rejected.
 */
[[nodiscard]] HudScaleEstimate estimateHudUnitScale(
    const HudCalibrationInput &input, const HudLayoutConstants &layout = {});

/** Stable lower-case name used by the on-device diagnostic log. */
[[nodiscard]] constexpr std::string_view hudLayoutName(HudLayout layout) noexcept {
    switch (layout) {
    case HudLayout::Classic:
        return "classic";
    case HudLayout::Pocket:
        return "pocket";
    case HudLayout::Unknown:
        break;
    }
    return "unknown";
}

} // namespace melonrind
