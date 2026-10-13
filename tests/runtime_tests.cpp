#include "runtime/Bedrock12651Adapter.h"
#include "runtime/PlayerSnapshot.h"
#include "runtime/SignatureCatalog.h"

#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>

#include <string_view>

namespace {
/** libc++ std::vector layout: begin, end, capacity. */
struct VectorAbi {
    std::byte *begin;
    std::byte *end;
    std::byte *capacity;
};

void check(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

/**
 * Writes a libc++ std::string image (default ABI, little endian) into a buffer:
 * the short form keeps the payload inline at +1, the long form stores the size
 * at +8 and the heap pointer at +0x10 behind an odd discrimination byte.
 */
void writeGameString(std::byte *destination, std::string_view text) {
    if (text.size() < 23) {
        destination[0] = static_cast<std::byte>(text.size() << 1);
        std::memcpy(destination + 1, text.data(), text.size());
        return;
    }
    destination[0] = std::byte{1};
    auto size = static_cast<std::uint64_t>(text.size());
    std::memcpy(destination + 8, &size, sizeof(size));
    auto const *data = text.data();
    std::memcpy(destination + 0x10, &data, sizeof(data));
}
}

int main() {
    using namespace melonrind::runtime;

    auto *primary = signatureSetForVersion("1.26.51.1");
    check(primary && primary->minecraftVersion == "1.26.51",
          "hotfix version resolves primary catalog");
    check(primary && primary->hungerRenderSlot == 17 &&
              primary->heartRenderSlot == 17,
          "primary catalog carries validated HUD vtable slots");
    check(primary && primary->attributeHandlerSlot == 144 &&
              primary->inventoryGetItemSlot == 7 &&
              primary->foodNutritionSlot == 2 &&
              primary->foodSaturationSlot == 3 &&
              primary->foodAlwaysEatSlot == 4,
          "primary catalog carries packet, inventory and food ABI slots");
    check(primary && primary->playerInventoryContainerOffset == 0x38,
          "player inventory bridge uses the inspected client offset");
    check(primary && primary->playerCarriedItemSlot == 78,
          "player carried-item virtual slot is catalogued");
    check(primary && primary->heartFullCountOffset == 0x98 &&
              primary->heartHalfCountOffset == 0xb0 &&
              primary->packetPayloadOffset == 0x30,
          "primary catalog carries validated object offsets");
    check(primary && primary->hungerShouldRenderOffset == 0x11 &&
              primary->itemNameStringOffset == 0xf0,
          "primary catalog points at mShouldRender and Item::mFullName");
    check(primary && primary->attributeStride == 0x58,
          "attribute records use the libc++ 88-byte stride");
    check(primary && primary->hungerRenderRva == 0x9673048 &&
              primary->attributePacketCompleteDtorRva == 0xcf55560 &&
              primary->attributePacketDeletingDtorRva == 0xcf555d4,
          "packet hooks are addressed by pinned file offset");
    check(primary && primary->hungerIconPositionOffset == 0xfb8 &&
              primary->hungerIconCount == 10 &&
              primary->hungerIconStride == 0xc &&
              primary->heartIconVectorOffset == 0x3498,
          "primary catalog carries the validated HUD icon geometry");

    melonrind::runtime::Bedrock12651Adapter native(*primary);
    std::array<std::byte, 0x100> hungerRenderer{};
    std::uint64_t full = 7;
    std::uint64_t half = 1;
    std::memcpy(hungerRenderer.data() + 0x18, &full, sizeof(full));
    std::memcpy(hungerRenderer.data() + 0x20, &half, sizeof(half));
    hungerRenderer[0x11] = std::byte{1};
    // The byte the adapter used to read is padding and must not affect the
    // result any more.
    hungerRenderer[0x15] = std::byte{0};
    float hunger{};
    bool visible{};
    check(native.readHunger(hungerRenderer.data(), hunger, visible) &&
              hunger == 15.0f && visible,
          "validated hunger renderer offsets produce a snapshot value");

    hungerRenderer[0x11] = std::byte{0};
    hungerRenderer[0x15] = std::byte{1};
    check(native.readHunger(hungerRenderer.data(), hunger, visible) && !visible,
          "the renderer padding byte no longer controls HUD visibility");

    std::array<std::byte, 0x140> heartRenderer{};
    std::uint64_t background = 10;
    std::uint64_t heartFull = 6;
    std::uint64_t heartHalf = 1;
    std::memcpy(heartRenderer.data() + 0x20, &background, sizeof(background));
    std::memcpy(heartRenderer.data() + 0x98, &heartFull, sizeof(heartFull));
    std::memcpy(heartRenderer.data() + 0xb0, &heartHalf, sizeof(heartHalf));
    float health{};
    float maxHealth{};
    check(native.readHealth(heartRenderer.data(), health, maxHealth) &&
              health == 13.0f && maxHealth == 20.0f,
          "validated heart renderer offsets produce health values");

    // Hunger icon origins: std::array<glm::vec3, 10> at +0xFB8, 12 bytes each.
    std::array<std::byte, 0x1100> iconRenderer{};
    for (int index = 0; index < 10; ++index) {
        const float x = 40.0f + 9.0f * static_cast<float>(index);
        const float y = 300.0f;
        const float z = 0.0f;
        auto const base = static_cast<std::size_t>(0xfb8 + index * 12);
        std::memcpy(iconRenderer.data() + base, &x, sizeof(x));
        std::memcpy(iconRenderer.data() + base + 4, &y, sizeof(y));
        std::memcpy(iconRenderer.data() + base + 8, &z, sizeof(z));
    }
    IconPosition hungerIcons[IconGeometry::kMaxHungerIcons]{};
    auto iconCount = native.readHungerIcons(iconRenderer.data(), hungerIcons,
                                            IconGeometry::kMaxHungerIcons);
    check(iconCount == 10 && hungerIcons[0].x == 40.0f &&
              hungerIcons[9].x == 121.0f && hungerIcons[4].y == 300.0f,
          "hunger icon origins are copied with the catalogued stride");
    check(native.readHungerIcons(iconRenderer.data(), hungerIcons, 4) == 4,
          "hunger icon copy honours the caller capacity");

    const float broken = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(iconRenderer.data() + 0xfb8 + 5 * 12, &broken, sizeof(broken));
    check(native.readHungerIcons(iconRenderer.data(), hungerIcons,
                                 IconGeometry::kMaxHungerIcons) == 0,
          "one implausible hunger origin invalidates the whole group");

    // ::UIControl::mCachedPosition sits at +0x10 and is what the renderers
    // translate their icon batch by.
    std::array<std::byte, 0x20> owner{};
    const float cachedX = 216.0f;
    const float cachedY = 247.0f;
    std::memcpy(owner.data() + 0x10, &cachedX, sizeof(cachedX));
    std::memcpy(owner.data() + 0x14, &cachedY, sizeof(cachedY));
    float ownerX{};
    float ownerY{};
    check(native.readHudOwnerOrigin(owner.data(), ownerX, ownerY) &&
              ownerX == cachedX && ownerY == cachedY,
          "owner cached position is read from the validated offset");
    std::memcpy(owner.data() + 0x14, &broken, sizeof(broken));
    check(!native.readHudOwnerOrigin(owner.data(), ownerX, ownerY),
          "implausible owner cached position is rejected");

    // Heart icon origins: std::vector<glm::vec3> at +0x3498.
    std::array<std::byte, 3 * 12> heartIconData{};
    for (int index = 0; index < 3; ++index) {
        const float x = 200.0f + 9.0f * static_cast<float>(index);
        const float y = 276.0f;
        auto const base = static_cast<std::size_t>(index * 12);
        std::memcpy(heartIconData.data() + base, &x, sizeof(x));
        std::memcpy(heartIconData.data() + base + 4, &y, sizeof(y));
    }
    std::array<std::byte, 0x3600> heartIconRenderer{};
    VectorAbi heartIconVector{heartIconData.data(),
                              heartIconData.data() + heartIconData.size(),
                              heartIconData.data() + heartIconData.size()};
    std::memcpy(heartIconRenderer.data() + 0x3498, &heartIconVector,
                sizeof(heartIconVector));
    IconPosition heartIcons[IconGeometry::kMaxHeartIcons]{};
    std::uint32_t heartTotal = 0;
    auto heartIconCount = native.readHeartIcons(heartIconRenderer.data(), heartIcons,
                                                IconGeometry::kMaxHeartIcons,
                                                &heartTotal);
    check(heartIconCount == 3 && heartTotal == 3 && heartIcons[2].x == 218.0f &&
              heartIcons[0].y == 276.0f,
          "heart icon origins come from the renderer's vector");
    VectorAbi reversedVector{heartIconData.data() + heartIconData.size(),
                             heartIconData.data(), heartIconData.data()};
    std::memcpy(heartIconRenderer.data() + 0x3498, &reversedVector,
                sizeof(reversedVector));
    check(native.readHeartIcons(heartIconRenderer.data(), heartIcons,
                                IconGeometry::kMaxHeartIcons, &heartTotal) == 0 &&
              heartTotal == 0,
          "a reversed heart icon vector is rejected");

    // AttributeData: mCurrentValue at +0, mName (::HashedString) at +0x18 so the
    // hash sits at +0x18 and the name string at +0x20.
    constexpr std::string_view saturationName = "minecraft:player.saturation";
    std::array<std::byte, 0x58> attribute{};
    float saturation = 9.5f;
    std::memcpy(attribute.data(), &saturation, sizeof(saturation));
    writeGameString(attribute.data() + 0x20, saturationName);
    VectorAbi vector{attribute.data(), attribute.data() + attribute.size(),
                     attribute.data() + attribute.size()};
    std::array<std::byte, 0x80> packet{};
    std::uint64_t runtimeId = 123;
    std::uint64_t tick = 456;
    std::memcpy(packet.data() + 0x30, &runtimeId, sizeof(runtimeId));
    std::memcpy(packet.data() + 0x38, &vector, sizeof(vector));
    std::memcpy(packet.data() + 0x50, &tick, sizeof(tick));
    std::uint64_t observedId{};
    std::uint64_t observedTick{};
    auto observed = native.readSaturationPacket(packet.data(), observedId,
                                                 observedTick);
    check(observed && *observed == 9.5f && observedId == runtimeId &&
              observedTick == tick,
          "attribute packet mirror retains saturation and packet identity");

    // Same packet, but with the hash the previous revision used: the name match
    // must still carry the read.
    auto const nameHash = [](std::string_view value) {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (char ch : value) {
            hash = static_cast<unsigned char>(ch) ^ (0x100000001b3ULL * hash);
        }
        return hash;
    };
    std::array<std::byte, 0x58> hashedAttribute{};
    std::memcpy(hashedAttribute.data(), &saturation, sizeof(saturation));
    auto const hash = nameHash(saturationName);
    std::memcpy(hashedAttribute.data() + 24, &hash, sizeof(hash));
    VectorAbi hashedVector{hashedAttribute.data(),
                           hashedAttribute.data() + hashedAttribute.size(),
                           hashedAttribute.data() + hashedAttribute.size()};
    std::memcpy(packet.data() + 0x38, &hashedVector, sizeof(hashedVector));
    observed = native.readSaturationPacket(packet.data(), observedId, observedTick);
    check(observed && *observed == 9.5f,
          "Bedrock's HashedString mixing still resolves the saturation attribute");
    check(nameHash(saturationName) == 0x5185acc9f6e9a50bULL,
          "attribute hashing follows HashedString::computeHash");
    check(signatureSetForVersion("1.26.53.1") == nullptr,
          "unsupported version is rejected");

    PlayerSnapshot snapshot;
    snapshot.playerIdentity = 1;
    snapshot.runtimeId = 2;
    snapshot.hunger = 40.0f;
    snapshot.maxHunger = 20.0f;
    snapshot.health = 10.0f;
    snapshot.maxHealth = std::nanf("");
    snapshot.healthAvailable = true;
    snapshot.mainHandFood = FoodSnapshot{8, 0.8f, false, false, "minecraft:porkchop"};
    snapshot = sanitizeSnapshot(snapshot);
    check(snapshot.hunger == 20.0f && snapshot.health == 10.0f,
          "invalid numeric state is clamped");
    check(snapshot.mainHandFood.has_value(), "valid food survives sanitization");
    check(snapshot.hasFiniteHealth(), "native health availability enables preview");

    snapshot.healthAvailable = false;
    check(!snapshot.hasFiniteHealth(), "unknown native health suppresses preview");

    snapshot.mainHandFood = FoodSnapshot{8, 0.8f, false, false, "minecraft:porkchop"};
    snapshot.alive = false;
    snapshot = sanitizeSnapshot(snapshot);
    check(!snapshot.mainHandFood.has_value(), "dead player has no food preview");

    PlayerSnapshot iconSnapshot;
    iconSnapshot.icons.hungerCount = 2;
    iconSnapshot.icons.hunger[0] = IconPosition{40.0f, 300.0f, 0.0f};
    iconSnapshot.icons.hunger[1] = IconPosition{49.0f, 300.0f, 0.0f};
    iconSnapshot.icons.heartCount = 1;
    iconSnapshot.icons.heart[0] = IconPosition{200.0f, 276.0f, 0.0f};
    iconSnapshot.icons.heartTotal = 1;
    auto clean = sanitizeSnapshot(iconSnapshot);
    check(clean.icons.hungerCount == 2 && clean.icons.hunger[1].x == 49.0f &&
              clean.icons.heartCount == 1 && clean.icons.heart[0].y == 276.0f,
          "plausible icon origins survive sanitization");

    iconSnapshot.icons.hunger[1].x = std::numeric_limits<float>::quiet_NaN();
    clean = sanitizeSnapshot(iconSnapshot);
    check(clean.icons.hungerCount == 0 && clean.icons.heartCount == 1,
          "one implausible origin clears its own group only");

    iconSnapshot.icons.hunger[1].x = 49.0f;
    iconSnapshot.icons.heartCount = 9999;
    clean = sanitizeSnapshot(iconSnapshot);
    check(clean.icons.heartCount == IconGeometry::kMaxHeartIcons,
          "icon counts are clamped to the bounded copy");

    PlayerSnapshot originSnapshot;
    originSnapshot.hungerBar = HudBarOrigin{true, 417.0f, 247.0f};
    originSnapshot.heartBar = HudBarOrigin{true, 216.0f, std::nanf("")};
    auto cleanOrigins = sanitizeSnapshot(originSnapshot);
    check(cleanOrigins.hungerBar.available &&
              cleanOrigins.hungerBar.x == 417.0f && !cleanOrigins.heartBar.available,
          "an implausible bar origin is dropped on its own");

    std::cout << "Passed runtime checks\n";
}
