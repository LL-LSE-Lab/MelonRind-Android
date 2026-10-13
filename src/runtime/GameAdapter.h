#pragma once

#include "runtime/PlayerSnapshot.h"
#include "runtime/SignatureCatalog.h"

#include <cstdint>
#include <functional>
#include <atomic>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <pl/memory/Hook.hpp>

namespace melonrind::runtime {

/**
 * Boundary between ARM64 Bedrock hooks and the UI/model code.
 *
 * A future signature adapter publishes copies through publishSnapshot() and
 * publishSaturation().  Nothing outside this class should retain a pointer to
 * a Bedrock object or execute game code on the render thread.
 */
class GameAdapter {
public:
    [[nodiscard]] bool install(std::string_view minecraftVersion);
    void uninstall() noexcept;

    [[nodiscard]] bool installed() const noexcept;
    [[nodiscard]] bool nativeReadsAvailable() const noexcept;
    [[nodiscard]] std::uintptr_t hungerRenderTarget() const noexcept;
    [[nodiscard]] std::uintptr_t heartRenderTarget() const noexcept;
    [[nodiscard]] bool hungerHookInstalled() const noexcept;
    [[nodiscard]] bool heartHookInstalled() const noexcept;
    [[nodiscard]] bool attributeHookInstalled() const noexcept;
    [[nodiscard]] std::uintptr_t attributeHookTarget() const noexcept;
    [[nodiscard]] std::uintptr_t inventoryGetItemTarget() const noexcept;
    [[nodiscard]] std::string minecraftVersion() const;
    [[nodiscard]] const SignatureSet *signatureSet() const noexcept;

    /** Returns a sanitized copy of the most recently published state. */
    [[nodiscard]] std::optional<PlayerSnapshot> sample() const;

    /** Hook bridge: replace the complete local-player state for this frame. */
    void publishSnapshot(PlayerSnapshot snapshot);

    /** Merge the health row sampled from HudHeartRenderer. */
    void publishHealth(std::uintptr_t playerIdentity, float health,
                       float maxHealth, bool hudVisible);

    /**
     * Parse a validated attribute packet at the native boundary.
     *
     * @param knownPacketId Skip the virtual id lookup when the caller already
     *        knows it (required when called from the packet's own id hook).
     */
    void observeAttributePacket(void *handler, void *packet,
                                std::uint32_t knownPacketId = 0);

    /**
     * Diagnostics: how many calls the attribute hook received and the packet id
     * the last one reported.  MinecraftPacketIds::UpdateAttributes is 29, so a
     * different value proves the catalog slot points at another packet.
     */
    [[nodiscard]] std::uint64_t attributeHookCalls() const noexcept;
    [[nodiscard]] std::uint32_t lastAttributePacketId() const noexcept;
    [[nodiscard]] std::string lastAttributeProbe() const;
    [[nodiscard]] std::uintptr_t attributePacketDtorTarget() const noexcept;
    [[nodiscard]] std::uintptr_t attributePacketDeletingDtorTarget() const noexcept;
    [[nodiscard]] bool attributePacketDtorHookInstalled() const noexcept;

    /** Render sink used by a version-specific frame adapter. */
    void setSnapshotCallback(std::function<void(const PlayerSnapshot &)> callback);

    [[nodiscard]] std::uint64_t nextFrame() noexcept { return ++mFrameCounter; }

    /** Hook bridge for client attribute packets carrying saturation deltas. */
    void publishSaturation(std::uintptr_t playerIdentity,
                           std::uint64_t runtimeId, std::uint64_t tick,
                           std::optional<float> saturation);

    /** Clear state on world transition, death, or local-player replacement. */
    void resetSession() noexcept;

private:
    mutable std::shared_mutex mMutex;
    bool mInstalled{};
    bool mNativeReadsAvailable{};
    std::string mMinecraftVersion;
    const SignatureSet *mSignatureSet{};
    std::optional<PlayerSnapshot> mSnapshot;
    std::function<void(const PlayerSnapshot &)> mSnapshotCallback;
    pl::memory::HookHandle mHungerRenderHook;
    pl::memory::HookHandle mHeartRenderHook;
    pl::memory::HookHandle mAttributeHook;
    pl::memory::HookHandle mAttributePacketDtorHook;
    pl::memory::HookHandle mAttributePacketDeletingDtorHook;
    std::uintptr_t mHungerRenderTarget{};
    std::uintptr_t mHeartRenderTarget{};
    std::uintptr_t mAttributeHookTarget{};
    std::uintptr_t mInventoryGetItemTarget{};
    std::uintptr_t mAttributePacketDtorTarget{};
    std::uintptr_t mAttributePacketDeletingDtorTarget{};
    std::uint64_t mFrameCounter{};
    std::optional<std::uint64_t> mLastAttributeTick;
    std::uint64_t mAttributeRuntimeId{};
    std::optional<float> mPendingSaturation;
    std::uint64_t mPendingSaturationRuntimeId{};
    std::uint64_t mPendingSaturationTick{};
    std::uint64_t mAttributeHookCalls{};
    std::uint32_t mLastAttributePacketId{};
    std::string mLastAttributeProbe;
};

} // namespace melonrind::runtime

