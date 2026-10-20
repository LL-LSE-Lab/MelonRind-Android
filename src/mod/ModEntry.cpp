#include "model/HudTransform.h"
#include "runtime/Config.h"
#include "runtime/GameAdapter.h"
#include "runtime/HudRenderer.h"

#include <pl/Config.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <cstdlib>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <chrono>

namespace melonrind {
namespace {

constexpr std::string_view kPrimaryVersion = "1.26.51";

/** How many frames to trace unconditionally after enabling the mod. */
constexpr int kSnapshotLogFrames = 5;

/**
 * Append-only diagnostics written next to the mod's own data directory.
 *
 * The in-game logcat channel is not dependable on every device (some vendor
 * ROMs drop an app's own log records once the game starts flooding logd), while
 * the mod directory stays readable over adb.  The file is truncated once per
 * session so it always describes the current run.
 */
class DiagnosticLog {
public:
    void open(const std::filesystem::path &dataDir) noexcept {
        try {
            std::error_code error;
            std::filesystem::create_directories(dataDir, error);
            auto path = dataDir / "melonrind-diag.log";
            mStream.open(path, std::ios::out | std::ios::trunc);
            mEnabled = mStream.is_open();
            if (mEnabled) {
                mStream << "MelonRind Android diagnostics\n";
                mStream.flush();
            }
        } catch (...) {
            mEnabled = false;
        }
    }

    void close() noexcept {
        std::lock_guard lock(mMutex);
        if (mStream.is_open()) mStream.close();
        mEnabled = false;
    }

    void write(const std::string &message) noexcept {
        try {
            std::lock_guard lock(mMutex);
            if (!mEnabled || !mStream.is_open()) return;
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
            mStream << '[' << now << "] " << message << '\n';
            mStream.flush();
        } catch (...) {
        }
    }

private:
    std::mutex mMutex;
    std::ofstream mStream;
    bool mEnabled{};
};

std::string hexValue(std::uintptr_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << value;
    return stream.str();
}

/**
 * One-line dump of the vanilla bar icon origins as the game reported them.
 *
 * The values are deliberately raw: the launcher overlay draws in view pixels
 * while the renderers lay the bar out in their own space, and this line is what
 * fixes the transform between the two.  `surface` is the overlay's own pixel
 * size, so a screenshot plus this record gives the scale and the origin offset.
 */
std::string describeIcons(const runtime::PlayerSnapshot &snapshot,
                          float surfaceWidth, float surfaceHeight) {
    std::ostringstream line;
    line << "icons surface=" << surfaceWidth << 'x' << surfaceHeight
         << " hudVisible=" << snapshot.hudVisible
         << " hunger=" << snapshot.icons.hungerCount << " [";
    line << std::fixed << std::setprecision(1);
    for (std::uint32_t i = 0; i < snapshot.icons.hungerCount; ++i) {
        if (i != 0) line << ' ';
        line << '(' << snapshot.icons.hunger[i].x << ','
             << snapshot.icons.hunger[i].y << ','
             << snapshot.icons.hunger[i].z << ')';
    }
    line << "] heart=" << snapshot.icons.heartCount << '/'
         << snapshot.icons.heartTotal << " [";
    for (std::uint32_t i = 0; i < snapshot.icons.heartCount; ++i) {
        if (i != 0) line << ' ';
        line << '(' << snapshot.icons.heart[i].x << ','
             << snapshot.icons.heart[i].y << ','
             << snapshot.icons.heart[i].z << ')';
    }
    line << ']';
    return line.str();
}

/**
 * One-line record of the calibrated HUD geometry.
 *
 * `unitScale` is what the overlay multiplies UI units by; `layout` says which
 * vanilla arrangement the origins were recognised as.  `fromWidth` and
 * `fromHeight` are the two independent estimates the Classic fit uses, so a log
 * where they disagree is the signal that the HUD is no longer the layout the
 * constants describe.  Pocket keeps `fromHeight` at its (rejected) Classic
 * value and uses `fromWidth`; the overlay then falls back to the configured
 * scale only when no layout is recognised.
 */
std::string describeHudGeometry(const runtime::PlayerSnapshot &snapshot,
                                const runtime::HudRenderer &renderer) {
    melonrind::HudCalibrationInput input;
    input.hungerOrigin = snapshot.hungerBar.available;
    input.heartOrigin = snapshot.heartBar.available;
    input.hungerX = snapshot.hungerBar.x;
    input.hungerY = snapshot.hungerBar.y;
    input.heartX = snapshot.heartBar.x;
    input.heartY = snapshot.heartBar.y;
    input.surfaceWidth = renderer.surfaceWidth();
    input.surfaceHeight = renderer.surfaceHeight();
    const auto estimate = melonrind::estimateHudUnitScale(input);
    const auto hud = renderer.geometry(snapshot);

    std::ostringstream line;
    line << std::fixed << std::setprecision(2);
    line << "hud origin heart=";
    if (snapshot.heartBar.available) {
        line << '(' << snapshot.heartBar.x << ',' << snapshot.heartBar.y << ')';
    } else {
        line << "<none>";
    }
    line << " hunger=";
    if (snapshot.hungerBar.available) {
        line << '(' << snapshot.hungerBar.x << ',' << snapshot.hungerBar.y
             << ')';
    } else {
        line << "<none>";
    }
    line << " surface=" << renderer.surfaceWidth() << 'x'
         << renderer.surfaceHeight();
    line << " layout=" << melonrind::hudLayoutName(estimate.layout);
    line << " unitScale=" << hud.unitScale << " auto=" << estimate.valid
         << " fromWidth=" << estimate.fromWidth
         << " fromHeight=" << estimate.fromHeight
         << " consistent=" << estimate.consistent
         << " trusted=" << hud.trusted;
    line << " pixel heart=(" << hud.heartOriginX << ',' << hud.heartOriginY
         << ") hunger=(" << hud.hungerOriginX << ',' << hud.hungerOriginY
         << ')';
    line << " mode=" << runtime::renderModeName(renderer.config().renderMode)
         << " overlayReady=" << (hud.valid && hud.trusted);
    return line.str();
}

/** Single-threaded rate limiter for the on-demand diagnostic heartbeat. */
class Heartbeat {
public:
    [[nodiscard]] bool due() noexcept {
        const auto now = std::chrono::steady_clock::now();
        if (mLast.time_since_epoch().count() != 0 &&
            now - mLast < std::chrono::seconds(2)) {
            return false;
        }
        mLast = now;
        return true;
    }

    void reset() noexcept { mLast = {}; }

private:
    std::chrono::steady_clock::time_point mLast{};
};

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
            mDiagnostics.open(mSelf.getDataDir());
            mConfig.emplace();
            if (!mConfig->load()) {
                mSelf.getLogger().error("Failed to load typed configuration");
                mDiagnostics.write("config load failed");
                mConfig.reset();
                return false;
            }
            mConfig->value().normalize();
            if (!mConfig->save()) {
                mSelf.getLogger().error("Failed to persist normalized configuration");
                mConfig.reset();
                return false;
            }
            mDiagnostics.write("load id=" + mSelf.getId() + " version=" +
                               mSelf.getVersion() + " dataDir=" +
                               mSelf.getDataDir().string());
            mRenderer.setConfig(mConfig->value());
            mAdapter.setHudProbeEnabled(mConfig->value().diagnosticMarkers);
            mAdapter.setSnapshotCallback([this](const runtime::PlayerSnapshot &snapshot) {
                if (mEnabled.load(std::memory_order_relaxed)) {
                    // The Java overlay view reports its size after the first
                    // game frame.  Refresh it before the diagnostic lines so
                    // the first usable icon record carries the real surface
                    // instead of the initial 0x0.
                    const auto surface = pl::modmenu::getHudSurfaceSize();
                    mRenderer.setSurfaceSize(surface.width, surface.height);
                    const int index =
                        mLoggedSnapshots.fetch_add(1, std::memory_order_relaxed);
                    // Trace the first few frames unconditionally, then keep a
                    // slow heartbeat while the Diagnostics toggle is on.
                    const bool sampled = index < kSnapshotLogFrames ||
                                         (mRenderer.diagnosticsEnabled() &&
                                          mDiagHeartbeat.due());
                    if (sampled) {
                        std::ostringstream line;
                        line << "snapshot #" << index
                             << " player=" << hexValue(snapshot.playerIdentity)
                             << " hunger=" << snapshot.hunger << "/"
                             << snapshot.maxHunger
                             << " saturation=" << snapshot.saturation.value_or(-1.0f)
                             << " health=" << snapshot.health << "/"
                             << snapshot.maxHealth
                             << " healthAvailable=" << snapshot.healthAvailable
                             << " hudVisible=" << snapshot.hudVisible
                             << " alive=" << snapshot.alive
                             << " creative=" << snapshot.creative
                             << " spectator=" << snapshot.spectator
                             << " food="
                             << (snapshot.mainHandFood ? snapshot.mainHandFood->itemId
                                                       : std::string("<none>"))
                             << " nutrition="
                             << (snapshot.mainHandFood ? snapshot.mainHandFood->nutrition
                                                       : 0)
                             << " saturationModifier="
                             << (snapshot.mainHandFood
                                     ? snapshot.mainHandFood->saturationModifier
                                     : 0.0f)
                             << " alwaysEat="
                             << (snapshot.mainHandFood &&
                                 snapshot.mainHandFood->alwaysEat)
                             << " uncertain="
                             << (snapshot.mainHandFood &&
                                 snapshot.mainHandFood->uncertainEffects)
                             << " attributeCalls=" << mAdapter.attributeHookCalls()
                             << " attributePacketId="
                             << mAdapter.lastAttributePacketId()
                             << " attributeProbe="
                             << mAdapter.lastAttributeProbe();
                        mDiagnostics.write(line.str());
                    }
                    const bool iconsPresent =
                        snapshot.icons.hungerCount != 0 ||
                        snapshot.icons.heartCount != 0;
                    // The first usable icon layout is always recorded, even
                    // with both diagnostic toggles off: it is the measurement
                    // that calibrates the overlay transform.  The geometry line
                    // is repeated once the overlay surface size becomes known,
                    // because the first snapshot can precede the Java view's
                    // onSizeChanged().  After that the lines only repeat every
                    // two seconds while the calibration markers are enabled.
                    const bool firstIcons = iconsPresent &&
                        !mLoggedIcons.exchange(true, std::memory_order_relaxed);
                    const bool surfaceReady =
                        mRenderer.surfaceWidth() > 0.0f &&
                        mRenderer.surfaceHeight() > 0.0f;
                    const bool firstCalibration = iconsPresent && surfaceReady &&
                        !mLoggedCalibration.exchange(true, std::memory_order_relaxed);
                    const bool heartbeatIcons =
                        iconsPresent && mRenderer.markerModeEnabled() &&
                        mIconHeartbeat.due();
                    if (firstIcons || heartbeatIcons) {
                        mDiagnostics.write(describeIcons(
                            snapshot, mRenderer.surfaceWidth(),
                            mRenderer.surfaceHeight()));
                    }
                    if (firstIcons || firstCalibration || heartbeatIcons) {
                        mDiagnostics.write(describeHudGeometry(snapshot, mRenderer));
                        // The transform probe only exists while the calibration
                        // markers are enabled; it records where the renderers
                        // get their screen placement from.
                        if (heartbeatIcons) {
                            const auto probe = mAdapter.hudTransformProbe();
                            if (!probe.empty()) mDiagnostics.write(probe);
                        }
                    }
                    if (snapshot.mainHandFood &&
                        !mLoggedFood.exchange(true, std::memory_order_relaxed)) {
                        mSelf.getLogger().info(
                            "First food snapshot: id={} nutrition={} saturationModifier={} alwaysEat={} uncertain={}",
                            snapshot.mainHandFood->itemId,
                            snapshot.mainHandFood->nutrition,
                            snapshot.mainHandFood->saturationModifier,
                            snapshot.mainHandFood->alwaysEat,
                            snapshot.mainHandFood->uncertainEffects);
                    }
                    if (!mLoggedAttributeHook.exchange(true, std::memory_order_relaxed) &&
                        mAdapter.attributeHookCalls() > 0) {
                        mSelf.getLogger().info(
                            "Attribute hook observed packet id={} (0x1d is UpdateAttributesPacket)",
                            mAdapter.lastAttributePacketId());
                    }
                    static const auto start = std::chrono::steady_clock::now();
                    const auto elapsed = std::chrono::steady_clock::now() - start;
                    const auto seconds = std::chrono::duration<float>(elapsed).count();
                    mRenderer.render(snapshot, seconds);
                }
            });
            mRenderer.setConfigChangedCallback([this](const runtime::Config &config) {
                std::lock_guard lock(mMutex);
                mAdapter.setHudProbeEnabled(config.diagnosticMarkers);
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
            mRenderer.setModId(mSelf.getId());
            mSelf.getLogger().info("Enabling mod id={} version={}",
                                   mSelf.getId(), mSelf.getVersion());
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

            mSelf.getLogger().info(
                "HUD targets: hunger={:#x} installed={} heart={:#x} installed={} attributes={:#x} installed={}",
                mAdapter.hungerRenderTarget(), mAdapter.hungerHookInstalled(),
                mAdapter.heartRenderTarget(), mAdapter.heartHookInstalled(),
                mAdapter.attributeHookTarget(), mAdapter.attributeHookInstalled());
            {
                std::ostringstream line;
                line << "enable targetVersion=" << version
                     << " catalog="
                     << (mAdapter.signatureSet() ? "yes" : "no")
                     << " hungerTarget=" << hexValue(mAdapter.hungerRenderTarget())
                     << " hungerHook=" << mAdapter.hungerHookInstalled()
                     << " heartTarget=" << hexValue(mAdapter.heartRenderTarget())
                     << " heartHook=" << mAdapter.heartHookInstalled()
                     << " attributeTarget="
                     << hexValue(mAdapter.attributeHookTarget())
                     << " attributeHook=" << mAdapter.attributeHookInstalled()
                     << " packetDtorTarget="
                     << hexValue(mAdapter.attributePacketDtorTarget())
                     << " packetDeletingDtorTarget="
                     << hexValue(mAdapter.attributePacketDeletingDtorTarget())
                     << " packetDtorHook="
                     << mAdapter.attributePacketDtorHookInstalled()
                     << " packetDeletingDtorHook="
                     << mAdapter.attributePacketDtorHookInstalled()
                     << " inventoryGetItem="
                     << hexValue(mAdapter.inventoryGetItemTarget())
                     << " gameVersion=" << mAdapter.minecraftVersion();
                mDiagnostics.write(line.str());
            }

            if (!mRenderer.registerModule()) {
                mAdapter.uninstall();
                mSelf.getLogger().error("Failed to register Mod Menu HUD");
                mDiagnostics.write("registerModule failed");
                return false;
            }
            mSelf.getLogger().info("Registered Mod Menu module {} for mod id {}",
                                   runtime::HudRenderer::kModuleId, mSelf.getId());
            mDiagnostics.write(std::string("registerModule ok moduleId=") +
                               std::string(runtime::HudRenderer::kModuleId) +
                               " diagnostics=" +
                               (mRenderer.diagnosticsEnabled() ? "on" : "off") +
                               " markers=" +
                               (mRenderer.markerModeEnabled() ? "on" : "off"));
            mLoggedSnapshots = 0;
            mLoggedFood = false;
            mLoggedAttributeHook = false;
            mLoggedIcons = false;
            mLoggedCalibration = false;
            mDiagHeartbeat.reset();
            mIconHeartbeat.reset();
            mEnabled.store(true, std::memory_order_relaxed);
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
            mEnabled.store(false, std::memory_order_relaxed);
            mLoggedSnapshots = 0;
            mLoggedFood = false;
            mLoggedAttributeHook = false;
            mLoggedIcons = false;
            mLoggedCalibration = false;
            mIconHeartbeat.reset();
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
            mDiagnostics.close();
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
    DiagnosticLog mDiagnostics;
    Heartbeat mDiagHeartbeat;
    Heartbeat mIconHeartbeat;
    std::atomic_bool mEnabled{};
    std::atomic_int mLoggedSnapshots{};
    std::atomic_bool mLoggedFood{};
    std::atomic_bool mLoggedAttributeHook{};
    std::atomic_bool mLoggedIcons{};
    std::atomic_bool mLoggedCalibration{};
};

} // namespace
} // namespace melonrind

PL_REGISTER_MOD(melonrind::ModEntry, melonrind::ModEntry::instance())
