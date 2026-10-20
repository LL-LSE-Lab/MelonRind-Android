#include "model/HudTransform.h"

#include <algorithm>
#include <cmath>

namespace melonrind {
namespace {

bool plausible(float value) noexcept {
    return std::isfinite(value) && std::abs(value) < 100000.0f;
}

bool validLayoutConstants(const HudLayoutConstants &layout) noexcept {
    return layout.heartCenterOffset > 0.0f &&
           layout.hungerCenterOffset > 0.0f &&
           layout.bottomMargin > 0.0f &&
           layout.pocketEdgeFraction > 0.0f &&
           layout.pocketEdgeFraction < 0.5f &&
           layout.pocketTopFraction > 0.0f &&
           layout.pocketTopFraction < 0.5f &&
           layout.pocketRowTolerance >= 0.0f &&
           layout.pocketTopFloor >= 0.0f;
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
    if (!validLayoutConstants(layout)) return result;

    // The two origins straddle the centre in Classic and the screen in Pocket,
    // but in both layouts their distance adds up to one virtual screen width
    // plus the asymmetry between the left-anchored health bar and the
    // right-anchored hunger bar.  This width baseline is therefore shared.
    const float asymmetry =
        layout.heartCenterOffset - layout.hungerCenterOffset;
    const float unitsWide = input.heartX + input.hungerX + asymmetry;
    if (!(unitsWide > 1.0f)) return result;
    const float fromWidth = input.surfaceWidth / unitsWide;
    if (!std::isfinite(fromWidth) || fromWidth <= 0.0f) return result;
    result.fromWidth = fromWidth;
    result.valid = true;

    // Classic: both owner origins sit bottomMargin units above the bottom edge.
    const float unitsTall =
        0.5f * (input.heartY + input.hungerY) + layout.bottomMargin;
    if (unitsTall > 1.0f) {
        const float fromHeight = input.surfaceHeight / unitsTall;
        result.fromHeight = fromHeight;
        if (std::isfinite(fromHeight) && fromHeight > 0.0f) {
            // Least squares on 1/scale, which weights the much longer
            // horizontal baseline more heavily while still using the vertical
            // origin.
            const double width = input.surfaceWidth;
            const double height = input.surfaceHeight;
            const double denominator = width * width + height * height;
            const double numerator = width * unitsWide + height * unitsTall;
            if (denominator > 0.0 && numerator > 0.0) {
                result.combined =
                    static_cast<float>(denominator / numerator);
                result.valid = std::isfinite(result.combined) &&
                               result.combined > 0.0f;
                result.consistent = std::abs(fromWidth - fromHeight) <=
                                    0.05f * result.combined;
            }
        }
    }
    if (result.consistent) {
        result.layout = HudLayout::Classic;
        return result;
    }

    // Pocket UI: the bars are anchored inside the top corners, so the Classic
    // bottom-edge baseline disagrees with the width baseline.  Recognise the
    // top-row layout from the origins themselves and use the width baseline,
    // which is valid for both layouts.
    const float span = input.heartX + input.hungerX;
    if (!(span > 1.0f) || input.heartX < 0.0f || input.hungerX < 0.0f) {
        return result;
    }
    const float maxY =
        std::max(std::abs(input.heartY), std::abs(input.hungerY));
    const bool edgeAnchored =
        input.heartX <= layout.pocketEdgeFraction * span &&
        input.hungerX >= (1.0f - layout.pocketEdgeFraction) * span;
    const bool topRow =
        std::abs(input.heartY - input.hungerY) <= layout.pocketRowTolerance &&
        maxY <= std::max(layout.pocketTopFloor,
                         layout.pocketTopFraction * span);
    if (!edgeAnchored || !topRow) return result;

    result.layout = HudLayout::Pocket;
    result.combined = fromWidth;
    result.valid = true;
    result.consistent = true;
    return result;
}

} // namespace melonrind
