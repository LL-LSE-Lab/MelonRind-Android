#pragma once

#include "runtime/PlayerSnapshot.h"
#include "runtime/SignatureCatalog.h"

#include <cstdint>
#include <optional>

namespace melonrind::runtime {

/**
 * Opaque ARM64 bridge for the supplied Minecraft 1.26.51.1 client.
 *
 * Nothing in this type exposes a Bedrock object outside the render/network
 * hook that called it.  Every successful read is copied into POD/string
 * values before it reaches GameAdapter.
 */
class Bedrock12651Adapter {
public:
    explicit Bedrock12651Adapter(const SignatureSet &set,
                                 std::uintptr_t inventoryGetItemTarget = 0) noexcept
        : mSet(set), mInventoryGetItemTarget(inventoryGetItemTarget) {}

    [[nodiscard]] void *localPlayer(void *client) const noexcept;
    [[nodiscard]] void *localPlayerFromNetworkHandler(void *handler) const noexcept;
    [[nodiscard]] bool isInWorldWithoutMenu(void *client) const noexcept;
    [[nodiscard]] bool readHunger(void *renderer, float &value,
                                  bool &hudVisible) const noexcept;
    [[nodiscard]] bool readHealth(void *renderer, float &value,
                                  float &maximum) const noexcept;
    [[nodiscard]] std::optional<FoodSnapshot> readMainHandFood(
        void *player) const noexcept;

    /** Reads the packet payload after the game's handler has consumed it. */
    [[nodiscard]] std::optional<float> readSaturationPacket(
        void *packet, std::uint64_t &runtimeId, std::uint64_t &tick,
        std::string *probe = nullptr) const noexcept;

private:
    const SignatureSet &mSet;
    std::uintptr_t mInventoryGetItemTarget{};
};

} // namespace melonrind::runtime
