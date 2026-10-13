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
    std::string_view networkHandlerType;
    std::size_t hungerRenderSlot{};
    std::size_t heartRenderSlot{};
    std::size_t attributeHandlerSlot{};

    // 1.26.51.1 object layout contracts.  These are validated by the
    // offline inspector before the capability is enabled.
    //
    // HudHungerRenderer (base ::MinecraftUICustomRenderer ends at 0x0c):
    //   +0x10 mHasLoadedTextures, +0x11 mShouldRender, +0x12 mUseHungerEffect,
    //   +0x18 mNumFullIcons, +0x20 mNumHalfIcons.
    // The render entry point returns immediately when +0x11 is false, so a
    // snapshot produced from that callback is only meaningful while the host
    // HUD bar is actually being drawn.
    std::size_t hungerShouldRenderOffset{};
    std::size_t heartBackgroundCountOffset{};
    std::size_t heartFullCountOffset{};
    std::size_t heartHalfCountOffset{};
    std::size_t playerInventoryOffset{};
    std::size_t playerInventorySelectedOffset{};
    std::size_t playerInventoryContainerOffset{};
    std::size_t playerCarriedItemSlot{};
    std::size_t inventoryGetItemSlot{};
    std::size_t itemWeakCounterOffset{};
    std::size_t itemNameStringOffset{};
    std::size_t itemGetFoodSlot{};
    std::size_t foodNutritionSlot{};
    std::size_t foodSaturationSlot{};
    std::size_t foodAlwaysEatSlot{};
    std::size_t packetPayloadOffset{};
    /**
     * sizeof(::AttributeData) on the libc++/ARM64 build: six floats, a
     * ::HashedString (8 + 24 + 8) and a std::vector (24) = 88.  The MSVC layout
     * used by the desktop headers is 96, which walked off the end of the
     * vector.
     */
    std::size_t attributeStride{};
    /**
     * Icon geometry of the two HUD bar renderers.
     *
     * `::HudHungerRenderer::mIconPosition` is a `std::array<::glm::vec3, 10>`:
     * ten background slots, filled by the renderer before it draws.  The
     * disassembly passes it as the position argument of the icon draw helper
     * three times (10 backgrounds, `mNumFullIcons` full icons, `mNumHalfIcons`
     * half icons), and the half-icon run starts at
     * `mIconPosition + mNumFullIcons * 12`, which fixes the element stride.
     *
     * `::HudHeartRenderer::mIconPosition` is a `std::vector<::glm::vec3>` whose
     * begin/end/capacity words are read directly; absorption icons are appended
     * after the normal-health slots, so consumers must cap the usable range.
     *
     * Both offsets follow from rebasing the generated MSVC headers onto the
     * measured `::MinecraftUICustomRenderer` base size (0x0c) and were then
     * confirmed instruction by instruction against the shipped library.
     */
    std::size_t hungerIconPositionOffset{};
    std::size_t hungerIconCount{};
    std::size_t hungerIconStride{};
    std::size_t heartIconVectorOffset{};
    /**
     * File offsets inside the pinned libminecraftpe.so.
     *
     * The RTTI/vtable resolver in the launcher's Preloader walks `.data.rel.ro`
     * for a word equal to the typeinfo address and returns the first candidate
     * whose slot is non-zero.  For some types (measured on
     * UpdateAttributesPacket) more than one candidate matches, so the same
     * query can land one slot too far and hook an unrelated function.  The
     * library is pinned by SHA256 in the catalog, so the offsets below are used
     * directly; the module base comes from the already-validated hunger slot.
     */
    std::size_t hungerRenderRva{};
    std::size_t attributePacketCompleteDtorRva{};
    std::size_t attributePacketDeletingDtorRva{};

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

