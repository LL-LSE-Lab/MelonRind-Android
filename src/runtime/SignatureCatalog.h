#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace melonrind::runtime {

/**
 * A version-specific group of preloader signatures.  The strings are kept in
 * the same order as the JSON files under runtime/signatures so an update can
 * replace the data without changing GameAdapter's public interface.
 */
struct SignatureSet {
    std::string_view minecraftVersion;
    std::string_view moduleName;
    std::string_view frame;
    std::string_view playerState;
    std::string_view hunger;
    std::string_view health;
    std::string_view mainHandFood;
    std::string_view attributeUpdate;
    std::string_view hungerRendererType;
    std::string_view heartRendererType;
    std::size_t hungerRenderSlot{};
    std::size_t heartRenderSlot{};

    [[nodiscard]] constexpr bool hasAnyPattern() const noexcept {
        return !frame.empty() || !playerState.empty() || !hunger.empty() ||
               !health.empty() || !mainHandFood.empty() ||
               !attributeUpdate.empty();
    }
};

/** Returns the checked-in signature set or nullptr for an unsupported build. */
[[nodiscard]] const SignatureSet *signatureSetForVersion(
    std::string_view minecraftVersion) noexcept;

[[nodiscard]] constexpr std::array<std::string_view, 6> signatureNames() {
    return {"frame", "playerState", "hunger", "health", "mainHandFood",
            "attributeUpdate"};
}

} // namespace melonrind::runtime

