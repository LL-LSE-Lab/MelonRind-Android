#include "runtime/GameAdapter.h"

#include <pl/memory/Signature.hpp>
#include <pl/memory/Vtable.hpp>

#include <cmath>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace melonrind::runtime {

namespace {

using RenderFn = void (*)(void *, void *, void *, void *, int);
RenderFn gOriginalHungerRender{};
RenderFn gOriginalHeartRender{};
GameAdapter *gActiveAdapter{};

void publishHungerFrame(GameAdapter &adapter, void *client, void *renderer) {
    if (!client || !renderer) return;

    // 1.26.51.1's ARM64 layout follows the generated LeviLamina client
    // header: the renderer base occupies 0x0c bytes, then mTickCount,
    // booleans, mNumFullIcons at +0x18 and mNumHalfIcons at +0x20.
    auto const full = *reinterpret_cast<std::uint64_t *>(
        static_cast<std::byte *>(renderer) + 0x18);
    auto const half = *reinterpret_cast<std::uint64_t *>(
        static_cast<std::byte *>(renderer) + 0x20);
    if (full > 10 || half > 1) return;

    void *player{};
    auto vtable = *reinterpret_cast<void ***>(client);
    if (vtable && vtable[32]) {
        using GetLocalPlayerFn = void *(*)(void *);
        player = reinterpret_cast<GetLocalPlayerFn>(vtable[32])(client);
    }
    if (!player) return;

    PlayerSnapshot snapshot;
    snapshot.playerIdentity = reinterpret_cast<std::uintptr_t>(player);
    snapshot.runtimeId = snapshot.playerIdentity;
    snapshot.tick = adapter.nextFrame();
    snapshot.hunger = static_cast<float>(full * 2 + half);
    snapshot.maxHunger = 20.0f;
    snapshot.health = 20.0f;
    snapshot.maxHealth = 20.0f;
    snapshot.alive = true;
    snapshot.hudVisible = true;
    adapter.publishSnapshot(std::move(snapshot));
}

void hungerRenderHook(void *renderer, void *context, void *client, void *owner,
                      int pass) {
    if (gOriginalHungerRender) {
        gOriginalHungerRender(renderer, context, client, owner, pass);
    }
    if (gActiveAdapter) publishHungerFrame(*gActiveAdapter, client, renderer);
}

void heartRenderHook(void *renderer, void *context, void *client, void *owner,
                     int pass) {
    if (gOriginalHeartRender) {
        gOriginalHeartRender(renderer, context, client, owner, pass);
    }
}

} // namespace

bool GameAdapter::install(std::string_view minecraftVersion) {
    std::unique_lock lock(mMutex);
    mInstalled = false;
    mNativeReadsAvailable = false;
    mMinecraftVersion.clear();
    mSignatureSet = nullptr;
    mSnapshot.reset();

    auto *set = signatureSetForVersion(minecraftVersion);
    if (!set) return false;

    mMinecraftVersion = std::string(minecraftVersion);
    mSignatureSet = set;

    // Resolve all configured patterns together.  The checked-in development
    // catalog is intentionally empty, so this remains false until a pattern
    // set has been validated against the exact shipped ARM64 binary.  A
    // future adapter must install typed hooks only after this complete check.
    std::vector<std::string> patterns;
    for (auto value : {set->frame, set->playerState, set->hunger,
                       set->health, set->mainHandFood, set->attributeUpdate}) {
        if (!value.empty()) patterns.emplace_back(value);
    }
    if (patterns.size() == 6) {
        auto resolved = pl::memory::resolveSignatures(patterns, set->moduleName);
        mNativeReadsAvailable = resolved.size() == patterns.size();
        for (const auto &pattern : patterns) {
            auto it = resolved.find(pattern);
            if (it == resolved.end() || it->second == 0) {
                mNativeReadsAvailable = false;
                break;
            }
        }
    } else {
        mNativeReadsAvailable = false;
    }

    // The 1.26.51.1 APK supplied for this build was inspected directly. Its
    // RTTI-backed renderer vtables expose the render implementation at slot
    // 17. This hook only publishes the vanilla hunger count; health, food and
    // saturation stay unavailable until their own ABI contracts are verified.
    if (set->hungerRenderSlot != 0 && !set->hungerRendererType.empty()) {
        auto hunger = pl::memory::resolveVtableFunction(
            set->hungerRendererType, set->hungerRenderSlot, set->moduleName);
        auto heart = pl::memory::resolveVtableFunction(
            set->heartRendererType, set->heartRenderSlot, set->moduleName);
        mHungerRenderTarget = hunger;
        mHeartRenderTarget = heart;
        if (hunger) {
            gActiveAdapter = this;
            mHungerRenderHook = pl::memory::HookHandle(
                reinterpret_cast<void *>(hunger),
                reinterpret_cast<void *>(&hungerRenderHook),
                reinterpret_cast<void **>(&gOriginalHungerRender));
        }
        if (heart) {
            mHeartRenderHook = pl::memory::HookHandle(
                reinterpret_cast<void *>(heart),
                reinterpret_cast<void *>(&heartRenderHook),
                reinterpret_cast<void **>(&gOriginalHeartRender));
        }
        mNativeReadsAvailable = mHungerRenderHook.installed();
    }
    mInstalled = true;
    return true;
}

void GameAdapter::uninstall() noexcept {
    std::unique_lock lock(mMutex);
    mInstalled = false;
    mNativeReadsAvailable = false;
    mMinecraftVersion.clear();
    mSignatureSet = nullptr;
    mSnapshot.reset();
    mSnapshotCallback = {};
    mHungerRenderTarget = 0;
    mHeartRenderTarget = 0;
    mHungerRenderHook.reset();
    mHeartRenderHook.reset();
    if (gActiveAdapter == this) gActiveAdapter = nullptr;
    gOriginalHungerRender = nullptr;
    gOriginalHeartRender = nullptr;
}

bool GameAdapter::installed() const noexcept {
    std::shared_lock lock(mMutex);
    return mInstalled;
}

bool GameAdapter::nativeReadsAvailable() const noexcept {
    std::shared_lock lock(mMutex);
    return mNativeReadsAvailable;
}

std::uintptr_t GameAdapter::hungerRenderTarget() const noexcept {
    std::shared_lock lock(mMutex);
    return mHungerRenderTarget;
}

std::uintptr_t GameAdapter::heartRenderTarget() const noexcept {
    std::shared_lock lock(mMutex);
    return mHeartRenderTarget;
}

bool GameAdapter::hungerHookInstalled() const noexcept {
    std::shared_lock lock(mMutex);
    return mHungerRenderHook.installed();
}

bool GameAdapter::heartHookInstalled() const noexcept {
    std::shared_lock lock(mMutex);
    return mHeartRenderHook.installed();
}

std::string GameAdapter::minecraftVersion() const {
    std::shared_lock lock(mMutex);
    return mMinecraftVersion;
}

const SignatureSet *GameAdapter::signatureSet() const noexcept {
    std::shared_lock lock(mMutex);
    return mSignatureSet;
}

std::optional<PlayerSnapshot> GameAdapter::sample() const {
    std::shared_lock lock(mMutex);
    if (!mInstalled || !mSnapshot) return std::nullopt;
    return *mSnapshot;
}

void GameAdapter::publishSnapshot(PlayerSnapshot snapshot) {
    snapshot = sanitizeSnapshot(std::move(snapshot));
    std::function<void(const PlayerSnapshot &)> callback;
    {
        std::unique_lock lock(mMutex);
        if (!mInstalled) return;
        if (mSnapshot && snapshot.playerIdentity == mSnapshot->playerIdentity &&
            snapshot.runtimeId == mSnapshot->runtimeId &&
            snapshot.tick < mSnapshot->tick) {
            return;
        }
        mSnapshot = snapshot;
        callback = mSnapshotCallback;
    }
    if (callback) callback(snapshot);
}

void GameAdapter::setSnapshotCallback(
    std::function<void(const PlayerSnapshot &)> callback) {
    std::unique_lock lock(mMutex);
    mSnapshotCallback = std::move(callback);
}

void GameAdapter::publishSaturation(std::uintptr_t playerIdentity,
                                    std::uint64_t runtimeId,
                                    std::uint64_t tick,
                                    std::optional<float> saturation) {
    if (saturation && (!std::isfinite(*saturation) || *saturation < 0.0f ||
                       *saturation > 20.0f)) {
        saturation.reset();
    }
    std::function<void(const PlayerSnapshot &)> callback;
    std::optional<PlayerSnapshot> copy;
    {
        std::unique_lock lock(mMutex);
        if (!mInstalled || !mSnapshot) return;
        if (mSnapshot->playerIdentity != playerIdentity ||
            mSnapshot->runtimeId != runtimeId) {
            return;
        }
        if (tick < mSnapshot->tick) return;
        mSnapshot->tick = tick;
        mSnapshot->saturation = saturation;
        callback = mSnapshotCallback;
        copy = mSnapshot;
    }
    if (callback && copy) callback(*copy);
}

void GameAdapter::resetSession() noexcept {
    std::unique_lock lock(mMutex);
    mSnapshot.reset();
}

} // namespace melonrind::runtime

