#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace melonrind {

// Client attribute packets are deltas. Keep only an explicitly received S value,
// scoped to the local player object and runtime ID. Callers provide synchronization.
class SaturationObservation {
    std::uintptr_t player{};
    std::uint64_t runtimeId{};
    std::optional<std::uint64_t> tick;
    std::optional<float> saturation;
public:
    void clear() { *this = {}; }

    bool receive(std::uintptr_t identity, std::uint64_t id, std::uint64_t packetTick,
                 std::optional<float> value) {
        if (!identity) return false;
        if (player != identity || runtimeId != id) {
            clear();
            player = identity;
            runtimeId = id;
        }
        if (tick && packetTick < *tick) return false;
        tick = packetTick;
        if (value) {
            if (std::isfinite(*value) && *value >= 0 && *value <= 20) saturation = value;
            else saturation.reset();
        }
        return true;
    }

    [[nodiscard]] std::optional<float> get(std::uintptr_t identity, std::uint64_t id) const {
        return player == identity && runtimeId == id ? saturation : std::nullopt;
    }

    void forget(std::uintptr_t identity) {
        if (player == identity) clear();
    }
};
} // namespace melonrind
