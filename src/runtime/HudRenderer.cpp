#include "runtime/HudRenderer.h"

#include "generated/OverlayArt.h"
#include "model/HudTransform.h"
#include "model/Markers.h"
#include "model/OverlayArt.h"
#include "model/OverlayPlan.h"
#include "model/Preview.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>
#include <utility>

namespace melonrind::runtime {
namespace {

constexpr std::string_view kSaturationImage = "melonrind.android.saturation";
constexpr std::string_view kFoodImage = "melonrind.android.food";
constexpr std::string_view kRegenImage = "melonrind.android.regen";

/**
 * Overlay art families.  Each one is registered once per quantised crop, since
 * a launcher draw command cannot crop or tint an image.
 */
constexpr std::string_view kSaturationArt = "sat";
constexpr std::string_view kPreviewArt = "pre";
constexpr std::string_view kRegenArt = "reg";

float parseFloat(std::string_view value, float fallback) noexcept {
    std::string text(value);
    char *end{};
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return fallback;
    }
    return parsed;
}

bool parseBool(std::string_view value, bool fallback) noexcept {
    if (value == "true" || value == "1" || value == "on") return true;
    if (value == "false" || value == "0" || value == "off") return false;
    return fallback;
}

std::vector<unsigned char> iconPixels(std::uint32_t rgb) {
    constexpr int size = 12;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 4));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int dx = x - size / 2;
            const int dy = y - size / 2;
            const bool inside = dx * dx + dy * dy <= 25;
            auto const offset = static_cast<std::size_t>((y * size + x) * 4);
            pixels[offset + 0] = static_cast<unsigned char>((rgb >> 16) & 0xff);
            pixels[offset + 1] = static_cast<unsigned char>((rgb >> 8) & 0xff);
            pixels[offset + 2] = static_cast<unsigned char>(rgb & 0xff);
            pixels[offset + 3] = inside ? 0xff : 0x00;
        }
    }
    return pixels;
}

std::uint32_t withAlpha(std::uint32_t rgb, float alpha) noexcept {
    auto a = static_cast<std::uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return (a << 24) | (rgb & 0x00ffffffU);
}

/** Calibration marker colours: hunger icons are cyan, hearts are magenta. */
constexpr std::uint32_t kHungerMarkerColor = 0x00e5ff;
constexpr std::uint32_t kHeartMarkerColor = 0xff4dd2;
/** Colours of the calibrated markers, which must land on the vanilla icons. */
constexpr std::uint32_t kHungerCalibratedColor = 0x39ff14;
constexpr std::uint32_t kHeartCalibratedColor = 0xffff00;
/**
 * Raw icon coordinates at or below this magnitude are drawn as-is.  The
 * launcher overlay resolves `x <= -9000` (and `x <= -19000`) as anchor
 * sentinels, so a marker must never be submitted in that range; the game's UI
 * coordinates for a HUD bar are four orders of magnitude smaller anyway.
 */
constexpr float kMarkerCoordinateLimit = 8000.0f;
/** Vanilla HUD icon sprites are 9 UI units square (drawn on an 8-unit pitch). */
constexpr float kIconUnits = 9.0f;

} // namespace

HudRenderer::HudGeometry HudRenderer::geometry(
    const PlayerSnapshot &snapshot) const {
    HudGeometry result;
    melonrind::HudCalibrationInput input;
    input.hungerOrigin = snapshot.hungerBar.available;
    input.heartOrigin = snapshot.heartBar.available;
    input.hungerX = snapshot.hungerBar.x;
    input.hungerY = snapshot.hungerBar.y;
    input.heartX = snapshot.heartBar.x;
    input.heartY = snapshot.heartBar.y;
    input.surfaceWidth = mSurfaceWidth;
    input.surfaceHeight = mSurfaceHeight;

    const auto estimate = melonrind::estimateHudUnitScale(input);
    float unitScale = estimate.valid ? estimate.combined : 0.0f;
    const bool pinned = mConfig.unitScale > 0.0f;
    if (pinned) unitScale = mConfig.unitScale;
    if (!std::isfinite(unitScale) || unitScale <= 0.0f) return result;

    result.unitScale = unitScale;
    result.calibrated = estimate.valid;
    result.trusted = (estimate.valid && estimate.consistent) || pinned;
    if (snapshot.hungerBar.available) {
        result.hungerOriginX =
            snapshot.hungerBar.x * unitScale + mConfig.hudPixelOffsetX;
        result.hungerOriginY =
            snapshot.hungerBar.y * unitScale + mConfig.hudPixelOffsetY;
    }
    if (snapshot.heartBar.available) {
        result.heartOriginX =
            snapshot.heartBar.x * unitScale + mConfig.hudPixelOffsetX;
        result.heartOriginY =
            snapshot.heartBar.y * unitScale + mConfig.hudPixelOffsetY;
    }
    result.valid = snapshot.hungerBar.available || snapshot.heartBar.available;
    return result;
}

bool HudRenderer::registerImages() {
    // The launcher bridge keeps registered images for the lifetime of the
    // process and rejects a duplicate id, while unregisterModule() does not
    // release them.  Re-registering on every enable would therefore fail and
    // abort the whole module registration, so the images are published once.
    static bool published = false;
    if (published) return true;
    mImagePixels.clear();
    mImagePixels.reserve(3);
    mImagePixels.push_back(iconPixels(0xe3b341));
    if (!pl::modmenu::registerImage(kSaturationImage, mImagePixels.back(), 12, 12)) {
        mImagePixels.clear();
        return false;
    }
    mImagePixels.push_back(iconPixels(0x69c8ff));
    if (!pl::modmenu::registerImage(kFoodImage, mImagePixels.back(), 12, 12)) {
        mImagePixels.clear();
        return false;
    }
    mImagePixels.push_back(iconPixels(0x68db85));
    if (!pl::modmenu::registerImage(kRegenImage, mImagePixels.back(), 12, 12)) {
        mImagePixels.clear();
        return false;
    }
    if (!registerOverlayImages()) return false;
    published = true;
    return true;
}

std::string HudRenderer::overlayImageId(std::string_view art,
                                        const melonrind::IconCrop &crop) const {
    return std::string(kModuleId) + ".ov." + std::string(art) + "." +
           melonrind::iconCropKey(crop);
}

bool HudRenderer::registerOverlayImages() {
    // Every fill the overlay can draw is one of the pre-quantised crops, so the
    // whole set is published up front: the launcher creates a bitmap lazily, and
    // only the crops that are actually painted ever get drawn.
    const auto crops = melonrind::registeredIconCrops();
    const std::pair<std::string_view, std::span<const unsigned char>> arts[3] = {
        {kSaturationArt, melonrind::generated::kSaturationArt},
        {kPreviewArt, melonrind::generated::kFoodPreviewArt},
        {kRegenArt, melonrind::generated::kRegenArt},
    };
    for (auto const &[art, pixels] : arts) {
        for (auto const &crop : crops) {
            const auto image =
                melonrind::cropOverlayArt(pixels, crop.left, crop.right);
            if (image.empty()) return false;
            if (!pl::modmenu::registerImage(overlayImageId(art, crop),
                                            image.pixels, image.width,
                                            image.height)) {
                return false;
            }
        }
    }
    return true;
}

bool HudRenderer::registerModule() {
    if (mRegistered) return true;
    mConfig.normalize();
    if (!registerImages()) return false;

    auto builder = pl::modmenu::ModuleBuilder(std::string(kModuleId), "MelonRind Android HUD")
                       .modId(mModId)
                       .description("AppleSkin-like hunger, saturation and regeneration preview.")
                       .defaultEnabled(mConfig.enabled)
                       .onToggle([this](std::string_view moduleId, bool enabled) {
                           handleToggle(moduleId, enabled);
                       })
                       .config("renderMode", "Render Mode",
                               pl::modmenu::ConfigType::Radio,
                               std::string(renderModeName(mConfig.renderMode)),
                               "overlay,panel")
                       .config("showSaturation", "Show Saturation",
                               pl::modmenu::ConfigType::Toggle,
                               mConfig.showSaturation ? "true" : "false")
                       .config("showFoodPreview", "Show Food Preview",
                               pl::modmenu::ConfigType::Toggle,
                               mConfig.showFoodPreview ? "true" : "false")
                       .config("showRegeneration", "Show Regeneration",
                               pl::modmenu::ConfigType::Toggle,
                               mConfig.showRegeneration ? "true" : "false")
                       .config("pulse", "Pulse Preview",
                               pl::modmenu::ConfigType::Toggle,
                               mConfig.pulse ? "true" : "false")
                       .config("opacity", "Opacity", pl::modmenu::ConfigType::SliderFloat,
                               std::to_string(mConfig.opacity), "0", "1")
                       .config("scale", "Scale", pl::modmenu::ConfigType::SliderFloat,
                               std::to_string(mConfig.scale), "0.5", "3")
                       .config("saturationCap", "Saturation Cap", pl::modmenu::ConfigType::Radio,
                               std::string(saturationCapName(mConfig.saturationCap)),
                               "attributeMaximum,hungerAfterEating")
                       .config("estimateUnknownRule", "Estimate Unknown Rule",
                               pl::modmenu::ConfigType::Toggle,
                               mConfig.estimateUnknownRule ? "true" : "false")
                       .config("diagnostics", "Diagnostics", pl::modmenu::ConfigType::Toggle,
                               mConfig.diagnostics ? "true" : "false")
                       .config("diagnosticMarkers", "Diagnostic Markers",
                               pl::modmenu::ConfigType::Toggle,
                               mConfig.diagnosticMarkers ? "true" : "false")
                       .config("unitScale", "Unit Scale (0 = auto)",
                               pl::modmenu::ConfigType::SliderFloat,
                               std::to_string(mConfig.unitScale), "0", "16")
                       .config("hudPixelOffsetX", "HUD Pixel Offset X",
                               pl::modmenu::ConfigType::SliderFloat,
                               std::to_string(mConfig.hudPixelOffsetX), "-64",
                               "64")
                       .config("hudPixelOffsetY", "HUD Pixel Offset Y",
                               pl::modmenu::ConfigType::SliderFloat,
                               std::to_string(mConfig.hudPixelOffsetY), "-64",
                               "64")
                       .onConfigChanged([this](std::string_view moduleId,
                                               std::string_view key,
                                               std::string_view value) {
                           handleConfig(moduleId, key, value);
                       });
    mRegistered = builder.registerModule();
    return mRegistered;
}

void HudRenderer::setModId(std::string modId) {
    if (mRegistered || modId.empty()) return;
    mModId = std::move(modId);
}

void HudRenderer::unregisterModule() noexcept {
    if (!mRegistered) return;
    clear();
    pl::modmenu::unregisterModule(kModuleId);
    mRegistered = false;
    mImagePixels.clear();
}

void HudRenderer::setConfig(Config config) noexcept {
    config.normalize();
    mConfig = config;
}

void HudRenderer::setConfigChangedCallback(
    std::function<void(const Config &)> callback) {
    mConfigChanged = std::move(callback);
}

void HudRenderer::setSurfaceSize(float width, float height) noexcept {
    mSurfaceWidth = std::isfinite(width) && width > 0.0f ? width : 0.0f;
    mSurfaceHeight = std::isfinite(height) && height > 0.0f ? height : 0.0f;
}

float HudRenderer::baseX(float contentWidth) const noexcept {
    if (mConfig.hudPosX > -10000.0f) return mConfig.hudPosX;
    return mSurfaceWidth > contentWidth ? (mSurfaceWidth - contentWidth) * 0.5f : 16.0f;
}

float HudRenderer::baseY(float contentHeight) const noexcept {
    if (mConfig.hudPosY > -10000.0f) return mConfig.hudPosY;
    return mSurfaceHeight > contentHeight + 16.0f ? mSurfaceHeight - contentHeight - 16.0f : 16.0f;
}

std::vector<pl::modmenu::DrawCommand> HudRenderer::buildCommands(
    const PlayerSnapshot &rawSnapshot, float timeSeconds) const {
    std::vector<pl::modmenu::DrawCommand> commands;
    if (!mConfig.enabled || !rawSnapshot.hudVisible || !rawSnapshot.alive ||
        rawSnapshot.creative || rawSnapshot.spectator) {
        return commands;
    }

    auto snapshot = sanitizeSnapshot(rawSnapshot);
    struct Row {
        float value{};
        float maximum{};
        std::uint32_t color{};
        std::string_view image{};
        std::optional<std::pair<float, float>> preview;
        std::uint32_t previewColor{};
        std::optional<std::pair<float, float>> previewUpper;
        std::uint32_t previewUpperColor{};
    };
    std::vector<Row> rows;
    rows.reserve(3);

    const auto food = snapshot.mainHandFood;
    std::optional<melonrind::MealPreview> meal;
    if (food) {
        meal = melonrind::previewMeal(
            {snapshot.hunger, snapshot.maxHunger, snapshot.saturation,
             snapshot.maxSaturation},
            {food->nutrition, food->saturationModifier, food->alwaysEat,
             food->uncertainEffects},
            mConfig.saturationCap == SaturationCap::AttributeMaximum
                ? melonrind::SaturationCap::AttributeMaximum
                : melonrind::SaturationCap::HungerAfterEating);
    }

    rows.push_back({snapshot.hunger, snapshot.maxHunger, 0xd9a441, kFoodImage,
                    (mConfig.showFoodPreview && meal &&
                     meal->hungerAfter > meal->hungerBefore)
                        ? std::optional{std::pair{meal->hungerBefore,
                                                   meal->hungerAfter}}
                        : std::nullopt,
                    0x69c8ff, std::nullopt, 0x9ce6a8});

    if (mConfig.showSaturation && snapshot.saturation) {
        rows.push_back({*snapshot.saturation, snapshot.maxSaturation, 0xe3b341,
                        kSaturationImage,
                        (mConfig.showFoodPreview && meal &&
                         meal->saturationBefore && meal->saturationAfter &&
                         *meal->saturationAfter > *meal->saturationBefore)
                            ? std::optional{std::pair{*meal->saturationBefore,
                                                       *meal->saturationAfter}}
                            : std::nullopt,
                        0x69c8ff, std::nullopt, 0x9ce6a8});
    }

    if (mConfig.showRegeneration && meal && snapshot.hasFiniteHealth()) {
        melonrind::RegenContext context{
            snapshot.health,
            snapshot.maxHealth,
            static_cast<melonrind::RuleState>(snapshot.naturalRegeneration),
            snapshot.peaceful,
            snapshot.regenerationSuppressed || meal->uncertainEffects,
            mConfig.estimateUnknownRule,
        };
        if (auto regen = melonrind::previewRegeneration(*meal, context)) {
            if (regen->highHp > 0.0f) {
                rows.push_back({snapshot.health, snapshot.maxHealth, 0xc75f68,
                                kRegenImage,
                                std::optional{std::pair{
                                    snapshot.health,
                                    snapshot.health + regen->lowHp}},
                                0x68db85,
                                std::nullopt,
                                0x9ce6a8});
                if (regen->highHp > regen->lowHp) {
                    rows.back().previewUpper = std::pair{
                        snapshot.health + regen->lowHp,
                        snapshot.health + regen->highHp};
                }
            }
        }
    }

    if (rows.empty()) return commands;
    constexpr float contentWidthBase = 124.0f;
    constexpr float rowHeightBase = 14.0f;
    constexpr float iconSizeBase = 10.0f;
    const float scale = mConfig.scale;
    const float rowHeight = rowHeightBase * scale;
    const float iconSize = iconSizeBase * scale;
    const float contentHeight = rowHeight * static_cast<float>(rows.size());
    const float surfaceWidth = mSurfaceWidth;
    const float contentWidth = surfaceWidth > 0.0f
                                   ? std::min(contentWidthBase * scale,
                                             std::max(1.0f, surfaceWidth - 8.0f))
                                   : contentWidthBase * scale;
    float x = baseX(contentWidth);
    float y = baseY(contentHeight);
    if (surfaceWidth > 0.0f) {
        x = std::clamp(x, 0.0f, std::max(0.0f, surfaceWidth - contentWidth));
    }
    if (mSurfaceHeight > 0.0f) {
        y = std::clamp(y, 0.0f, std::max(0.0f, mSurfaceHeight - contentHeight));
    }
    const float alpha = mConfig.pulse
                            ? mConfig.opacity * (0.78f + 0.22f *
                                                   std::sin(timeSeconds * 4.0f))
                            : mConfig.opacity;
    const float barX = x + iconSize + 5.0f * scale;
    const float barWidth = std::max(1.0f, contentWidth - iconSize - 5.0f * scale);

    for (auto const &row : rows) {
        if (!std::isfinite(row.value) || !std::isfinite(row.maximum) ||
            row.maximum <= 0.0f) {
            y += rowHeight;
            continue;
        }
        const float fraction = std::clamp(row.value / row.maximum, 0.0f, 1.0f);
        commands.push_back({.type = pl::modmenu::DrawCommandType::Image,
                             .x = x,
                             .y = y + (rowHeight - iconSize) * 0.5f,
                             .w = iconSize,
                             .h = iconSize,
                             .color = withAlpha(0xffffff, alpha),
                             .text = {},
                             .fontId = {},
                             .imageId = std::string(row.image)});
        commands.push_back({.type = pl::modmenu::DrawCommandType::RectFilled,
                             .x = barX,
                             .y = y + 3.0f * scale,
                             .w = barWidth,
                             .h = 8.0f * scale,
                             .color = withAlpha(0x26303a, alpha * 0.7f),
                             .text = {},
                             .fontId = {},
                             .imageId = {}});
        commands.push_back({.type = pl::modmenu::DrawCommandType::RectFilled,
                             .x = barX,
                             .y = y + 3.0f * scale,
                             .w = barWidth * fraction,
                             .h = 8.0f * scale,
                             .color = withAlpha(row.color, alpha),
                             .text = {},
                             .fontId = {},
                             .imageId = {}});
        auto drawPreview = [&](std::optional<std::pair<float, float>> range,
                               std::uint32_t color) {
            if (!range || range->second <= range->first) return;
            const float start = std::clamp(range->first / row.maximum, 0.0f, 1.0f);
            const float end = std::clamp(range->second / row.maximum, 0.0f, 1.0f);
            if (end <= start) return;
            commands.push_back({.type = pl::modmenu::DrawCommandType::RectFilled,
                                 .x = barX + barWidth * start,
                                 .y = y + 3.0f * scale,
                                 .w = barWidth * (end - start),
                                 .h = 8.0f * scale,
                                 .color = withAlpha(color, alpha),
                                 .text = {},
                                 .fontId = {},
                                 .imageId = {}});
        };
        drawPreview(row.preview, row.previewColor);
        drawPreview(row.previewUpper, row.previewUpperColor);
        y += rowHeight;
    }
    return commands;
}

std::vector<pl::modmenu::DrawCommand> HudRenderer::buildDiagnosticCommands(
    float timeSeconds) const {
    // Three fixed, fully filled rows.  Unlike buildCommands this ignores the
    // snapshot entirely, so it proves the renderer, the image registrations and
    // the launcher overlay bridge are alive even when no usable player state
    // was read.
    std::vector<pl::modmenu::DrawCommand> commands;
    constexpr float contentWidthBase = 124.0f;
    constexpr float rowHeightBase = 14.0f;
    constexpr float iconSizeBase = 10.0f;
    const float scale = mConfig.scale;
    const float rowHeight = rowHeightBase * scale;
    const float iconSize = iconSizeBase * scale;
    const float contentHeight = rowHeight * 3.0f;
    const float surfaceWidth = mSurfaceWidth;
    const float contentWidth = surfaceWidth > 0.0f
                                   ? std::min(contentWidthBase * scale,
                                             std::max(1.0f, surfaceWidth - 8.0f))
                                   : contentWidthBase * scale;
    float x = baseX(contentWidth);
    float y = baseY(contentHeight);
    if (surfaceWidth > 0.0f) {
        x = std::clamp(x, 0.0f, std::max(0.0f, surfaceWidth - contentWidth));
    }
    if (mSurfaceHeight > 0.0f) {
        y = std::clamp(y, 0.0f, std::max(0.0f, mSurfaceHeight - contentHeight));
    }
    const float alpha = mConfig.opacity;
    const float barX = x + iconSize + 5.0f * scale;
    const float barWidth = std::max(1.0f, contentWidth - iconSize - 5.0f * scale);
    constexpr std::uint32_t colors[3] = {0xe3b341, 0x69c8ff, 0xc75f68};
    constexpr std::string_view images[3] = {
        kSaturationImage, kFoodImage, kRegenImage};
    for (int row = 0; row < 3; ++row) {
        commands.push_back({.type = pl::modmenu::DrawCommandType::Image,
                            .x = x,
                            .y = y + (rowHeight - iconSize) * 0.5f,
                            .w = iconSize,
                            .h = iconSize,
                            .color = withAlpha(0xffffff, alpha),
                            .text = {},
                            .fontId = {},
                            .imageId = std::string(images[row])});
        commands.push_back({.type = pl::modmenu::DrawCommandType::RectFilled,
                            .x = barX,
                            .y = y + 3.0f * scale,
                            .w = barWidth,
                            .h = 8.0f * scale,
                            .color = withAlpha(colors[row], alpha),
                            .text = {},
                            .fontId = {},
                            .imageId = {}});
        y += rowHeight;
    }
    (void)timeSeconds;
    return commands;
}

std::vector<pl::modmenu::DrawCommand> HudRenderer::buildMarkerCommands(
    const PlayerSnapshot &rawSnapshot) const {
    std::vector<pl::modmenu::DrawCommand> commands;
    const auto snapshot = sanitizeSnapshot(rawSnapshot);
    const auto &icons = snapshot.icons;
    if (icons.hungerCount == 0 && icons.heartCount == 0) return commands;

    const auto hud = geometry(snapshot);
    commands.reserve(static_cast<std::size_t>(icons.hungerCount +
                                              icons.heartCount) *
                     10);

    auto const addCross = [&commands](float x, float y, std::uint32_t color,
                                      float arm, float dot) {
        commands.push_back({.type = pl::modmenu::DrawCommandType::RectFilled,
                            .x = x - dot,
                            .y = y - dot,
                            .w = dot * 2.0f,
                            .h = dot * 2.0f,
                            .color = withAlpha(color, 1.0f),
                            .text = {},
                            .fontId = {},
                            .imageId = {}});
        commands.push_back({.type = pl::modmenu::DrawCommandType::Line,
                            .x = x - arm,
                            .y = y,
                            .w = arm * 2.0f,
                            .h = 0.0f,
                            .color = withAlpha(color, 1.0f),
                            .size = 1.0f,
                            .text = {},
                            .fontId = {},
                            .imageId = {}});
        commands.push_back({.type = pl::modmenu::DrawCommandType::Line,
                            .x = x,
                            .y = y - arm,
                            .w = 0.0f,
                            .h = arm * 2.0f,
                            .color = withAlpha(color, 1.0f),
                            .size = 1.0f,
                            .text = {},
                            .fontId = {},
                            .imageId = {}});
    };
    // A marker drawn at the raw value: it lands wherever the renderer's own
    // coordinate space starts, which is what made the unit scale measurable.
    auto const addRawMarker = [&addCross](const melonrind::MarkerPoint &point,
                                          std::uint32_t color) {
        addCross(point.x, point.y, color, 7.0f, 2.0f);
    };
    // The calibrated marker is what has to sit exactly on the vanilla icon: a
    // dot at the sprite's top-left corner plus its 9-unit outline.
    auto const addCalibratedMarker = [&commands, &addCross, &hud](
                                         float x, float y, std::uint32_t color) {
        addCross(x, y, color, 6.0f, 2.0f);
        const float size = kIconUnits * hud.unitScale;
        const float box[4][4] = {
            {x, y, size, 0.0f},
            {x, y + size, size, 0.0f},
            {x, y, 0.0f, size},
            {x + size, y, 0.0f, size},
        };
        for (auto const &line : box) {
            commands.push_back({.type = pl::modmenu::DrawCommandType::Line,
                                .x = line[0],
                                .y = line[1],
                                .w = line[2],
                                .h = line[3],
                                .color = withAlpha(color, 1.0f),
                                .size = 1.0f,
                                .text = {},
                                .fontId = {},
                                .imageId = {}});
        }
    };

    auto const drawBar = [&](const IconPosition *source, std::uint32_t count,
                             bool hunger, std::uint32_t rawColor,
                             std::uint32_t calibratedColor) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto &icon = source[i];
            const melonrind::MarkerPoint point{icon.x, icon.y};
            const std::span<const melonrind::MarkerPoint> single{&point, 1};
            if (melonrind::usableMarkerPoints(single, kMarkerCoordinateLimit)
                    .empty()) {
                continue;
            }
            addRawMarker(point, rawColor);
            if (!hud.valid) continue;
            const auto pixel = hud.iconPixel(icon, hunger);
            addCalibratedMarker(pixel.first, pixel.second, calibratedColor);
        }
    };
    drawBar(icons.hunger.data(), icons.hungerCount, true, kHungerMarkerColor,
            kHungerCalibratedColor);
    drawBar(icons.heart.data(), icons.heartCount, false, kHeartMarkerColor,
            kHeartCalibratedColor);
    return commands;
}

std::vector<pl::modmenu::DrawCommand> HudRenderer::buildOverlayCommands(
    const PlayerSnapshot &rawSnapshot, float timeSeconds) const {
    std::vector<pl::modmenu::DrawCommand> commands;
    if (!mConfig.enabled || !rawSnapshot.hudVisible || !rawSnapshot.alive ||
        rawSnapshot.creative || rawSnapshot.spectator) {
        return commands;
    }
    auto const snapshot = sanitizeSnapshot(rawSnapshot);
    auto const hud = geometry(snapshot);
    if (!hud.valid || !hud.trusted) return commands;
    if (!snapshot.hasFiniteNutrition()) return commands;

    const float iconSize = melonrind::kOverlayIconUnits * hud.unitScale;
    if (!std::isfinite(iconSize) || iconSize <= 0.0f) return commands;
    // Same pulse the desktop mod uses: the preview and both regeneration bands
    // breathe, while the saturation a player already has stays steady.
    const float pulse = mConfig.pulse
                            ? 0.5f + 0.3f * std::sin(timeSeconds * 4.0f)
                            : 0.65f;
    const float base = mConfig.opacity;

    auto const emit = [&](const melonrind::IconDraw &draw, bool hunger,
                          std::string_view art, float alpha) {
        if (!std::isfinite(alpha) || alpha <= 0.004f) return;
        const melonrind::IconCrop crop =
            melonrind::canonicalIconCrop(draw.crop.left, draw.crop.right);
        if (crop.empty()) return;
        const auto &icon = hunger ? snapshot.icons.hunger[draw.index]
                                  : snapshot.icons.heart[draw.index];
        const auto origin = hud.iconPixel(icon, hunger);
        commands.push_back({.type = pl::modmenu::DrawCommandType::Image,
                            .x = origin.first + crop.left * iconSize,
                            .y = origin.second,
                            .w = (crop.right - crop.left) * iconSize,
                            .h = iconSize,
                            .color = withAlpha(0xffffff, alpha),
                            .text = {},
                            .fontId = {},
                            .imageId = overlayImageId(art, crop)});
    };
    auto const emitAll = [&](float before, float after, std::size_t iconCount,
                             bool hunger, bool reverseFill, std::string_view art,
                             float alpha) {
        for (auto const &draw :
             melonrind::planIconDraws(before, after, iconCount, reverseFill)) {
            emit(draw, hunger, art, alpha);
        }
    };

    const auto food = snapshot.mainHandFood;
    std::optional<melonrind::MealPreview> meal;
    if (food) {
        meal = melonrind::previewMeal(
            {snapshot.hunger, snapshot.maxHunger, snapshot.saturation,
             snapshot.maxSaturation},
            {food->nutrition, food->saturationModifier, food->alwaysEat,
             food->uncertainEffects},
            mConfig.saturationCap == SaturationCap::AttributeMaximum
                ? melonrind::SaturationCap::AttributeMaximum
                : melonrind::SaturationCap::HungerAfterEating);
    }

    const std::size_t hungerIcons = snapshot.icons.hungerCount;
    const std::size_t heartIcons =
        snapshot.maxHealth > 0.0f
            ? std::min<std::size_t>(
                  snapshot.icons.heartCount,
                  static_cast<std::size_t>(
                      std::ceil(snapshot.maxHealth / 2.0f)))
            : 0;

    // Held-food preview first: it is the "what this meal will add" ghost, and
    // everything else is painted on top of it.
    if (mConfig.showFoodPreview && meal && meal->hungerAfter > meal->hungerBefore) {
        emitAll(meal->hungerBefore, meal->hungerAfter, hungerIcons, true, true,
                kPreviewArt, base * pulse);
    }
    if (mConfig.showSaturation && snapshot.saturation) {
        emitAll(0.0f, *snapshot.saturation, hungerIcons, true, true,
                kSaturationArt, base);
        if (mConfig.showFoodPreview && meal && meal->saturationBefore &&
            meal->saturationAfter &&
            *meal->saturationAfter > *meal->saturationBefore) {
            emitAll(*meal->saturationBefore, *meal->saturationAfter, hungerIcons,
                    true, true, kSaturationArt, base * pulse);
        }
    }
    if (mConfig.showRegeneration && meal && snapshot.hasFiniteHealth() &&
        heartIcons != 0) {
        melonrind::RegenContext context{
            snapshot.health,
            snapshot.maxHealth,
            static_cast<melonrind::RuleState>(snapshot.naturalRegeneration),
            snapshot.peaceful,
            snapshot.regenerationSuppressed || meal->uncertainEffects,
            mConfig.estimateUnknownRule,
        };
        if (auto regen = melonrind::previewRegeneration(*meal, context)) {
            const float hp = snapshot.health;
            if (regen->lowHp > 0.0f) {
                emitAll(hp, hp + regen->lowHp, heartIcons, false, false,
                        kRegenArt, base * pulse);
            }
            if (regen->highHp > regen->lowHp) {
                // The uncertain extra healing stays dimmer, but visible over the
                // empty hearts it is drawn on.
                emitAll(hp + regen->lowHp, hp + regen->highHp, heartIcons, false,
                        false, kRegenArt, base * pulse * 0.6f);
            }
        }
    }
    return commands;
}

void HudRenderer::render(const PlayerSnapshot &snapshot, float timeSeconds) {
    if (!mRegistered) return;
    const auto surface = pl::modmenu::getHudSurfaceSize();
    setSurfaceSize(surface.width, surface.height);
    if (mConfig.enabled && mConfig.diagnosticMarkers) {
        // Calibration mode deliberately submits only the crosses: the regular
        // panel sits on top of the vanilla hunger bar and would hide exactly the
        // pixels the calibration screenshot has to measure.
        pl::modmenu::submitDrawCommands(kModuleId, buildMarkerCommands(snapshot));
        return;
    }
    if (!mConfig.enabled || !snapshot.hudVisible) {
        // Diagnostics keeps a fixed marker on screen instead of clearing, so a
        // suppressed HUD is immediately distinguishable from a broken one.
        if (mConfig.enabled && mConfig.diagnostics) {
            pl::modmenu::submitDrawCommands(kModuleId,
                                            buildDiagnosticCommands(timeSeconds));
        } else {
            clear();
        }
        return;
    }
    // The overlay needs the calibrated bar geometry; without it the compact
    // panel is still drawn, so a HUD the calibration cannot describe (an
    // unsupported client, a resource pack that moved the bars) degrades to
    // something readable instead of nothing.
    const auto hud = geometry(snapshot);
    const bool overlayReady =
        mConfig.renderMode == RenderMode::Overlay && hud.valid && hud.trusted &&
        (snapshot.icons.hungerCount != 0 || snapshot.icons.heartCount != 0);
    auto commands = overlayReady ? buildOverlayCommands(snapshot, timeSeconds)
                                 : buildCommands(snapshot, timeSeconds);
    if (commands.empty() && !overlayReady && mConfig.diagnostics) {
        commands = buildDiagnosticCommands(timeSeconds);
    }
    pl::modmenu::submitDrawCommands(kModuleId, commands);
}

void HudRenderer::clear() noexcept {
    if (!mRegistered) return;
    const std::vector<pl::modmenu::DrawCommand> empty;
    pl::modmenu::submitDrawCommands(kModuleId, empty);
}

std::vector<HudEditorElement> HudRenderer::editorElements() const {
    const float width = 124.0f * mConfig.scale;
    const float height = 14.0f * mConfig.scale * 3.0f;
    return {{std::string(kModuleId), "MelonRind HUD", baseX(width), baseY(height),
             width, height, 0x07U, 8.0f}};
}

void HudRenderer::applyEditorPosition(float x, float y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    mConfig.hudPosX = x;
    mConfig.hudPosY = y;
    mConfig.normalize();
    notifyConfigChanged();
}

void HudRenderer::handleToggle(std::string_view moduleId, bool enabled) {
    if (moduleId != kModuleId) return;
    mConfig.enabled = enabled;
    if (!enabled) clear();
    notifyConfigChanged();
}

void HudRenderer::handleConfig(std::string_view moduleId, std::string_view key,
                               std::string_view value) {
    if (moduleId != kModuleId) return;
    if (key == "showSaturation") mConfig.showSaturation = parseBool(value, mConfig.showSaturation);
    else if (key == "showFoodPreview") mConfig.showFoodPreview = parseBool(value, mConfig.showFoodPreview);
    else if (key == "showRegeneration") mConfig.showRegeneration = parseBool(value, mConfig.showRegeneration);
    else if (key == "pulse") mConfig.pulse = parseBool(value, mConfig.pulse);
    else if (key == "estimateUnknownRule") mConfig.estimateUnknownRule = parseBool(value, mConfig.estimateUnknownRule);
    else if (key == "diagnostics") mConfig.diagnostics = parseBool(value, mConfig.diagnostics);
    else if (key == "diagnosticMarkers") mConfig.diagnosticMarkers = parseBool(value, mConfig.diagnosticMarkers);
    else if (key == "unitScale") mConfig.unitScale = parseFloat(value, mConfig.unitScale);
    else if (key == "hudPixelOffsetX") mConfig.hudPixelOffsetX = parseFloat(value, mConfig.hudPixelOffsetX);
    else if (key == "hudPixelOffsetY") mConfig.hudPixelOffsetY = parseFloat(value, mConfig.hudPixelOffsetY);
    else if (key == "opacity") mConfig.opacity = parseFloat(value, mConfig.opacity);
    else if (key == "scale") mConfig.scale = parseFloat(value, mConfig.scale);
    else if (key == "saturationCap") mConfig.saturationCap = saturationCapFromName(value);
    else if (key == "renderMode") mConfig.renderMode = renderModeFromName(value);
    mConfig.normalize();
    notifyConfigChanged();
}

void HudRenderer::notifyConfigChanged() {
    if (mConfigChanged) mConfigChanged(mConfig);
}

} // namespace melonrind::runtime

