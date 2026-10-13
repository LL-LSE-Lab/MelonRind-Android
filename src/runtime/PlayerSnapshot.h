#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace melonrind::runtime {

enum class RuleState : std::uint8_t {
    Unknown,
    Enabled,
    Disabled,
};

/** Values read from the selected food item in the local player's hand. */
struct FoodSnapshot {
    int nutrition{};
    float saturationModifier{};
    bool alwaysEat{};
    bool uncertainEffects{};
    std::string itemId;
};

/**
 * A frame-safe copy of the local player state.  No Bedrock object pointers are
 * retained here: hooks copy primitive values into this structure and the HUD
 * consumes the copy on the render thread.
 */
struct PlayerSnapshot {
    std::uintptr_t playerIdentity{};
    std::uint64_t runtimeId{};
    std::uint64_t tick{};

    float hunger{};
    float maxHunger{20.0f};
    std::optional<float> saturation;
    float maxSaturation{20.0f};

    float health{};
    float maxHealth{20.0f};
    /** True only when the native adapter read both health values. */
    bool healthAvailable{};

    std::optional<FoodSnapshot> mainHandFood;
    RuleState naturalRegeneration{RuleState::Unknown};
    bool peaceful{};
    bool regenerationSuppressed{};
    bool alive{true};
    bool creative{};
    bool spectator{};
    bool hudVisible{true};
    /** Native player mode was read and allows the HUD to be shown. */
    bool modeAvailable{};
    std::string itemName;

    [[nodiscard]] bool hasFiniteNutrition() const noexcept;
    [[nodiscard]] bool hasFiniteHealth() const noexcept;
    [[nodiscard]] bool hasFood() const noexcept { return mainHandFood.has_value(); }
};

/** Sanitizes a copy before it crosses from a hook to the renderer. */
[[nodiscard]] PlayerSnapshot sanitizeSnapshot(PlayerSnapshot snapshot) noexcept;

} // namespace melonrind::runtime

