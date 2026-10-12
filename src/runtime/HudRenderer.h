#pragma once

#include "runtime/Config.h"
#include "runtime/PlayerSnapshot.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pl/ModMenu.hpp>

namespace melonrind::runtime {

/** A launcher HUD-editor descriptor; the Java bridge can serialize this. */
struct HudEditorElement {
    std::string moduleId;
    std::string displayName;
    float x{};
    float y{};
    float width{};
    float height{};
    std::uint32_t snapFlags{};
    float gridSize{8.0f};
};

/**
 * Lightweight Mod Menu renderer for Android.  It only consumes copied
 * PlayerSnapshot values and submits public preloader DrawCommand primitives.
 * Position editing is exposed as data because the launcher owns the HUD editor;
 * the renderer reads the current surface size from the public Preloader API.
 */
class HudRenderer {
public:
    static constexpr std::string_view kModuleId = "melonrind.android.hud";

    explicit HudRenderer(std::string modId = "melonrind_android")
        : mModId(std::move(modId)) {}

    [[nodiscard]] bool registerModule();
    void unregisterModule() noexcept;

    [[nodiscard]] bool registered() const noexcept { return mRegistered; }
    [[nodiscard]] const Config &config() const noexcept { return mConfig; }
    void setModId(std::string modId);
    void setConfig(Config config) noexcept;
    void setConfigChangedCallback(std::function<void(const Config &)> callback);

    /** Update the surface dimensions reported by the Java overlay bridge. */
    void setSurfaceSize(float width, float height) noexcept;

    /** Submit the current frame's commands, replacing the previous overlay. */
    void render(const PlayerSnapshot &snapshot, float timeSeconds = 0.0f);
    void clear() noexcept;

    /** Pure command generation hook useful for host-side tests. */
    [[nodiscard]] std::vector<pl::modmenu::DrawCommand> buildCommands(
        const PlayerSnapshot &snapshot, float timeSeconds = 0.0f) const;

    [[nodiscard]] std::vector<HudEditorElement> editorElements() const;
    void applyEditorPosition(float x, float y) noexcept;

private:
    std::string mModId;
    Config mConfig{};
    bool mRegistered{};
    float mSurfaceWidth{};
    float mSurfaceHeight{};
    std::vector<std::vector<unsigned char>> mImagePixels;
    std::function<void(const Config &)> mConfigChanged;

    [[nodiscard]] bool registerImages();
    void handleToggle(std::string_view moduleId, bool enabled);
    void handleConfig(std::string_view moduleId, std::string_view key,
                      std::string_view value);
    void notifyConfigChanged();
    [[nodiscard]] float baseX(float contentWidth) const noexcept;
    [[nodiscard]] float baseY(float contentHeight) const noexcept;
};

} // namespace melonrind::runtime

