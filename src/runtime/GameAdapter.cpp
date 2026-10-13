#include "runtime/GameAdapter.h"

#include "runtime/Bedrock12651Adapter.h"

#include <pl/memory/Hook.hpp>
#include <pl/memory/Vtable.hpp>

#include <cmath>
#include <cstddef>
#include <mutex>
#include <string>

namespace melonrind::runtime {
namespace {

/** MinecraftPacketIds::UpdateAttributes. */
constexpr int kAttributePacketId = 29;

using RenderFn = void (*)(void *, void *, void *, void *, int);

struct SharedPtrAbi {
    void *pointer{};
    void *control{};
};
using AttributeHandlerFn = void (*)(void *, void *, SharedPtrAbi);
/** `Packet::~Packet()` — one pointer in, no hidden result register. */
using PacketDtorFn = void (*)(void *);

RenderFn gOriginalHungerRender{};
RenderFn gOriginalHeartRender{};
AttributeHandlerFn gOriginalAttributeHandler{};
PacketDtorFn gOriginalPacketCompleteDtor{};
PacketDtorFn gOriginalPacketDeletingDtor{};
GameAdapter *gActiveAdapter{};

void publishHungerFrame(GameAdapter &adapter, void *client, void *renderer) {
    auto const *set = adapter.signatureSet();
    if (!set || !client || !renderer) return;
    Bedrock12651Adapter native(*set, adapter.inventoryGetItemTarget());

    float hunger{};
    bool hudVisible{};
    if (!native.readHunger(renderer, hunger, hudVisible)) return;
    auto *player = native.localPlayer(client);
    if (!player) {
        adapter.resetSession();
        return;
    }

    PlayerSnapshot snapshot;
    snapshot.playerIdentity = reinterpret_cast<std::uintptr_t>(player);
    snapshot.runtimeId = 0;
    snapshot.tick = adapter.nextFrame();
    snapshot.hunger = hunger;
    snapshot.maxHunger = 20.0f;
    snapshot.alive = true;
    snapshot.hudVisible = hudVisible;
    snapshot.modeAvailable = true;
    snapshot.mainHandFood = native.readMainHandFood(player);
    adapter.publishSnapshot(std::move(snapshot));
}

void publishHeartFrame(GameAdapter &adapter, void *client, void *renderer) {
    auto const *set = adapter.signatureSet();
    if (!set || !client || !renderer) return;
    Bedrock12651Adapter native(*set, adapter.inventoryGetItemTarget());
    float health{};
    float maximum{};
    if (!native.readHealth(renderer, health, maximum)) return;
    auto *player = native.localPlayer(client);
    if (!player) return;
    adapter.publishHealth(reinterpret_cast<std::uintptr_t>(player), health,
                          maximum, true);
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
    if (gActiveAdapter) publishHeartFrame(*gActiveAdapter, client, renderer);
}

void attributeHandlerHook(void *handler, void *network, SharedPtrAbi packet) {
    // Parse while the by-value shared_ptr argument still owns the packet. The
    // native handler is called with the exact same two-word ABI value.
    if (gActiveAdapter && packet.pointer) {
        gActiveAdapter->observeAttributePacket(handler, packet.pointer);
    }
    if (gOriginalAttributeHandler) {
        gOriginalAttributeHandler(handler, network, packet);
    }
}

/**
 * Saturation is carried by UpdateAttributesPacket.  Hook its destructors: the
 * payload is still intact when a destructor is entered, the signature is a
 * trivial `void(void *)` (no hidden result register has to survive the detour)
 * and destructors are large enough to patch safely — unlike the packet's own
 * two-instruction `getId()`, where an inline patch runs into the next function.
 *
 * Both entries are hooked: `std::make_shared` calls the complete-object
 * destructor directly, while `delete` goes through the deleting destructor
 * stored in the vtable.
 */
void packetCompleteDtorHook(void *packet) {
    if (gActiveAdapter && packet) {
        gActiveAdapter->observeAttributePacket(nullptr, packet,
                                               kAttributePacketId);
    }
    if (gOriginalPacketCompleteDtor) gOriginalPacketCompleteDtor(packet);
}

void packetDeletingDtorHook(void *packet) {
    if (gActiveAdapter && packet) {
        gActiveAdapter->observeAttributePacket(nullptr, packet,
                                               kAttributePacketId);
    }
    if (gOriginalPacketDeletingDtor) gOriginalPacketDeletingDtor(packet);
}

} // namespace

bool GameAdapter::install(std::string_view minecraftVersion) {
    std::unique_lock lock(mMutex);
    mInstalled = false;
    mNativeReadsAvailable = false;
    mMinecraftVersion.clear();
    mSignatureSet = nullptr;
    mSnapshot.reset();
    mLastAttributeTick.reset();
    mAttributeRuntimeId = 0;
    mPendingSaturation.reset();
    mPendingSaturationRuntimeId = 0;
    mPendingSaturationTick = 0;
    mHungerRenderTarget = 0;
    mHeartRenderTarget = 0;
    mAttributeHookTarget = 0;
    mInventoryGetItemTarget = 0;
    mAttributePacketDtorTarget = 0;
    mAttributePacketDeletingDtorTarget = 0;
    mAttributeHookCalls = 0;
    mLastAttributePacketId = 0;

    auto *set = signatureSetForVersion(minecraftVersion);
    if (!set || set->minecraftVersion != "1.26.51" ||
        set->heartBackgroundCountOffset == 0) {
        return false;
    }
    mMinecraftVersion = std::string(minecraftVersion);
    mSignatureSet = set;

    auto hunger = pl::memory::resolveVtableFunction(
        set->hungerRendererType, set->hungerRenderSlot, set->moduleName);
    auto heart = pl::memory::resolveVtableFunction(
        set->heartRendererType, set->heartRenderSlot, set->moduleName);
    auto attributes = pl::memory::resolveVtableFunction(
        set->networkHandlerType, set->attributeHandlerSlot, set->moduleName);
    auto inventoryGetItem = pl::memory::resolveVtableFunction(
        "9Inventory", set->inventoryGetItemSlot, set->moduleName);
    // The packet destructors are reached by fixed offset from the module base
    // instead of through the RTTI resolver: for this type the resolver matched
    // more than one candidate table and returned a slot one entry too far,
    // which hooked an unrelated (and much smaller) function.
    const auto moduleBase =
        hunger ? static_cast<std::uintptr_t>(hunger - set->hungerRenderRva) : 0;
    const auto packetCompleteDtor =
        moduleBase ? moduleBase + set->attributePacketCompleteDtorRva : 0;
    const auto packetDeletingDtor =
        moduleBase ? moduleBase + set->attributePacketDeletingDtorRva : 0;
    mHungerRenderTarget = hunger;
    mHeartRenderTarget = heart;
    mAttributeHookTarget = attributes;
    mInventoryGetItemTarget = inventoryGetItem;
    mAttributePacketDtorTarget = packetCompleteDtor;
    mAttributePacketDeletingDtorTarget = packetDeletingDtor;

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
    if (attributes) {
        mAttributeHook = pl::memory::HookHandle(
            reinterpret_cast<void *>(attributes),
            reinterpret_cast<void *>(&attributeHandlerHook),
            reinterpret_cast<void **>(&gOriginalAttributeHandler));
    }
    if (packetCompleteDtor) {
        gActiveAdapter = this;
        mAttributePacketDtorHook = pl::memory::HookHandle(
            reinterpret_cast<void *>(packetCompleteDtor),
            reinterpret_cast<void *>(&packetCompleteDtorHook),
            reinterpret_cast<void **>(&gOriginalPacketCompleteDtor));
    }
    if (packetDeletingDtor) {
        gActiveAdapter = this;
        mAttributePacketDeletingDtorHook = pl::memory::HookHandle(
            reinterpret_cast<void *>(packetDeletingDtor),
            reinterpret_cast<void *>(&packetDeletingDtorHook),
            reinterpret_cast<void **>(&gOriginalPacketDeletingDtor));
    }
    mNativeReadsAvailable = mHungerRenderHook.installed();
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
    mLastAttributeTick.reset();
    mAttributeRuntimeId = 0;
    mPendingSaturation.reset();
    mPendingSaturationRuntimeId = 0;
    mPendingSaturationTick = 0;
    mSnapshotCallback = {};
    mHungerRenderTarget = 0;
    mHeartRenderTarget = 0;
    mAttributeHookTarget = 0;
    mInventoryGetItemTarget = 0;
    mAttributePacketDtorTarget = 0;
    mAttributePacketDeletingDtorTarget = 0;
    mAttributeHook.reset();
    mHungerRenderHook.reset();
    mHeartRenderHook.reset();
    mAttributePacketDtorHook.reset();
    mAttributePacketDeletingDtorHook.reset();
    if (gActiveAdapter == this) gActiveAdapter = nullptr;
    gOriginalHungerRender = nullptr;
    gOriginalHeartRender = nullptr;
    gOriginalAttributeHandler = nullptr;
    gOriginalPacketCompleteDtor = nullptr;
    gOriginalPacketDeletingDtor = nullptr;
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

std::uintptr_t GameAdapter::attributeHookTarget() const noexcept {
    std::shared_lock lock(mMutex);
    return mAttributeHookTarget;
}

std::uintptr_t GameAdapter::inventoryGetItemTarget() const noexcept {
    std::shared_lock lock(mMutex);
    return mInventoryGetItemTarget;
}

bool GameAdapter::hungerHookInstalled() const noexcept {
    std::shared_lock lock(mMutex);
    return mHungerRenderHook.installed();
}

bool GameAdapter::heartHookInstalled() const noexcept {
    std::shared_lock lock(mMutex);
    return mHeartRenderHook.installed();
}

bool GameAdapter::attributeHookInstalled() const noexcept {
    std::shared_lock lock(mMutex);
    return mAttributeHook.installed();
}

std::uint64_t GameAdapter::attributeHookCalls() const noexcept {
    std::shared_lock lock(mMutex);
    return mAttributeHookCalls;
}

std::uint32_t GameAdapter::lastAttributePacketId() const noexcept {
    std::shared_lock lock(mMutex);
    return mLastAttributePacketId;
}

std::uintptr_t GameAdapter::attributePacketDtorTarget() const noexcept {
    std::shared_lock lock(mMutex);
    return mAttributePacketDtorTarget;
}

std::uintptr_t GameAdapter::attributePacketDeletingDtorTarget() const noexcept {
    std::shared_lock lock(mMutex);
    return mAttributePacketDeletingDtorTarget;
}

std::string GameAdapter::lastAttributeProbe() const {
    std::shared_lock lock(mMutex);
    return mLastAttributeProbe;
}

bool GameAdapter::attributePacketDtorHookInstalled() const noexcept {
    std::shared_lock lock(mMutex);
    return mAttributePacketDtorHook.installed();
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
    std::function<void(const PlayerSnapshot &)> callback;
    {
        std::unique_lock lock(mMutex);
        if (!mInstalled) return;
        if (mSnapshot && snapshot.playerIdentity == mSnapshot->playerIdentity) {
            if (snapshot.runtimeId == 0) snapshot.runtimeId = mSnapshot->runtimeId;
            if (!snapshot.saturation) snapshot.saturation = mSnapshot->saturation;
            if (!snapshot.healthAvailable && mSnapshot->healthAvailable) {
                snapshot.health = mSnapshot->health;
                snapshot.maxHealth = mSnapshot->maxHealth;
                snapshot.healthAvailable = true;
            }
        }
        if (!snapshot.saturation && mPendingSaturation) {
            snapshot.saturation = mPendingSaturation;
            snapshot.runtimeId = mPendingSaturationRuntimeId;
            mLastAttributeTick = mPendingSaturationTick;
            mAttributeRuntimeId = mPendingSaturationRuntimeId;
            mPendingSaturation.reset();
        }
        snapshot = sanitizeSnapshot(std::move(snapshot));
        mSnapshot = snapshot;
        callback = mSnapshotCallback;
    }
    if (callback) callback(snapshot);
}

void GameAdapter::publishHealth(std::uintptr_t playerIdentity, float health,
                                float maxHealth, bool hudVisible) {
    std::function<void(const PlayerSnapshot &)> callback;
    std::optional<PlayerSnapshot> copy;
    {
        std::unique_lock lock(mMutex);
        if (!mInstalled || !mSnapshot ||
            mSnapshot->playerIdentity != playerIdentity) return;
        mSnapshot->health = health;
        mSnapshot->maxHealth = maxHealth;
        mSnapshot->healthAvailable = std::isfinite(health) &&
            std::isfinite(maxHealth) && maxHealth > 0.0f && health >= 0.0f &&
            health <= maxHealth;
        mSnapshot->hudVisible = hudVisible;
        mSnapshot->tick = ++mFrameCounter;
        mSnapshot = sanitizeSnapshot(*mSnapshot);
        copy = mSnapshot;
        callback = mSnapshotCallback;
    }
    if (callback && copy) callback(*copy);
}

void GameAdapter::setSnapshotCallback(
    std::function<void(const PlayerSnapshot &)> callback) {
    std::unique_lock lock(mMutex);
    mSnapshotCallback = std::move(callback);
}

void GameAdapter::observeAttributePacket(void *handler, void *packet,
                                         std::uint32_t knownPacketId) {
    if (packet) {
        // Record which packet the hooked slot actually dispatches.  The vtable
        // lookup is only safe when the id is not already known: when this runs
        // from the packet's own getId detour, calling it again would recurse.
        std::unique_lock lock(mMutex);
        ++mAttributeHookCalls;
        if (knownPacketId != 0) {
            mLastAttributePacketId = knownPacketId;
        } else if (auto table = *reinterpret_cast<void *const **>(packet)) {
            using GetId = int (*)(void const *);
            if (table[2]) {
                mLastAttributePacketId = static_cast<std::uint32_t>(
                    reinterpret_cast<GetId>(table[2])(packet));
            }
        }
    }
    const SignatureSet *set{};
    std::uintptr_t identity{};
    {
        std::shared_lock lock(mMutex);
        if (!mInstalled || !mSignatureSet) return;
        set = mSignatureSet;
        if (mSnapshot) identity = mSnapshot->playerIdentity;
    }
    Bedrock12651Adapter native(*set);
    std::uint64_t runtimeId{};
    std::uint64_t tick{};
    std::string probeText;
            auto saturation =
                native.readSaturationPacket(packet, runtimeId, tick, &probeText);
            {
                std::unique_lock lock(mMutex);
                mLastAttributeProbe = probeText;
            }
    if (!saturation) return;
    if (auto *player = native.localPlayerFromNetworkHandler(handler)) {
        auto packetIdentity = reinterpret_cast<std::uintptr_t>(player);
        if (identity && packetIdentity != identity) return;
        identity = packetIdentity;
    }
    if (!identity) {
        std::unique_lock lock(mMutex);
        if (mPendingSaturationTick != 0 && tick < mPendingSaturationTick) return;
        mPendingSaturation = saturation;
        mPendingSaturationRuntimeId = runtimeId;
        mPendingSaturationTick = tick;
        return;
    }
    publishSaturation(identity, runtimeId, tick, saturation);
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
        if (!mInstalled || !mSnapshot ||
            mSnapshot->playerIdentity != playerIdentity) return;
        if (mSnapshot->runtimeId != 0 && runtimeId != 0 &&
            mSnapshot->runtimeId != runtimeId) return;
        if (mAttributeRuntimeId != 0 && runtimeId != 0 &&
            mAttributeRuntimeId != runtimeId) {
            mLastAttributeTick.reset();
        }
        if (mLastAttributeTick && tick < *mLastAttributeTick) return;
        mAttributeRuntimeId = runtimeId;
        mLastAttributeTick = tick;
        mSnapshot->runtimeId = runtimeId;
        mSnapshot->saturation = saturation;
        copy = mSnapshot;
        callback = mSnapshotCallback;
    }
    if (callback && copy) callback(*copy);
}

void GameAdapter::resetSession() noexcept {
    std::unique_lock lock(mMutex);
    mSnapshot.reset();
    mLastAttributeTick.reset();
    mAttributeRuntimeId = 0;
    mPendingSaturation.reset();
    mPendingSaturationRuntimeId = 0;
    mPendingSaturationTick = 0;
}

} // namespace melonrind::runtime

