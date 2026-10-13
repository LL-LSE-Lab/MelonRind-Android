#include "runtime/SignatureCatalog.h"

namespace melonrind::runtime {
namespace {

constexpr SignatureSet emptySet(std::string_view version) {
    SignatureSet set{};
    set.minecraftVersion = version;
    set.moduleName = "libminecraftpe.so";
    return set;
}

// The 1.26.51 entry is tied to the supplied ARM64 library hash in the JSON
// fixture.  The adapter only enables these offsets for that exact catalog
// entry; other game versions remain capability-disabled.
constexpr SignatureSet kMinecraft12650 = emptySet("1.26.50");
constexpr SignatureSet kMinecraft12651{
    "1.26.51", "libminecraftpe.so", {}, {}, {}, {}, {}, {},
    "17HudHungerRenderer", "16HudHeartRenderer", "20ClientNetworkHandler",
    17, 17, 144,
    0x11,
    0x20, 0x98, 0xb0,
    0x5e8, 0x10, 0x38, 78, 7,
    0x8, 0xf0, 26, 2, 3, 4, 0x30, 0x58,
    0x9673048, 0xcf55560, 0xcf555d4};
constexpr SignatureSet kMinecraft12652 = emptySet("1.26.52");

} // namespace

const SignatureSet *signatureSetForVersion(
    std::string_view minecraftVersion) noexcept {
    // Launchers may report a patch suffix (for example 1.26.51.1).  Match the
    // first three components while leaving exact catalog keys in one place.
    for (const auto *set : {&kMinecraft12650, &kMinecraft12651,
                            &kMinecraft12652}) {
        if (minecraftVersion == set->minecraftVersion ||
            (minecraftVersion.size() > set->minecraftVersion.size() &&
             minecraftVersion.starts_with(set->minecraftVersion) &&
             minecraftVersion[set->minecraftVersion.size()] == '.')) {
            return set;
        }
    }
    return nullptr;
}

} // namespace melonrind::runtime

