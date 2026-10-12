#pragma once

#include <optional>
#include <vector>

namespace melonrind {

enum class SaturationCap { AttributeMaximum, HungerAfterEating };
enum class RuleState { Unknown, Enabled, Disabled };

struct Nutrition {
    float hunger{};
    float maxHunger{20};
    std::optional<float> saturation;
    float maxSaturation{20};
};

struct Food {
    int nutrition{};
    float saturationModifier{};
    bool alwaysEat{};
    bool uncertainEffects{};
};

struct MealPreview {
    float hungerBefore{};
    float hungerAfter{};
    std::optional<float> saturationBefore;
    std::optional<float> saturationAfter;
    // Some foods have effects that can change hunger/saturation or healing in
    // ways the client cannot infer from the item components alone.  Keep that
    // uncertainty attached to the meal so the heart preview can be hidden
    // without hiding the useful hunger/saturation preview.
    bool uncertainEffects{};
};

[[nodiscard]] std::optional<MealPreview> previewMeal(Nutrition const&, Food const&, SaturationCap);

// Fraction of a single icon occupied by [before, after], in logical fill order.
struct IconSlice {
    int index{};
    float start{};
    float end{};
};
[[nodiscard]] std::vector<IconSlice> iconSlices(float before, float after, int iconCount);

struct RegenContext {
    float health{};
    float maxHealth{20};
    RuleState naturalRegeneration{RuleState::Unknown};
    bool peaceful{};
    bool suppressed{};
    bool allowUnknownRule{true};
};

// Model parameters, never read from the player's exhaustion attribute.
struct RegenRules {
    float threshold{4};
    float costPerHp{6};
    float minimumHunger{18};
    float unknownExhaustionMaximum{20};
    bool strictThreshold{true};
};

struct RegenPreview {
    float lowHp{};
    float highHp{};
    bool assumedRule{};
};

[[nodiscard]] std::optional<RegenPreview>
previewRegeneration(MealPreview const&, RegenContext const&, RegenRules const& = {});

} // namespace melonrind
