#include "runtime/SignatureCatalog.h"

namespace melonrind::runtime {
namespace {

// Data-access patterns intentionally stay empty until they are verified against
// the corresponding ARM64 libminecraftpe.so build. The 1.26.51 entry carries
// a separately validated RTTI/vtable HUD adapter; all deeper reads remain
// unavailable rather than guessing an offset.
constexpr SignatureSet kMinecraft12650{
    "1.26.50", "libminecraftpe.so", {}, {}, {}, {}, {}, {}, {}, {}, 0, 0};
constexpr SignatureSet kMinecraft12651{
    "1.26.51", "libminecraftpe.so", {}, {}, {}, {}, {}, {},
    "17HudHungerRenderer", "16HudHeartRenderer", 17, 17};
constexpr SignatureSet kMinecraft12652{
    "1.26.52", "libminecraftpe.so", {}, {}, {}, {}, {}, {}, {}, {}, 0, 0};

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

