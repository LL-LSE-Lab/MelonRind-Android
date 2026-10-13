#include "model/HudTransform.h"

#include <cmath>

namespace melonrind {
namespace {

bool plausible(float value) noexcept {
    return std::isfinite(value) && std::abs(value) < 100000.0f;
}

} // namespace

HudScaleEstimate estimateHudUnitScale(const HudCalibrationInput &input,
                                      const HudLayoutConstants &layout) {
    HudScaleEstimate result;
    if (!input.hungerOrigin || !input.heartOrigin) return result;
    if (!plausible(input.hungerX) || !plausible(input.hungerY) ||
        !plausible(input.heartX) || !plausible(input.heartY)) {
        return result;
    }
    if (!plausible(input.surfaceWidth) || !plausible(input.surfaceHeight) ||
        input.surfaceWidth <= 0.0f || input.surfaceHeight <= 0.0f) {
        return result;
    }
    if (!(layout.heartCenterOffset > 0.0f) || !(layout.hungerCenterOffset > 0.0f) ||
        !(layout.bottomMargin > 0.0f)) {
        return result;
    }

    // The two origins straddle the centre: their distance from the centre adds
    // up to one virtual screen width plus the asymmetry between the
    // left-anchored health bar and the right-anchored hunger bar.
    const float asymmetry =
        layout.heartCenterOffset - layout.hungerCenterOffset;
    const float unitsWide = input.heartX + input.hungerX + asymmetry;
    const float unitsTall =
        0.5f * (input.heartY + input.hungerY) + layout.bottomMargin;
    if (!(unitsWide > 1.0f) || !(unitsTall > 1.0f)) return result;

    result.fromWidth = input.surfaceWidth / unitsWide;
    result.fromHeight = input.surfaceHeight / unitsTall;
    // Least squares on 1/scale, which weights the much longer horizontal
    // baseline more heavily while still using the vertical origin.
    const double width = input.surfaceWidth;
    const double height = input.surfaceHeight;
    const double denominator = width * width + height * height;
    const double numerator = width * unitsWide + height * unitsTall;
    if (!(denominator > 0.0) || !(numerator > 0.0)) return result;
    result.combined = static_cast<float>(denominator / numerator);
    result.valid = std::isfinite(result.combined) && result.combined > 0.0f;
    result.consistent = std::abs(result.fromWidth - result.fromHeight) <=
                        0.05f * result.combined;
    return result;
}

} // namespace melonrind
