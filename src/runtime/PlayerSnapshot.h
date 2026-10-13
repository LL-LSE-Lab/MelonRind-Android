#pragma once

#include <array>
#include <cstddef>
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
 * One icon origin as published by the game's HUD bar renderers.
 *
 * The units are whatever space the renderer laid the bar out in; the overlay is
 * drawn in launcher-view pixels, so the two are related by a per-device affine
 * transform that has to be measured on-device (see the calibration notes in the
 * README).  The values are stored raw and never scaled here.
 */
struct IconPosition {
    float x{};
    float y{};
    float z{};
};

/**
 * Bounded copy of both HUD bar icon layouts.
 *
 * `hungerCount` is the number of valid hunger slots and `heartCount` the number
 * copied from the game's heart vector; `heartTotal` keeps the real vector size
 * so a consumer can tell how many of the slots were absorption icons (which are
 * appended after the normal-health ones and must not be overlaid).
 */
struct IconGeometry {
    static constexpr std::size_t kMaxHungerIcons = 10;
    static constexpr std::size_t kMaxHeartIcons = 40;

    std::array<IconPosition, kMaxHungerIcons> hunger{};
    std::uint32_t hungerCount{};
    std::array<IconPosition, kMaxHeartIcons> heart{};
    std::uint32_t heartCount{};
    std::uint32_t heartTotal{};
};

/**
 * The `::UIControl` cached position of one HUD bar, in UI units.
 *
 * Both renderers translate their icon batch by this value before drawing, so it
 * is the origin the local `IconGeometry` offsets are relative to.
 */
struct HudBarOrigin {
    bool available{};
    float x{};
    float y{};
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
    /**
     * Icon origins of the vanilla hunger and health bars, captured in the same
     * render hooks that publish hunger and health.  Empty when unavailable.
     */
    IconGeometry icons;
    /** Owner origins those icon offsets are relative to. */
    HudBarOrigin hungerBar;
    HudBarOrigin heartBar;

    [[nodiscard]] bool hasFiniteNutrition() const noexcept;
    [[nodiscard]] bool hasFiniteHealth() const noexcept;
    [[nodiscard]] bool hasFood() const noexcept { return mainHandFood.has_value(); }
};

/** Sanitizes a copy before it crosses from a hook to the renderer. */
[[nodiscard]] PlayerSnapshot sanitizeSnapshot(PlayerSnapshot snapshot) noexcept;

} // namespace melonrind::runtime

