#include "runtime/PlayerSnapshot.h"

#include <algorithm>
#include <cmath>

namespace melonrind::runtime {
namespace {

bool finiteIn(float value, float low, float high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}

/**
 * Icon origins are only ever used for drawing, so a single implausible value
 * invalidates the whole group instead of producing a marker outside the screen.
 */
bool validIcon(const IconPosition &icon) noexcept {
    return finiteIn(icon.x, -100000.0f, 100000.0f) &&
           finiteIn(icon.y, -100000.0f, 100000.0f);
}

} // namespace

bool PlayerSnapshot::hasFiniteNutrition() const noexcept {
    return finiteIn(hunger, 0.0f, maxHunger) &&
           finiteIn(maxHunger, 1.0f, 20.0f) &&
           finiteIn(maxSaturation, 0.0f, 20.0f) &&
           (!saturation || finiteIn(*saturation, 0.0f, maxSaturation));
}

bool PlayerSnapshot::hasFiniteHealth() const noexcept {
    return healthAvailable && finiteIn(maxHealth, 1.0f, 2000.0f) &&
           finiteIn(health, 0.0f, maxHealth);
}

PlayerSnapshot sanitizeSnapshot(PlayerSnapshot snapshot) noexcept {
    if (!finiteIn(snapshot.maxHunger, 1.0f, 20.0f)) snapshot.maxHunger = 20.0f;
    if (!finiteIn(snapshot.hunger, 0.0f, snapshot.maxHunger)) {
        snapshot.hunger = std::clamp(snapshot.hunger, 0.0f, snapshot.maxHunger);
        if (!std::isfinite(snapshot.hunger)) snapshot.hunger = 0.0f;
    }

    if (!finiteIn(snapshot.maxSaturation, 0.0f, 20.0f)) snapshot.maxSaturation = 20.0f;
    if (snapshot.saturation &&
        !finiteIn(*snapshot.saturation, 0.0f, snapshot.maxSaturation)) {
        snapshot.saturation.reset();
    }

    if (!finiteIn(snapshot.maxHealth, 1.0f, 2000.0f)) snapshot.maxHealth = 20.0f;
    if (!finiteIn(snapshot.health, 0.0f, snapshot.maxHealth)) {
        snapshot.health = std::clamp(snapshot.health, 0.0f, snapshot.maxHealth);
        if (!std::isfinite(snapshot.health)) snapshot.health = 0.0f;
    }

    if (snapshot.mainHandFood) {
        auto &food = *snapshot.mainHandFood;
        if (food.nutrition < 0 || food.nutrition > 1000 ||
            !std::isfinite(food.saturationModifier) ||
            food.saturationModifier < 0.0f || food.saturationModifier > 1000.0f) {
            snapshot.mainHandFood.reset();
        }
    }
    if (!snapshot.alive || (snapshot.healthAvailable && snapshot.health <= 0.0f)) {
        snapshot.mainHandFood.reset();
    }
    if (!snapshot.healthAvailable) {
        snapshot.health = 0.0f;
        snapshot.maxHealth = 20.0f;
    }

    auto &icons = snapshot.icons;
    icons.hungerCount = std::min<std::uint32_t>(
        icons.hungerCount, IconGeometry::kMaxHungerIcons);
    icons.heartCount =
        std::min<std::uint32_t>(icons.heartCount, IconGeometry::kMaxHeartIcons);
    for (std::uint32_t i = 0; i < icons.hungerCount; ++i) {
        if (!validIcon(icons.hunger[i])) {
            icons.hungerCount = 0;
            break;
        }
    }
    for (std::uint32_t i = 0; i < icons.heartCount; ++i) {
        if (!validIcon(icons.heart[i])) {
            icons.heartCount = 0;
            break;
        }
    }
    if (icons.heartCount == 0) icons.heartTotal = 0;

    auto const sanitizeOrigin = [](HudBarOrigin &origin) {
        if (!origin.available) return;
        if (!finiteIn(origin.x, -100000.0f, 100000.0f) ||
            !finiteIn(origin.y, -100000.0f, 100000.0f)) {
            origin = HudBarOrigin{};
        }
    };
    sanitizeOrigin(snapshot.hungerBar);
    sanitizeOrigin(snapshot.heartBar);
    return snapshot;
}

} // namespace melonrind::runtime

