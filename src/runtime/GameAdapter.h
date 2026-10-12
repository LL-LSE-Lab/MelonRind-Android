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
    [[nodiscard]] std::string minecraftVersion() const;
    [[nodiscard]] const SignatureSet *signatureSet() const noexcept;

    /** Returns a sanitized copy of the most recently published state. */
    [[nodiscard]] std::optional<PlayerSnapshot> sample() const;

    /** Hook bridge: replace the complete local-player state for this frame. */
    void publishSnapshot(PlayerSnapshot snapshot);

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
    std::uintptr_t mHungerRenderTarget{};
    std::uintptr_t mHeartRenderTarget{};
    std::uint64_t mFrameCounter{};
};

} // namespace melonrind::runtime

