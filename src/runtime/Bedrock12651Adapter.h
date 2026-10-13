#pragma once

#include "runtime/PlayerSnapshot.h"
#include "runtime/SignatureCatalog.h"

#include <cstdint>
#include <optional>
#include <string>

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
    /**
     * Copies the hunger bar icon origins out of the renderer.
     *
     * The game builds `::HudHungerRenderer::mIconPosition` before it draws, so
     * the values are the ones the vanilla bar was just laid out with.  Returns
     * the number of icons copied, or zero when the array is not readable or any
     * slot is implausible.
     */
    [[nodiscard]] std::uint32_t readHungerIcons(
        void *renderer, IconPosition *out, std::uint32_t capacity) const noexcept;
    /**
     * Copies the health bar icon origins out of the renderer's vector.
     *
     * @param total Receives the vector's real element count, which also covers
     *        the absorption icons appended after the normal-health ones.
     */
    [[nodiscard]] std::uint32_t readHeartIcons(
        void *renderer, IconPosition *out, std::uint32_t capacity,
        std::uint32_t *total) const noexcept;

    /**
     * Reads `::UIControl::mCachedPosition`, the `glm::vec2` at `owner + 0x10`
     * that both HUD render entry points translate their icon batch by.  The
     * value is in UI units; the renderer refreshes it lazily, and the render
     * hook reads it after the original call, so it belongs to this frame.
     */
    [[nodiscard]] bool readHudOwnerOrigin(void *owner, float &x,
                                          float &y) const noexcept;

    /**
     * Diagnostic text for one HUD bar's screen-space transform.
     *
     * `owner` is the `::UIControl` the renderer belongs to and `context` the
     * `::MinecraftUIRenderContext` of the draw call.  The render entry points
     * translate the icon batch by `owner + 0x10` (a `glm::vec2` cached position
     * in UI units, refreshed through the getter at `0x968bc9c`) and then draw
     * the local `mIconPosition` offsets through the context's transform, so
     * both the cached position and the transform scale are recorded here.
     */
    [[nodiscard]] std::string describeHudTransform(const char *label, void *owner,
                                                  void *context) const noexcept;
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
