#include "runtime/Config.h"
#include "runtime/GameAdapter.h"
#include "runtime/HudRenderer.h"

#include <pl/Config.hpp>
#include <pl/Mod.hpp>

#include <cstdlib>
#include <exception>
#include <mutex>
#include <optional>
#include <string>

namespace melonrind {
namespace {

constexpr std::string_view kPrimaryVersion = "1.26.51";

std::string requestedMinecraftVersion() {
    if (const char *value = std::getenv("MELONRIND_MINECRAFT_VERSION")) {
        if (*value) return value;
    }
    return std::string(kPrimaryVersion);
}

class ModEntry {
public:
    static ModEntry &instance() {
        static ModEntry entry;
        return entry;
    }

    ModEntry() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod &self() noexcept { return mSelf; }

    bool load() noexcept {
        try {
            std::lock_guard lock(mMutex);
            mConfig.emplace();
            if (!mConfig->load()) {
                mSelf.getLogger().error("Failed to load typed configuration");
                mConfig.reset();
                return false;
            }
            mConfig->value().normalize();
            if (!mConfig->save()) {
                mSelf.getLogger().error("Failed to persist normalized configuration");
                mConfig.reset();
                return false;
            }
            mRenderer.setConfig(mConfig->value());
            mAdapter.setSnapshotCallback([this](const runtime::PlayerSnapshot &snapshot) {
                if (mEnabled) mRenderer.render(snapshot);
            });
            mRenderer.setConfigChangedCallback([this](const runtime::Config &config) {
                std::lock_guard lock(mMutex);
                if (!mConfig) return;
                mConfig->value() = config;
                mConfig->value().normalize();
                if (!mConfig->save()) {
                    mSelf.getLogger().warn("Failed to persist HUD configuration");
                }
            });
            mSelf.getLogger().info("Loaded MelonRind Android {}", mSelf.getVersion());
            return true;
        } catch (const std::exception &error) {
            mSelf.getLogger().error("Configuration exception: {}", error.what());
            return false;
        } catch (...) {
            mSelf.getLogger().error("Unknown configuration exception");
            return false;
        }
    }

    bool enable() noexcept {
        try {
            auto version = requestedMinecraftVersion();
            if (!mAdapter.install(version)) {
                mSelf.getLogger().warn(
                    "Minecraft {} is not present in the checked-in adapter catalog; "
                    "native reads remain disabled",
                    version);
            } else if (!mAdapter.nativeReadsAvailable()) {
                mSelf.getLogger().warn(
                    "Minecraft {} has no validated ARM64 signatures; HUD stays empty "
                    "until an adapter is supplied",
                    version);
            }

            if (!mRenderer.registerModule()) {
                mAdapter.uninstall();
                mSelf.getLogger().error("Failed to register Mod Menu HUD");
                return false;
            }
            mEnabled = true;
            return true;
        } catch (const std::exception &error) {
            mSelf.getLogger().error("Enable exception: {}", error.what());
            return false;
        } catch (...) {
            mSelf.getLogger().error("Unknown enable exception");
            return false;
        }
    }

    bool disable() noexcept {
        try {
            mEnabled = false;
            mRenderer.clear();
            mRenderer.unregisterModule();
            mAdapter.uninstall();
            return true;
        } catch (const std::exception &error) {
            mSelf.getLogger().error("Disable exception: {}", error.what());
            return false;
        } catch (...) {
            mSelf.getLogger().error("Unknown disable exception");
            return false;
        }
    }

    bool unload() noexcept {
        try {
            disable();
            std::lock_guard lock(mMutex);
            mConfig.reset();
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    ll::mod::NativeMod &mSelf;
    std::mutex mMutex;
    std::optional<pl::config::ConfigFile<runtime::Config>> mConfig;
    runtime::GameAdapter mAdapter;
    runtime::HudRenderer mRenderer;
    bool mEnabled{};
};

} // namespace
} // namespace melonrind

PL_REGISTER_MOD(melonrind::ModEntry, melonrind::ModEntry::instance())
