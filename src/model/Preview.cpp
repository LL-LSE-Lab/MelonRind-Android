#include "model/Preview.h"

#include <algorithm>
#include <cmath>

namespace melonrind {
namespace {
bool bounded(float value, float low, float high) {
    return std::isfinite(value) && value >= low && value <= high;
}

float simulate(float hunger, float saturation, float missingHp, float exhaustion, RegenRules const& rules) {
    float healed = 0;
    // At most 20 hunger + 20 saturation in the supported vanilla model.
    for (int step = 0; step < 128 && healed < missingHp; ++step) {
        for (int drain = 0; drain < 128; ++drain) {
            bool const due = rules.strictThreshold ? exhaustion > rules.threshold : exhaustion >= rules.threshold;
            if (!due) break;
            exhaustion -= rules.threshold;
            if (saturation > 0) saturation = std::max(0.0f, saturation - 1);
            else hunger = std::max(0.0f, hunger - 1);
        }
        if (hunger < rules.minimumHunger) break;
        healed = std::min(missingHp, healed + 1);
        exhaustion += rules.costPerHp;
    }
    return healed;
}
} // namespace

std::optional<MealPreview> previewMeal(Nutrition const& state, Food const& food, SaturationCap cap) {
    if (!bounded(state.maxHunger, 1, 20) || !bounded(state.hunger, 0, state.maxHunger)
        || !bounded(state.maxSaturation, 0, 20) || food.nutrition < 0 || food.nutrition > 1000
        || !bounded(food.saturationModifier, 0, 1000)) return {};
    if (state.hunger >= state.maxHunger && !food.alwaysEat) return {};

    MealPreview result{state.hunger, std::min(state.maxHunger, state.hunger + food.nutrition), {}, {}, food.uncertainEffects};
    if (state.saturation && bounded(*state.saturation, 0, state.maxSaturation)) {
        auto const limit = cap == SaturationCap::HungerAfterEating
            ? std::min(state.maxSaturation, result.hungerAfter) : state.maxSaturation;
        result.saturationBefore = state.saturation;
        result.saturationAfter = std::min(limit, *state.saturation + 2.0f * food.nutrition * food.saturationModifier);
    }
    return result;
}

std::vector<IconSlice> iconSlices(float before, float after, int iconCount) {
    std::vector<IconSlice> result;
    if (!std::isfinite(before) || !std::isfinite(after) || iconCount <= 0 || iconCount > 1000) return result;
    before = std::clamp(before, 0.0f, 2.0f * iconCount);
    after = std::clamp(after, 0.0f, 2.0f * iconCount);
    if (after <= before) return result;
    auto const first = static_cast<int>(std::floor(before / 2));
    auto const last = std::min(iconCount, static_cast<int>(std::ceil(after / 2)));
    result.reserve(last - first);
    for (int index = first; index < last; ++index) {
        result.push_back({index, std::clamp(before / 2 - index, 0.0f, 1.0f),
                         std::clamp(after / 2 - index, 0.0f, 1.0f)});
    }
    return result;
}

std::optional<RegenPreview>
previewRegeneration(MealPreview const& meal, RegenContext const& context, RegenRules const& rules) {
    if (meal.uncertainEffects
        || !meal.saturationAfter || !bounded(*meal.saturationAfter, 0, 20)
        || !bounded(meal.hungerAfter, 0, 20) || !bounded(context.maxHealth, 1, 2000)
        || !bounded(context.health, 0, context.maxHealth) || context.health <= 0
        || context.health >= context.maxHealth || context.peaceful || context.suppressed
        || context.naturalRegeneration == RuleState::Disabled
        || (context.naturalRegeneration == RuleState::Unknown && !context.allowUnknownRule)
        || !bounded(rules.threshold, 0.1f, 20) || !bounded(rules.costPerHp, 1, 20)
        || !bounded(rules.minimumHunger, 1, 20) || !bounded(rules.unknownExhaustionMaximum, 0, 20)) return {};

    float const missing = context.maxHealth - context.health;
    // Monotonic in the unknown starting exhaustion for this stationary model.
    float const low = simulate(meal.hungerAfter, *meal.saturationAfter, missing, rules.unknownExhaustionMaximum, rules);
    float const high = simulate(meal.hungerAfter, *meal.saturationAfter, missing, 0, rules);
    return RegenPreview{low, high, context.naturalRegeneration == RuleState::Unknown};
}
} // namespace melonrind
