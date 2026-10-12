#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace melonrind::runtime {

/**
 * Defines the upper bound used when predicting the saturation value after a
 * meal.  Bedrock has used both forms of this rule over its lifetime, so the
 * choice stays in the runtime layer instead of being hard-coded in a hook.
 */
enum class SaturationCap : std::uint8_t {
    AttributeMaximum,
    HungerAfterEating,
};

/**
 * Native runtime configuration.  This is deliberately an aggregate so it can
 * be used directly with pl::config::ConfigFile by the mod entry point.
 */
struct Config {
    int version{1};
    bool enabled{true};
    bool showSaturation{true};
    bool showFoodPreview{true};
    bool showRegeneration{true};
    bool pulse{true};
    float opacity{0.8f};
    float scale{1.0f};
    float hudPosX{-20000.0f};
    float hudPosY{-10080.0f};
    bool estimateUnknownRule{true};
    bool diagnostics{false};
    SaturationCap saturationCap{SaturationCap::HungerAfterEating};

    /**
     * Clamp values loaded from a user file or Mod Menu.  Returning the same
     * object makes this convenient at lifecycle boundaries.
     */
    Config &normalize() noexcept {
        version = 1;
        if (!std::isfinite(opacity)) opacity = 0.8f;
        if (!std::isfinite(scale)) scale = 1.0f;
        if (!std::isfinite(hudPosX)) hudPosX = -20000.0f;
        if (!std::isfinite(hudPosY)) hudPosY = -10080.0f;
        opacity = std::clamp(opacity, 0.0f, 1.0f);
        scale = std::clamp(scale, 0.5f, 3.0f);
        // Keep the sentinel values usable while preventing accidental giant
        // coordinates from an edited JSON file.
        hudPosX = std::clamp(hudPosX, -20000.0f, 100000.0f);
        hudPosY = std::clamp(hudPosY, -20000.0f, 100000.0f);
        return *this;
    }
};

[[nodiscard]] inline constexpr std::string_view saturationCapName(
    SaturationCap cap) noexcept {
    return cap == SaturationCap::AttributeMaximum ? "attributeMaximum"
                                                   : "hungerAfterEating";
}

[[nodiscard]] inline SaturationCap saturationCapFromName(
    std::string_view name) noexcept {
    return name == "attributeMaximum" ? SaturationCap::AttributeMaximum
                                       : SaturationCap::HungerAfterEating;
}

} // namespace melonrind::runtime

