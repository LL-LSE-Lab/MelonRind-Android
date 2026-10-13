#include "runtime/Bedrock12651Adapter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#if defined(__ANDROID__)
#include <sys/mman.h>
#endif
#include <vector>

namespace melonrind::runtime {
namespace {

template <typename T>
[[nodiscard]] T load(void const *base, std::size_t offset) noexcept {
    T result{};
    std::memcpy(&result, static_cast<std::byte const *>(base) + offset,
                sizeof(result));
    return result;
}

[[nodiscard]] bool plausiblePointer(void const *value) noexcept {
    auto address = reinterpret_cast<std::uintptr_t>(value);
    return address >= 0x10000U && (address & 0x7U) == 0;
}

/**
 * Is `[pointer, pointer + bytes)` inside a mapped region of this process?
 *
 * `mincore` answers that without dereferencing anything, so a speculative read
 * of a not-yet-filled packet cannot fault.  The earlier implementation parsed
 * /proc/self/maps, which turned out to be unreliable inside the game process
 * (every lookup failed and silently rejected all reads).
 */
[[nodiscard]] bool readable(void const *pointer, std::size_t bytes) noexcept {
    if (!pointer || bytes == 0) return false;
#if defined(__ANDROID__)
    constexpr std::uintptr_t pageSize = 4096;
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (begin > std::numeric_limits<std::uintptr_t>::max() - bytes) return false;
    const auto end = begin + bytes;
    const auto pageBegin = begin & ~(pageSize - 1);
    const auto pageEnd = (end + pageSize - 1) & ~(pageSize - 1);
    const auto span = pageEnd - pageBegin;
    const auto pages = span / pageSize;
    if (pages == 0 || pages > 64) return false;
    unsigned char residency[64]{};
    return ::mincore(reinterpret_cast<void *>(pageBegin), span, residency) == 0;
#else
    // Host builds have no mincore; the model tests only need the structural
    // checks, so the mapping validation is Android-only.
    (void)bytes;
    return true;
#endif
}

[[nodiscard]] std::uint64_t hashString(std::string_view value) noexcept {
    // Bedrock's ::HashedString::computeHash multiplies first and folds the byte
    // in afterwards (`hash = byte ^ (prime * hash)`).  The more common FNV-1a
    // order used to live here and could never match a native HashedString, so
    // attribute lookups by hash silently failed.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char ch : value) hash = ch ^ (0x100000001b3ULL * hash);
    return hash;
}

struct SharedVector {
    std::byte const *begin{};
    std::byte const *end{};
    std::byte const *capacity{};
};

struct SharedPtrAbi {
    void *pointer{};
    void *control{};
};

/** sizeof(::glm::vec3) on the validated ARM64 build, in bytes. */
constexpr std::size_t kIconStride = 12;
/**
 * Sanity ceiling for the heart icon vector.  The vanilla bar is drawn from at
 * most `ceil(maxHealth / 2)` slots, and the model accepts up to 2000 health, so
 * 1000 elements is the largest layout the rest of the code can consume.
 */
constexpr std::size_t kMaxHeartIconBytes = 1000 * kIconStride;

[[nodiscard]] std::string readGameString(void const *address) noexcept {
    // libc++ (default ABI, little endian) lays out std::string as
    //   __short { uchar __is_long_:1, __size_:7; char __data_[23]; }
    //   __long  { size_type __is_long_:1, __cap_:63; size_type __size_; char* __data_; }
    // so the discrimination byte is at +0, the size of a long string at +8 and
    // its heap pointer at +0x10.  The previous revision read the long pointer
    // from +0 and the size from +8, which corrupted every name of 23 bytes or
    // more ("minecraft:player.saturation", "minecraft:enchanted_golden_apple").
    if (!readable(address, 24)) return {};
    auto const first = load<std::uint8_t>(address, 0);
    const char *data{};
    std::size_t size{};
    if ((first & 1U) == 0) {
        size = first >> 1U;
        data = static_cast<char const *>(address) + 1;
    } else {
        size = load<std::size_t>(address, 8);
        data = load<char const *>(address, 0x10);
    }
    if (size > 96 || (!data && size != 0)) return {};
    if (size == 0) return {};
    if (!readable(data, size)) return {};
    for (std::size_t i = 0; i < size; ++i) {
        auto c = static_cast<unsigned char>(data[i]);
        if (c < 0x20 || c > 0x7e) return {};
    }
    return std::string(data, size);
}

[[nodiscard]] std::string hexPointer(void const *value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(value);
    return stream.str();
}

/** True for the "namespace:name" identifiers Bedrock uses for items. */
[[nodiscard]] bool looksLikeNamespacedId(std::string_view value) noexcept {
    auto const colon = value.find(':');
    if (colon == std::string_view::npos || colon == 0 ||
        colon + 1 >= value.size()) {
        return false;
    }
    for (char ch : value) {
        auto c = static_cast<unsigned char>(ch);
        bool const allowed =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            ch == '_' || ch == '.' || ch == '-' || ch == ':' || ch == '/';
        if (!allowed) return false;
    }
    return true;
}

} // namespace

void *Bedrock12651Adapter::localPlayer(void *client) const noexcept {
    if (!client) return nullptr;
    auto table = load<void **>(client, 0);
    if (!plausiblePointer(table) || !table[32]) return nullptr;
    using GetLocalPlayer = void *(*)(void *);
    return reinterpret_cast<GetLocalPlayer>(table[32])(client);
}

void *Bedrock12651Adapter::localPlayerFromNetworkHandler(
    void *handler) const noexcept {
    if (!handler) return nullptr;
    auto client = load<void *>(handler, 0x58);
    return localPlayer(client);
}

bool Bedrock12651Adapter::isInWorldWithoutMenu(void *client) const noexcept {
    if (!client) return false;
    auto table = load<void **>(client, 0);
    if (!plausiblePointer(table) || !table[159]) return false;
    using VisibilityFn = bool (*)(void const *);
    return reinterpret_cast<VisibilityFn>(table[159])(client);
}

bool Bedrock12651Adapter::readHunger(void *renderer, float &value,
                                     bool &hudVisible) const noexcept {
    if (!renderer) return false;
    auto full = load<std::uint64_t>(renderer, 0x18);
    auto half = load<std::uint64_t>(renderer, 0x20);
    // ::HudHungerRenderer::mShouldRender.  +0x14..+0x17 are padding before
    // mNumFullIcons, so reading +0x15 always produced "false" and suppressed
    // every draw command the mod submitted.
    hudVisible =
        load<std::uint8_t>(renderer, mSet.hungerShouldRenderOffset) != 0;
    if (full > 10 || half > 1) return false;
    value = static_cast<float>(full * 2U + half);
    return true;
}

bool Bedrock12651Adapter::readHealth(void *renderer, float &value,
                                     float &maximum) const noexcept {
    if (!renderer) return false;
    auto background = load<std::uint64_t>(renderer,
                                          mSet.heartBackgroundCountOffset);
    auto full = load<std::uint64_t>(renderer, mSet.heartFullCountOffset);
    auto half = load<std::uint64_t>(renderer, mSet.heartHalfCountOffset);
    if (background > 100 || full > 100 || half > 100 || full + half > background) {
        return false;
    }
    maximum = static_cast<float>(background * 2U);
    value = static_cast<float>(full * 2U + half);
    return maximum > 0.0f && value <= maximum;
}

std::uint32_t Bedrock12651Adapter::readHungerIcons(
    void *renderer, IconPosition *out, std::uint32_t capacity) const noexcept {
    if (!renderer || !out || capacity == 0) return 0;
    const auto count = static_cast<std::uint32_t>(mSet.hungerIconCount);
    const auto stride = static_cast<std::uint32_t>(mSet.hungerIconStride);
    if (count == 0 || stride < kIconStride) return 0;
    auto const *base = static_cast<std::byte const *>(renderer) +
                       mSet.hungerIconPositionOffset;
    // The whole array is validated before anything is copied: the renderer
    // fills it in an update step, so a partially plausible array means the
    // object we are looking at is not the layout the catalog describes.
    if (!readable(base, static_cast<std::size_t>(count) * stride)) return 0;
    const auto limit = std::min(count, capacity);
    std::uint32_t written = 0;
    for (std::uint32_t index = 0; index < limit; ++index) {
        auto const element = static_cast<std::size_t>(index) * stride;
        IconPosition icon;
        icon.x = load<float>(base, element + 0);
        icon.y = load<float>(base, element + 4);
        icon.z = load<float>(base, element + 8);
        if (!std::isfinite(icon.x) || !std::isfinite(icon.y)) return 0;
        out[written++] = icon;
    }
    return written;
}

std::uint32_t Bedrock12651Adapter::readHeartIcons(
    void *renderer, IconPosition *out, std::uint32_t capacity,
    std::uint32_t *total) const noexcept {
    if (total) *total = 0;
    if (!renderer || !out || capacity == 0) return 0;
    auto const *vector = static_cast<std::byte const *>(renderer) +
                         mSet.heartIconVectorOffset;
    if (!readable(vector, sizeof(SharedVector))) return 0;
    // `std::vector<::glm::vec3>`: three words, the second being the end.  The
    // icon count is derived from the byte size so a reallocated or empty vector
    // (which the renderer legitimately produces while the bar is hidden) cannot
    // be misread as icon data.
    SharedVector entries = load<SharedVector>(vector, 0);
    if (!entries.begin || !entries.end || entries.end < entries.begin) return 0;
    const auto bytes = static_cast<std::size_t>(entries.end - entries.begin);
    if (bytes == 0 || bytes % kIconStride != 0 ||
        bytes > kMaxHeartIconBytes) {
        return 0;
    }
    if (!readable(entries.begin, bytes)) return 0;
    const auto count = static_cast<std::uint32_t>(bytes / kIconStride);
    if (total) *total = count;
    const auto limit = std::min(count, capacity);
    std::uint32_t written = 0;
    for (std::uint32_t index = 0; index < limit; ++index) {
        auto const element = static_cast<std::size_t>(index) * kIconStride;
        IconPosition icon;
        icon.x = load<float>(entries.begin, element + 0);
        icon.y = load<float>(entries.begin, element + 4);
        icon.z = load<float>(entries.begin, element + 8);
        if (!std::isfinite(icon.x) || !std::isfinite(icon.y)) {
            if (total) *total = 0;
            return 0;
        }
        out[written++] = icon;
    }
    return written;
}

bool Bedrock12651Adapter::readHudOwnerOrigin(void *owner, float &x,
                                             float &y) const noexcept {
    if (!owner || !readable(owner, 0x20)) return false;
    // `0x968bc9c(owner)` refreshes the cache and returns `owner + 0x10`; the
    // render entry points read the two floats there and use them as the
    // translation that positions the whole icon batch.
    const float cachedX = load<float>(owner, 0x10);
    const float cachedY = load<float>(owner, 0x14);
    if (!std::isfinite(cachedX) || !std::isfinite(cachedY) ||
        std::abs(cachedX) > 100000.0f || std::abs(cachedY) > 100000.0f) {
        return false;
    }
    x = cachedX;
    y = cachedY;
    return true;
}

std::string Bedrock12651Adapter::describeHudTransform(const char *label, void *owner,
                                                     void *context) const noexcept {
    std::ostringstream out;
    out << "transform " << (label ? label : "?") << " owner=" << hexPointer(owner)
        << " cached=(";
    float cached[2]{};
    if (readable(owner, 0x20)) {
        cached[0] = load<float>(owner, 0x10);
        cached[1] = load<float>(owner, 0x14);
    }
    out << cached[0] << ',' << cached[1] << ") context=" << hexPointer(context);

    // `[[context + 0x18] + 0x48]` is the handle the render entry point uses to
    // obtain the transform matrix: the call returns a shared_ptr and the matrix
    // pointer is the handle's first word.  Both the window and the followed
    // pointer are recorded so the exact member can be confirmed offline.
    std::uintptr_t scene = 0;
    if (readable(context, 0x20)) scene = load<std::uintptr_t>(context, 0x18);
    out << " scene=" << hexPointer(reinterpret_cast<void *>(scene));
    std::uintptr_t matrix = 0;
    if (readable(reinterpret_cast<void *>(scene), 0x60)) {
        for (std::size_t offset : {std::size_t{0x48}, std::size_t{0x50},
                                   std::size_t{0x58}, std::size_t{0x60}}) {
            auto const candidate =
                load<std::uintptr_t>(reinterpret_cast<void *>(scene), offset);
            if (plausiblePointer(reinterpret_cast<void *>(candidate)) &&
                readable(reinterpret_cast<void *>(candidate), 64)) {
                matrix = candidate;
                out << " matrixField=+" << std::hex << offset << std::dec;
                break;
            }
        }
    }
    out << " matrix=" << hexPointer(reinterpret_cast<void *>(matrix));
    if (matrix) {
        out << " m=[";
        for (int i = 0; i < 16; ++i) {
            if (i) out << ' ';
            out << load<float>(reinterpret_cast<void *>(matrix),
                               static_cast<std::size_t>(i) * 4);
        }
        out << ']';
    }
    auto const dumpWords = [&out](const char *name, void const *base,
                                  std::size_t words) {
        out << "\n  " << name << "=[";
        if (readable(base, words * 4)) {
            for (std::size_t i = 0; i < words; ++i) {
                if (i) out << ' ';
                out << std::hex
                    << load<std::uint32_t>(base, i * 4) << std::dec;
            }
        } else {
            out << "unreadable";
        }
        out << ']';
    };
    dumpWords("context", context, 0x10);
    dumpWords("scene", reinterpret_cast<void *>(scene), 0x20);
    return out.str();
}

std::optional<FoodSnapshot> Bedrock12651Adapter::readMainHandFood(
    void *player) const noexcept {
    if (!player) return std::nullopt;

    auto readStack = [&](void const *stack) -> std::optional<FoodSnapshot> {
        if (!plausiblePointer(stack)) return std::nullopt;
        auto counter = load<void *>(stack, mSet.itemWeakCounterOffset);
        if (!plausiblePointer(counter)) return std::nullopt;
        auto item = load<void *>(counter, 0);
        if (!plausiblePointer(item)) return std::nullopt;
        auto itemTable = load<void **>(item, 0);
        if (!plausiblePointer(itemTable) || !itemTable[mSet.itemGetFoodSlot]) {
            return std::nullopt;
        }
        using GetFood = void *(*)(void const *);
        auto food = reinterpret_cast<GetFood>(
            itemTable[mSet.itemGetFoodSlot])(item);
        if (!plausiblePointer(food)) return std::nullopt;
        auto foodTable = load<void **>(food, 0);
        if (!plausiblePointer(foodTable) ||
            !foodTable[mSet.foodNutritionSlot] ||
            !foodTable[mSet.foodSaturationSlot] ||
            !foodTable[mSet.foodAlwaysEatSlot]) {
            return std::nullopt;
        }

        using GetNutrition = int (*)(void const *);
        using GetSaturation = float (*)(void const *);
        using GetAlwaysEat = bool (*)(void const *);
        auto nutrition = reinterpret_cast<GetNutrition>(
            foodTable[mSet.foodNutritionSlot])(food);
        auto saturation = reinterpret_cast<GetSaturation>(
            foodTable[mSet.foodSaturationSlot])(food);
        auto alwaysEat = reinterpret_cast<GetAlwaysEat>(
            foodTable[mSet.foodAlwaysEatSlot])(food);
        if (nutrition < 0 || nutrition > 1000 ||
            !std::isfinite(saturation) || saturation < 0.0f ||
            saturation > 1000.0f) {
            return std::nullopt;
        }

        FoodSnapshot result;
        result.nutrition = nutrition;
        result.saturationModifier = saturation;
        result.alwaysEat = alwaysEat;
        // Item::mFullName is a ::HashedString at +0xe8 on the libc++ layout
        // (mDescriptionId sits at +0x90 and getFood() returns +0x180, both
        // confirmed against the shipped library), so the name string lives at
        // +0xf0.  The previous +0x108 landed inside mFullName.mLastMatch and
        // always decoded to an empty name.
        auto itemId = readGameString(static_cast<std::byte const *>(item) +
                                      mSet.itemNameStringOffset);
        if (!looksLikeNamespacedId(itemId)) itemId.clear();
        result.itemId = std::move(itemId);
        result.uncertainEffects = result.itemId.empty() ||
            result.itemId.find("minecraft:") != 0;
        if (!result.itemId.empty()) {
            result.uncertainEffects =
                result.itemId == "minecraft:golden_apple" ||
                result.itemId == "minecraft:enchanted_golden_apple" ||
                result.itemId == "minecraft:appleenchanted" ||
                result.itemId == "minecraft:suspicious_stew" ||
                result.itemId == "minecraft:rotten_flesh" ||
                result.itemId == "minecraft:pufferfish" ||
                result.itemId == "minecraft:poisonous_potato" ||
                result.itemId == "minecraft:spider_eye" ||
                result.itemId == "minecraft:chicken";
        }
        return result;
    };

    // Actor::getCarriedItem is virtual in the target and returns the actual
    // selected ItemStack reference. This avoids depending on the unrelated
    // creative-inventory layout while retaining the inspected inventory path
    // as a fallback for custom client builds.
    auto playerTable = load<void **>(player, 0);
    if (plausiblePointer(playerTable) && playerTable[mSet.playerCarriedItemSlot]) {
        using GetCarriedItem = void const *(*)(void const *);
        if (auto food = readStack(reinterpret_cast<GetCarriedItem>(
                playerTable[mSet.playerCarriedItemSlot])(player))) {
            return food;
        }
    }

    auto inventory = load<void *>(player, mSet.playerInventoryOffset);
    if (!plausiblePointer(inventory)) return std::nullopt;
    auto selected = load<std::int32_t>(inventory,
                                       mSet.playerInventorySelectedOffset);
    if (selected < 0 || selected > 35) return std::nullopt;
    auto container = load<void *>(inventory, mSet.playerInventoryContainerOffset);
    if (!plausiblePointer(container)) return std::nullopt;
    auto table = load<void **>(container, 0);
    if (!plausiblePointer(table) || !table[mSet.inventoryGetItemSlot]) {
        return std::nullopt;
    }
    if (mInventoryGetItemTarget &&
        reinterpret_cast<std::uintptr_t>(table[mSet.inventoryGetItemSlot]) !=
            mInventoryGetItemTarget) {
        return std::nullopt;
    }
    using GetItem = void const *(*)(void *, int);
    return readStack(reinterpret_cast<GetItem>(table[mSet.inventoryGetItemSlot])(
        container, selected));
}

std::optional<float> Bedrock12651Adapter::readSaturationPacket(
    void *packet, std::uint64_t &runtimeId, std::uint64_t &tick,
    std::string *probe) const noexcept {
    auto const note = [probe](std::string text) {
        if (probe) *probe = std::move(text);
    };
    if (!packet) {
        note("null");
        return std::nullopt;
    }
    // This path can run from the packet's destructor, where the payload may
    // already have been moved out, so every access is validated.
    if (!readable(packet, mSet.packetPayloadOffset + 40)) {
        note("packet-not-readable ptr=" + hexPointer(packet) + " len=" +
             std::to_string(mSet.packetPayloadOffset + 40));
        return std::nullopt;
    }
    auto payload = static_cast<std::byte const *>(packet) + mSet.packetPayloadOffset;
    runtimeId = load<std::uint64_t>(payload, 0);
    SharedVector attributes = load<SharedVector>(payload, 8);
    tick = load<std::uint64_t>(payload, 32);
    auto const stride = mSet.attributeStride ? mSet.attributeStride : 0x58;
    if (!attributes.begin || !attributes.end || attributes.end < attributes.begin) {
        note("rid=" + std::to_string(runtimeId) + " vector=null");
        return std::nullopt;
    }
    auto const bytes =
        static_cast<std::size_t>(attributes.end - attributes.begin);
    if (bytes == 0 || bytes % stride != 0 || bytes > 128 * stride) {
        note("rid=" + std::to_string(runtimeId) + " bytes=" +
             std::to_string(bytes));
        return std::nullopt;
    }
    if (!readable(attributes.begin, bytes)) {
        note("rid=" + std::to_string(runtimeId) + " vector-not-readable");
        return std::nullopt;
    }
    constexpr std::string_view kSaturationName = "minecraft:player.saturation";
    auto const hash = hashString(kSaturationName);
    auto const count = bytes / stride;
    std::string names;
    for (std::size_t i = 0; i < std::min<std::size_t>(count, 128); ++i) {
        auto entry = attributes.begin + i * stride;
        // AttributeData::mName is a ::HashedString at +0x18, so its string
        // lives at +0x20.  Compare the decoded name first and only fall back
        // to the hash: that keeps the read correct even if a future build
        // changes the hash mixing.
        auto name = readGameString(entry + 32);
        if (names.size() < 120) {
            names += name.empty() ? std::string("?") : name;
            names.push_back(',');
        }
        if (name.empty() || name != kSaturationName) {
            if (load<std::uint64_t>(entry, 24) != hash) continue;
        }
        auto value = load<float>(entry, 0);
        if (std::isfinite(value) && value >= 0.0f && value <= 20.0f) {
            note("rid=" + std::to_string(runtimeId) + " n=" +
                 std::to_string(count) + " sat=" + std::to_string(value));
            return value;
        }
    }
    note("rid=" + std::to_string(runtimeId) + " n=" + std::to_string(count) +
         " names=" + names);
    return std::nullopt;
}

} // namespace melonrind::runtime
