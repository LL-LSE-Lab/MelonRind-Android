#include "model/Preview.h"
#include "model/SaturationObservation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
    std::cout << "Passed " << checks << " checks\n";
}
