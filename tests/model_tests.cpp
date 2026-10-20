#include "model/HudTransform.h"
#include "model/Markers.h"
#include "model/OverlayArt.h"
#include "model/OverlayPlan.h"
#include "model/Preview.h"
#include "model/SaturationObservation.h"
#include "runtime/generated/OverlayArt.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace {
int checks = 0;
void check(bool ok, char const* message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }
}

int main() {
    using namespace melonrind;
    SaturationObservation observed;
    check(!observed.get(100, 1), "no saturation before native synchronization");
    check(observed.receive(100, 1, 10, 5) && observed.get(100, 1) == 5, "native saturation retained");
    observed.receive(100, 1, 11, {});
    check(observed.get(100, 1) == 5, "health-only delta preserves saturation");
    check(!observed.receive(100, 1, 9, 8) && observed.get(100, 1) == 5, "older packet cannot overwrite saturation");
    observed.receive(100, 1, 11, 4);
    check(observed.get(100, 1) == 4, "same-tick attribute delta accepted");
    check(!observed.get(200, 1) && !observed.get(100, 2), "different player cannot inherit cached saturation");
    observed.receive(200, 1, 1, {});
    check(!observed.get(200, 1), "replacement player with reused runtime ID starts unknown");
    observed.receive(200, 1, 2, 6);
    observed.forget(100);
    check(observed.get(200, 1) == 6, "unrelated render instance cannot erase local observation");
    observed.forget(200);
    check(!observed.get(200, 1), "death clears observation even if respawn reuses player identity");
    observed.receive(200, 1, 1, 7);
    observed.clear();
    check(!observed.get(200, 1), "world exit clears observation");
    observed.receive(200, 1, 0, 3);
    check(observed.get(200, 1) == 3, "new session tick zero accepted after reset");
    observed.receive(200, 1, 1, std::numeric_limits<float>::quiet_NaN());
    check(!observed.get(200, 1), "invalid synchronized saturation becomes unknown");
    auto meal = previewMeal({19, 20, 18, 20}, {8, .8f}, SaturationCap::AttributeMaximum);
    check(meal && meal->hungerAfter == 20 && meal->saturationAfter == 20, "meal clamps both overflows");
    check(!previewMeal({20, 20, 2, 20}, {4, .3f}, SaturationCap::AttributeMaximum), "full hunger rejects ordinary food");
    check(previewMeal({20, 20, 2, 20}, {4, .3f, true}, SaturationCap::AttributeMaximum).has_value(), "always edible at full hunger");
    meal = previewMeal({10, 20, {}, 20}, {5, .6f}, SaturationCap::AttributeMaximum);
    check(meal && meal->hungerAfter == 15 && !meal->saturationAfter, "unknown saturation preserves hunger preview only");
    auto const nan = std::numeric_limits<float>::quiet_NaN();
    check(!previewMeal(Nutrition{nan, 20, std::nullopt, 20}, Food{4, .3f}, SaturationCap::AttributeMaximum), "invalid hunger rejected");
    check(!previewMeal(Nutrition{10, 20, std::nullopt, 20}, Food{4, nan}, SaturationCap::AttributeMaximum), "invalid food rejected");
    auto a = previewMeal({0, 20, 0, 20}, {6, 1.2f}, SaturationCap::AttributeMaximum);
    auto b = previewMeal({0, 20, 0, 20}, {6, 1.2f}, SaturationCap::HungerAfterEating);
    check(a && b && near(*a->saturationAfter, 14.4f) && *b->saturationAfter == 6, "cap strategies remain distinguishable for calibration");
    auto pork1 = previewMeal({0, 20, 0, 20}, {8, .8f}, SaturationCap::HungerAfterEating);
    auto pork2 = previewMeal({8, 20, 4, 20}, {8, .8f}, SaturationCap::HungerAfterEating);
    check(pork1 && pork1->saturationAfter == 8, "observed target client: porkchop at H=0 yields S=8");
    check(pork2 && pork2->saturationAfter == 16, "next meal uses new hunger cap, not previous attribute maximum 8");
    auto uncertain = previewMeal({10, 20, 8, 20}, {4, .3f, false, true}, SaturationCap::AttributeMaximum);
    check(uncertain && uncertain->uncertainEffects, "uncertain food keeps hunger preview but marks healing uncertain");
    check(!previewRegeneration(*uncertain, {10, 20, RuleState::Enabled}), "uncertain food suppresses healing preview");
    auto slices = iconSlices(1, 3, 10);
    check(slices.size() == 2 && slices[0].index == 0 && slices[0].start == .5f && slices[0].end == 1
          && slices[1].index == 1 && slices[1].start == 0 && slices[1].end == .5f, "partial icon gain does not repaint old half");
    check(iconSlices(nan, 5, 10).empty() && iconSlices(5, 4, 10).empty(), "invalid and reversed intervals rejected");
    slices = iconSlices(19.5f, 100, 10);
    check(slices.size() == 1 && slices[0].index == 9 && slices[0].start == .75f, "icon count clips overflow");
    auto regen = previewRegeneration({17, 18, 0, 0}, {10, 20, RuleState::Enabled});
    check(regen && regen->lowHp == 0 && regen->highHp == 1, "hunger 18 permits one last heal only with small initial exhaustion");
    check(!previewRegeneration({17, 20, {}, {}}, {10}), "unknown saturation suppresses healing");
    check(!previewRegeneration({17, 20, 0, 20}, {10, 20, RuleState::Disabled}), "disabled rule suppresses healing");
    check(!previewRegeneration({17, 20, 0, 20}, {10, 20, RuleState::Unknown, false, false, false}), "unknown rule opt-out works");
    check(!previewRegeneration({17, 20, 0, 20}, {10, 20, RuleState::Enabled, true}), "peaceful suppressed");
    check(!previewRegeneration({17, 20, 0, 20}, {0}), "dead player suppressed");
    regen = previewRegeneration({17, 20, 0, 20}, {19.5f});
    check(regen && regen->highHp == .5f && regen->lowHp == .5f, "healing clips to fractional missing HP");
    for (int h = 0; h <= 20; ++h) {
        for (int s = 0; s <= 80; ++s) {
            regen = previewRegeneration({0, static_cast<float>(h), 0, s / 4.0f}, {1});
            check(regen && regen->lowHp <= regen->highHp && regen->lowHp >= 0 && regen->highHp <= 19, "bounded ordered regen range");
            if (h < 18) check(regen->highHp == 0, "below threshold never heals");
        }
    }
    std::array<MarkerPoint, 4> rawPoints{MarkerPoint{40, 300}, MarkerPoint{-10001, 20},
                                         MarkerPoint{nan, 30}, MarkerPoint{50, 1e6f}};
    auto markers = usableMarkerPoints(rawPoints, 8000.0f);
    check(markers.size() == 1 && markers[0].x == 40 && markers[0].y == 300,
          "calibration markers drop sentinel, non-finite and oversized origins");
    std::array<MarkerPoint, 3> orderedPoints{MarkerPoint{1, 2}, MarkerPoint{3, 4},
                                             MarkerPoint{5, 6}};
    markers = usableMarkerPoints(orderedPoints, 8000.0f);
    check(markers.size() == 3 && markers[2].x == 5 && markers[2].y == 6,
          "accepted origins keep their order and values");
    check(usableMarkerPoints(orderedPoints, 0.0f).empty(),
          "non-positive coordinate limit rejects every origin");

    // Both rows come from the on-device calibration runs: the same client at a
    // 100% and at an 83% UI scale, over a 3168x1440 overlay.  The icon pitch in
    // the screenshots was 48 px and 40 px for 8-unit spacing, i.e. 6 and 5
    // pixels per unit.
    HudCalibrationInput full{};
    full.hungerOrigin = full.heartOrigin = true;
    full.hungerX = 364;
    full.hungerY = 199;
    full.heartX = 163;
    full.heartY = 199;
    full.surfaceWidth = 3168;
    full.surfaceHeight = 1440;
    auto scale = estimateHudUnitScale(full);
    check(scale.valid && scale.consistent && near(scale.combined, 6.0f),
          "calibration recovers the measured 6.0 px/unit at 100% UI scale");
    check(near(scale.fromWidth, 6.0f) && near(scale.fromHeight, 6.0f),
          "both calibration baselines agree at 100% UI scale");
    HudCalibrationInput scaled = full;
    scaled.hungerX = 417;
    scaled.heartX = 216;
    scaled.hungerY = scaled.heartY = 247;
    scale = estimateHudUnitScale(scaled);
    check(scale.valid && scale.consistent && scale.combined > 4.98f &&
              scale.combined < 5.01f,
          "calibration tracks the UI scale change to 5.0 px/unit");
    HudCalibrationInput missing = full;
    missing.heartOrigin = false;
    check(!estimateHudUnitScale(missing).valid,
          "calibration needs both bar origins");
    HudCalibrationInput degenerate = full;
    degenerate.surfaceHeight = nan;
    check(!estimateHudUnitScale(degenerate).valid,
          "calibration rejects a non-finite surface size");

    // Overlay art: a 9x9 sprite with a distinct alpha per column, so the crop
    // mapping is checkable pixel by pixel.
    std::array<unsigned char, 9 * 9 * 4> art{};
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            art[(y * 9 + x) * 4 + 0] = static_cast<unsigned char>(x);
            art[(y * 9 + x) * 4 + 1] = static_cast<unsigned char>(y);
            art[(y * 9 + x) * 4 + 3] = 255;
        }
    }
    auto fullArt = cropOverlayArt(art, 0.0f, 1.0f, 6);
    check(fullArt.width == 54 && fullArt.height == 54,
          "full crop covers the whole 9-unit sprite at the art resolution");
    check(fullArt.pixels[(0 * 54 + 0) * 4 + 0] == 0 &&
              fullArt.pixels[(0 * 54 + 53) * 4 + 0] == 8 &&
              fullArt.pixels[(53 * 54 + 0) * 4 + 1] == 8,
          "a full crop maps destination pixels back to sprite columns and rows");
    auto halfArt = cropOverlayArt(art, 0.5f, 1.0f, 6);
    check(halfArt.width == 27 && halfArt.height == 54 &&
              halfArt.pixels[0] == 4,
          "a half crop starts at the sprite column that contains the boundary");
    check(cropOverlayArt(art, 0.5f, 0.5f, 6).empty() &&
              cropOverlayArt(art, nan, 1.0f, 6).empty(),
          "degenerate crops produce no image");

    // Registration hands the bytes to an ARGB_8888 bitmap that Android blends as
    // premultiplied, so a straight-alpha colour may only survive where it is
    // covered by the pixel's alpha.  The crop itself keeps the sprite's colours;
    // premultiplyRgba is what makes them safe to register.
    std::array<unsigned char, 9 * 9 * 4> straightArt{};
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            auto const offset = static_cast<std::size_t>((y * 9 + x) * 4);
            straightArt[offset + 0] = 255;
            straightArt[offset + 1] = 248;
            straightArt[offset + 2] = 225;
            straightArt[offset + 3] = (x == 4 && y == 4) ? 255 : 0;
        }
    }
    auto straightCrop = cropOverlayArt(straightArt, 0.0f, 1.0f, 6);
    check(straightCrop.pixels[0] == 255 && straightCrop.pixels[3] == 0,
          "a crop keeps the sprite's straight-alpha colours");
    premultiplyRgba(straightCrop.pixels);
    check(straightCrop.pixels[0] == 0 && straightCrop.pixels[1] == 0 &&
              straightCrop.pixels[2] == 0 && straightCrop.pixels[3] == 0,
          "premultiplying clears the colour of a transparent pixel");
    bool outlineSurvived = false;
    for (std::size_t i = 0; i + 3 < straightCrop.pixels.size(); i += 4) {
        check(straightCrop.pixels[i] <= straightCrop.pixels[i + 3] &&
                  straightCrop.pixels[i + 1] <= straightCrop.pixels[i + 3] &&
                  straightCrop.pixels[i + 2] <= straightCrop.pixels[i + 3],
              "premultiplied pixels never carry colour above their alpha");
        if (straightCrop.pixels[i + 3] == 255) outlineSurvived = true;
    }
    check(outlineSurvived, "premultiplying keeps the opaque art");

    std::array<unsigned char, 4 * 4> premul{
        255, 128, 0, 255,    // opaque: unchanged
        255, 255, 255, 0,    // transparent: becomes transparent black
        200, 200, 200, 128,  // half transparent: scaled and rounded
        10, 20, 30, 1};      // almost transparent: rounds down to zero
    premultiplyRgba(premul);
    check(premul[0] == 255 && premul[1] == 128 && premul[2] == 0 && premul[3] == 255,
          "opaque premultiplied pixels are unchanged");
    check(premul[4] == 0 && premul[5] == 0 && premul[6] == 0 && premul[7] == 0,
          "a transparent colour becomes transparent black");
    check(premul[8] == 100 && premul[9] == 100 && premul[10] == 100 &&
              premul[11] == 128,
          "a semi-transparent colour is scaled by its alpha");
    check(premul[12] == 0 && premul[13] == 0 && premul[14] == 0 && premul[15] == 1,
          "rounding keeps premultiplied components inside their alpha");
    std::array<unsigned char, 6> trailing{255, 255, 255, 0, 9, 9};
    premultiplyRgba(trailing);
    check(trailing[0] == 0 && trailing[4] == 9 && trailing[5] == 9,
          "a trailing partial pixel is ignored");

    // Crop quantisation has to stay inside the pre-registered set, otherwise the
    // overlay would reference an image id the launcher never received.
    const auto registry = registeredIconCrops();
    check(registry.size() == static_cast<std::size_t>(kOverlayFillSteps) + 6,
          "the registered crop set is the fine fill ladder plus the quarter grid");
    auto const registered = [&registry](const IconCrop &crop) {
        return std::find(registry.begin(), registry.end(), crop) != registry.end();
    };
    check(canonicalIconCrop(0.0f, 1.0f) == IconCrop{0.0f, 1.0f} &&
              canonicalIconCrop(0.9f, 1.0f) == IconCrop{0.875f, 1.0f} &&
              canonicalIconCrop(0.97f, 1.0f) == IconCrop{0.9375f, 1.0f},
          "a fill that reaches the sprite edge keeps the fine resolution");
    check(canonicalIconCrop(0.3f, 0.7f) == IconCrop{0.25f, 0.75f} &&
              canonicalIconCrop(0.2f, 0.21f) == IconCrop{0.0f, 0.25f},
          "an interior range snaps outward onto the quarter grid");
    check(canonicalIconCrop(0.4f, 0.0f).empty() &&
              canonicalIconCrop(nan, 1.0f).empty(),
          "an empty or non-finite range quantises to nothing");
    for (float left = 0.0f; left <= 1.0f; left += 0.05f) {
        for (float right = left; right <= 1.0f; right += 0.05f) {
            const auto crop = canonicalIconCrop(left, right);
            if (crop.empty()) continue;
            check(registered(crop), "every quantised crop is pre-registered");
            check(crop.left >= left - 0.26f && crop.right <= right + 0.26f,
                  "quantisation stays within a quarter icon of the request");
        }
    }

    // The embedded sprites are straight alpha.  The food preview is the worst
    // case: it recolours every pixel, so its fully transparent pixels carry a
    // bright pale colour, which is what leaked on screen as a solid rectangle
    // instead of an outline.  Every crop the renderer can register is converted
    // before registration, so the premultiplied invariant has to hold for the
    // real art too.
    const std::pair<std::string_view, std::span<const unsigned char>> embedded[3] = {
        {"saturation", melonrind::generated::kSaturationArt},
        {"food preview", melonrind::generated::kFoodPreviewArt},
        {"regeneration", melonrind::generated::kRegenArt},
    };
    for (auto const &art : embedded) {
        const auto pixels = art.second;
        bool anyOpaque = false;
        bool anyTransparent = false;
        for (auto const &crop : registry) {
            auto image = cropOverlayArt(pixels, crop.left, crop.right);
            check(!image.empty(), "every registered crop of the embedded art is drawn");
            premultiplyRgba(image.pixels);
            for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
                const unsigned alpha = image.pixels[i + 3];
                check(image.pixels[i] <= alpha && image.pixels[i + 1] <= alpha &&
                          image.pixels[i + 2] <= alpha,
                      "registered crops are premultiplied");
                anyOpaque = anyOpaque || alpha == 255;
                anyTransparent = anyTransparent || alpha == 0;
            }
        }
        check(anyOpaque && anyTransparent,
              "the embedded art keeps its opaque outline and its transparent holes");
    }

    // Planned fills: the hunger bar fills from its right end, the health bar
    // from its left.
    auto saturationDraws = planIconDraws(0.0f, 3.8f, 10, true);
    check(saturationDraws.size() == 2 && saturationDraws[0].index == 0 &&
              saturationDraws[0].crop == IconCrop{0.0f, 1.0f} &&
              saturationDraws[1].index == 1 &&
              near(saturationDraws[1].crop.left, 0.1f) &&
              saturationDraws[1].crop.right == 1.0f,
          "saturation fills the rightmost icons first, with the tail on the right");
    auto previewDraws = planIconDraws(9.0f, 13.0f, 10, true);
    check(previewDraws.size() == 3 && previewDraws[0].index == 4 &&
              near(previewDraws[0].crop.left, 0.0f) &&
              near(previewDraws[0].crop.right, 0.5f) &&
              previewDraws[1].crop == IconCrop{0.0f, 1.0f} &&
              previewDraws[2].index == 6 &&
              near(previewDraws[2].crop.left, 0.5f),
          "a hunger gain paints the empty part of the boundary icon");
    auto singleIcon = planIconDraws(8.5f, 9.5f, 10, true);
    check(singleIcon.size() == 1 && singleIcon[0].index == 4 &&
              near(singleIcon[0].crop.left, 0.25f) &&
              near(singleIcon[0].crop.right, 0.75f),
          "a one-point gain inside a single icon yields an interior sub-rect");
    auto heartDraws = planIconDraws(9.0f, 13.0f, 10, false);
    check(heartDraws.size() == 3 && heartDraws[0].index == 4 &&
              near(heartDraws[0].crop.left, 0.5f) &&
              heartDraws[0].crop.right == 1.0f &&
              heartDraws[2].index == 6 &&
              near(heartDraws[2].crop.right, 0.5f),
          "the health preview fills left to right");
    check(planIconDraws(0.0f, 20.0f, 10, true).size() == 10 &&
              planIconDraws(5.0f, 5.0f, 10, true).empty() &&
              planIconDraws(0.0f, 5.0f, 0, true).empty(),
          "planned fills clamp to the bar and reject empty requests");
    for (auto const &draw : saturationDraws) {
        check(registered(canonicalIconCrop(draw.crop.left, draw.crop.right)),
              "planned fills resolve to a registered crop");
    }
    for (auto const &draw : heartDraws) {
        check(registered(canonicalIconCrop(draw.crop.left, draw.crop.right)),
              "planned heart fills resolve to a registered crop");
    }
    std::cout << "Passed " << checks << " checks\n";
}
