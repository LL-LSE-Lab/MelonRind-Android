#include "model/OverlayPlan.h"

#include "model/Preview.h"

#include <algorithm>
#include <cmath>

namespace melonrind {
namespace {

constexpr float kEpsilon = 1e-4f;

} // namespace

IconCrop canonicalIconCrop(float left, float right) {
    if (!std::isfinite(left) || !std::isfinite(right)) return IconCrop{0.0f, 0.0f};
    left = std::clamp(left, 0.0f, 1.0f);
    right = std::clamp(right, 0.0f, 1.0f);
    if (right - left <= kEpsilon) return IconCrop{0.0f, 0.0f};

    const auto fine = [](float start) {
        const int steps = std::clamp(
            static_cast<int>(
                std::lround((1.0f - start) * static_cast<float>(kOverlayFillSteps))),
            1, kOverlayFillSteps);
        return IconCrop{1.0f - static_cast<float>(steps) /
                                   static_cast<float>(kOverlayFillSteps),
                        1.0f};
    };

    // Snap outward on the quarter grid first: a crop whose snapped end reaches
    // the sprite edge is served by the finer right-anchored table, which is what
    // the right-to-left hunger and saturation fills produce.
    const int low = std::clamp(
        static_cast<int>(std::floor(left * kOverlayQuarterSteps + kEpsilon)), 0,
        kOverlayQuarterSteps - 1);
    const int high = std::clamp(
        static_cast<int>(std::ceil(right * kOverlayQuarterSteps - kEpsilon)), 1,
        kOverlayQuarterSteps);
    if (high >= kOverlayQuarterSteps) return fine(left);

    const int end = high > low ? high : std::min(low + 1, kOverlayQuarterSteps);
    return IconCrop{static_cast<float>(low) / kOverlayQuarterSteps,
                    static_cast<float>(end) / kOverlayQuarterSteps};
}

std::string iconCropKey(const IconCrop &crop) {
    auto const steps = [](float value, int grid) {
        return static_cast<int>(std::lround(value * static_cast<float>(grid)));
    };
    if (crop.right >= 1.0f - kEpsilon) {
        return "r" + std::to_string(steps(1.0f - crop.left, kOverlayFillSteps));
    }
    return "q" + std::to_string(steps(crop.left, kOverlayQuarterSteps)) + "_" +
           std::to_string(steps(crop.right, kOverlayQuarterSteps));
}

std::vector<IconCrop> registeredIconCrops() {
    std::vector<IconCrop> crops;
    crops.reserve(static_cast<std::size_t>(kOverlayFillSteps) +
                  kOverlayQuarterSteps * (kOverlayQuarterSteps - 1) / 2);
    for (int steps = 1; steps <= kOverlayFillSteps; ++steps) {
        crops.push_back(IconCrop{1.0f - static_cast<float>(steps) /
                                            static_cast<float>(kOverlayFillSteps),
                                 1.0f});
    }
    // The full icon is already covered by the finest right-anchored crop, so the
    // quarter table only needs the intervals that end inside the sprite.
    for (int low = 0; low < kOverlayQuarterSteps; ++low) {
        for (int high = low + 1; high < kOverlayQuarterSteps; ++high) {
            crops.push_back(IconCrop{static_cast<float>(low) / kOverlayQuarterSteps,
                                     static_cast<float>(high) /
                                         kOverlayQuarterSteps});
        }
    }
    return crops;
}

std::vector<IconDraw> planIconDraws(float before, float after,
                                    std::size_t iconCount, bool reverseFill) {
    std::vector<IconDraw> draws;
    if (iconCount == 0 || iconCount > 1000) return draws;
    for (auto const &slice :
         iconSlices(before, after, static_cast<int>(iconCount))) {
        const IconCrop crop{reverseFill ? 1.0f - slice.end : slice.start,
                            reverseFill ? 1.0f - slice.start : slice.end};
        if (crop.empty()) continue;
        draws.push_back(IconDraw{static_cast<std::size_t>(slice.index), crop});
    }
    return draws;
}

} // namespace melonrind
