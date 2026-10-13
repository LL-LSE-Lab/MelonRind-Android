# MelonRind Android

MelonRind Android is a client-side AppleSkin-style nutrition HUD for
LeviLaunchroid. The first development release is `0.1.0-dev.1`.

## Current scope

- ARM64 Android native mod using `preloader-android`.
- Minecraft Bedrock `1.26.50.x`, `1.26.51.x` and `1.26.52.x` manifest gates.
- AppleSkin-style overlay: saturation, held-food hunger/saturation preview and
  regeneration preview painted directly over the vanilla bars.
- Compact panel output kept as a fallback when the calibrated bar geometry is
  not available.
- Mod Menu configuration and draw-command HUD output.

The supplied Minecraft 1.26.51.1 ARM64 APK has been inspected. The adapter
uses the validated hunger and heart renderer slot 17 hooks, the Inventory
slot 7 accessor, and the food component interface slots 2-4. The resulting
snapshot carries hunger, synchronized saturation, health and selected-food
values; unavailable fields remain unknown and do not produce a fake preview.

## ARM64 layout notes

The libc++/Itanium layout of the pinned `libminecraftpe.so` differs from the
MSVC layout that LeviLamina's generated headers describe. The values below were
confirmed against the shipped library (disassembly plus on-device traces):

- `::HudHungerRenderer`: `mHasLoadedTextures` at `+0x10`, `mShouldRender` at
  `+0x11`, `mUseHungerEffect` at `+0x12`, `mNumFullIcons` at `+0x18`,
  `mNumHalfIcons` at `+0x20`. `+0x14..+0x17` is padding — reading it as a
  visibility flag suppresses every draw command.
- `::std::string` is libc++'s: the discrimination byte is at `+0`, a short
  string keeps its payload at `+1`, a long string stores its size at `+8` and
  its heap pointer at `+0x10`.
- `::Item`: `mDescriptionId` at `+0x90`, `mFullName` (a `HashedString`) at
  `+0xe8` (its string at `+0xf0`), `getFood()` at vtable slot 26.
- `AttributeData` is 88 bytes (`0x58`) — six floats, a `HashedString`
  (8 + 24 + 8) and a `std::vector` — not the 96 bytes of the desktop layout.
- `UpdateAttributesPacket` keeps its payload at `+0x30`; `mSerializationMode`
  sits at `+0x58`, and the attribute vector is at payload `+8`.
- `HashedString::computeHash` multiplies before folding the byte in
  (`hash = byte ^ (prime * hash)`), the opposite order from FNV-1a.
- The Preloader's RTTI/vtable resolver scans `.data.rel.ro` for a word equal to
  the typeinfo address and accepts the first candidate with a non-zero slot.
  For `UpdateAttributesPacket` several candidates match, so the same query can
  return a slot one entry too far. Object offsets for that packet are therefore
  addressed through the fixed file offsets in the catalog, with the module base
  derived from the already-validated hunger renderer slot.

### HUD bar renderer layout

Rebasing the generated MSVC headers (`HudHungerRenderer.h`,
`HudHeartRenderer.h`) onto the measured `::MinecraftUICustomRenderer` base size
(`0x0c`) reproduces every field the disassembly touches, which is how the icon
geometry below was derived. Render is vtable slot 17 for both types
(`0x9673048` hunger, `0x966edd8` heart).

| `::HudHungerRenderer` | offset |
| --- | --- |
| `mTickCount` | `+0x0c` |
| `mHasLoadedTextures` / `mShouldRender` / `mUseHungerEffect` / `mAnimatedLastFrame` | `+0x10 / +0x11 / +0x12 / +0x13` |
| `mNumFullIcons` / `mNumHalfIcons` | `+0x18 / +0x20` |
| `mHungerTextures` (6 × 32 bytes) | `+0x28` |
| `mHungerMeshes` (6 × 632 bytes) | `+0xE8` |
| `mIconPosition` `std::array<glm::vec3, 10>` (12-byte stride) | `+0xFB8` |

The renderer passes `mIconPosition` to the icon draw helper three times: the ten
background slots, then `mNumFullIcons` full icons, then `mNumHalfIcons` half
icons starting at `mIconPosition + mNumFullIcons * 12`. The texture index is
`2/3` and `4/5` selected by `mUseHungerEffect`, which matches the declared
`HungerType` enumeration.

| `::HudHeartRenderer` | offset |
| --- | --- |
| `mHasLoadedTextures` / `mShouldRender` | `+0x0c / +0x0d` |
| Seven `HeartIconData` groups (`{int mType; uint64 mOffset; uint64 mCount}`, 24 bytes): background, full/half flash, full/half absorption, full, half | `+0x10` … `+0xB8` |
| `mHeartTextures` (20 × 32 bytes) | `+0xB8` |
| `mHeartMeshes` (20 × 632 bytes) | `+0x338` |
| `mIconPosition` `std::vector<glm::vec3>` (begin/end/capacity) | `+0x3498 / +0x34A0 / +0x34A8` |

`mFullIcon.mCount`/`mHalfIcon.mCount`/`mBackgroundIcon.mCount` are the health
counts the catalog already reads (`0x98`/`0xB0`/`0x20`). The position vector also
receives the absorption icons after the normal-health slots, so a consumer must
cap the range at `ceil(maxHealth / 2)`.

`::UIControl::mCachedPosition` is a `glm::vec2` at `+0x10` with a dirty bit in
the low bit of the `ushort` at `+0x18`; the getter at `0x968bc9c` refreshes the
cache when that bit is set and returns `owner + 0x10`. Both HUD render entry
points call it and use the pair as the translation they place the whole icon
batch at, which is why the icon arrays themselves only carry local offsets.

## HUD calibration

The launcher HUD overlay is a Java view drawn in launcher-view pixels
(`getHudSurfaceSize()`), while the renderers publish their bar layout in UI
units, so an AppleSkin-style overlay needs the transform between the two. Both
render entry points translate their icon batch by
`::UIControl::mCachedPosition` (the `glm::vec2` at `owner + 0x10`, refreshed by
the getter at `0x968bc9c`) and then draw the *local* `mIconPosition` offsets, so

```text
pixel = (ownerCachedPosition + localIconOffset) * unitScale
unitScale = pixels per UI unit
```

The launcher surface is the full-screen overlay view, so its size is the screen
size in pixels, and the vanilla layout fixes two relations that recover
`unitScale` without reading the game's transform matrix:

| relation | value |
| --- | --- |
| health bar owner origin | `centre_x - 101` units |
| hunger bar owner origin | `centre_x + 100` units |
| both owner origins | `virtual_height - 41` units |

`centre_x = surface_width / (2 * unitScale)` and
`virtual_height = surface_height / unitScale`, so the two owner origins give the
virtual width and the shared vertical origin gives the virtual height. The
overlay fits both with least squares on `1 / unitScale` and only trusts the
result when the two baselines agree within 5% (`unitScale = 0` in the
configuration auto-calibrates; a positive value pins it for a HUD a resource
pack has moved, with `hudPixelOffsetX/Y` as a pixel-level nudge).

Measured on-device over a 3168x1440 overlay:

| UI scale setting | owner origins (units) | estimated `unitScale` | icon pitch in the screenshot |
| --- | --- | --- | --- |
| 100% | heart `(163, 199)`, hunger `(364, 199)` | 6.00 | 48 px for 8 units |
| 83% | heart `(216, 247)`, hunger `(417, 247)` | 5.00 | 40 px for 8 units |

Both runs agree with the rendered icon rows to within about one pixel, and the
estimate follows the setting without any per-device constant.

Turn on `diagnosticMarkers` to re-verify: the toggle suppresses the regular
panel, draws a cyan/magenta cross at each *raw* icon origin (which is what makes
the unit space visible) and a calibrated marker at each computed pixel origin -
a green box for hunger, yellow for health - whose 9-unit outline has to hug the
vanilla icon sprite. `tools/analyze_hud_calibration.py` locates those markers in
an `adb shell screencap` frame. Leave the toggle off during normal use.

## Overlay rendering

With the geometry above, the default `renderMode` is `overlay`: the launcher
draws each fill as an image directly on top of the vanilla icon, at

```text
x = ownerOriginX + (localIconX + cropLeft * 9) * unitScale
w = (cropRight - cropLeft) * 9 * unitScale
h = 9 * unitScale
```

`renderMode = panel` keeps the compact panel, and the overlay falls back to it
automatically when the calibration is missing or not self-consistent, so an
unsupported client or a resource pack that moved the bars degrades to something
readable instead of drawing in the wrong place.

**Art.** A launcher draw command has no source rectangle and no tint, so every
fractional fill is a separately registered image. `tools/generate_overlay_art.py`
turns the sibling desktop mod's 9x9 sprites into
`src/runtime/generated/OverlayArt.h` (gold drumstick outline, green heart, plus
the drumstick recoloured pale for the food preview), and the renderer registers
each of them once per quantised crop:

| crop table | entries | used by |
| --- | --- | --- |
| right-anchored fill ladder, 1/16 icon steps | 16 | hunger, saturation (always reach the sprite's right edge) |
| quarter-icon intervals inside one sprite | 6 | food preview, regeneration (hunger and health only move in half-point steps) |

AppleSkin quantises the same way: its saturation sheet ships four pre-cropped
variants per icon and its preview steps in half icons.

**What is painted.**

| bar | range | art | alpha |
| --- | --- | --- | --- |
| hunger | `[0, saturation]` | gold | `opacity` (steady) |
| hunger | `[hungerBefore, hungerAfter]` | pale | `opacity * pulse` |
| hunger | `[saturationBefore, saturationAfter]` | gold | `opacity * pulse` |
| hearts | `[health, health + lowHp]` | green | `opacity * pulse` |
| hearts | `[health + lowHp, health + highHp]` | green | `opacity * pulse * 0.6` |

`pulse = 0.5 + 0.3 * sin(4t)` matches the desktop mod; the regeneration range is
capped at `ceil(maxHealth / 2)` icons so absorption hearts are never overlaid.

**Configuration** (also exposed in the Mod Menu): `renderMode` (`overlay` /
`panel`), `showSaturation`, `showFoodPreview`, `showRegeneration`, `pulse` and
`opacity` drive what is painted; `unitScale` (0 auto-calibrates) and
`hudPixelOffsetX/Y` override the geometry for a HUD the calibration cannot
describe; `diagnosticMarkers` and `diagnostics` are the troubleshooting toggles.

**Known limitations**, all of them art or fidelity details rather than placement:

- The hunger-effect ("rotten") and heart-status variants of the vanilla icons
  are not reproduced; the overlay always uses the neutral art.
- AppleSkin also paints the *empty* icon at `alpha * 0.25` behind a preview;
  that needs the vanilla empty-icon sprite and is not implemented.
- The launcher draws the overlay through an Android `Canvas`, and on this device
  a wide-gamut display shifts saturated colours slightly (pure sRGB yellow
  `#FFFF00` is captured as `(255, 255, 84)`), so the overlay's gold reads a
  little softer than the source art. The vanilla OpenGL HUD is unaffected.
- `unitScale` auto-calibration assumes the vanilla bar layout documented above.
  A resource pack that moves the bars makes the two baselines disagree; the log
  then reports `consistent=0` and the overlay falls back to the panel (or uses
  the `unitScale` value pinned in the configuration).

Saturation is read from `UpdateAttributesPacket` in the packet's destructors
(both the complete-object and deleting entries, because `std::make_shared`
invokes the former directly). Every pointer touched on that path is validated
with `mincore` first: the read can happen while the payload is still being
populated, and `/proc/self/maps` is not dependable inside the game process.

Validate the exact library before changing the 1.26.51 catalog:

```powershell
python .\tools\validate_adapter.py `
  "E:\dev\BDS\dev\temp\mc12651-apk\lib\arm64-v8a\libminecraftpe.so" `
  --sha256 B3BA0C9C5230D39B6921703255C9F375E0A1B221D9D3AC79D85A72B1790380E9
```

## Build

Requirements: Android NDK 28.2, CMake/Ninja, PowerShell, and the
`preloader-android` checkout used by LeviLaunchroid 1.5.26:
`5d108da350ce3e7f13f51415f81e65344ecd5d14`.

```powershell
.\tools\package.ps1 `
  -Ndk "D:\dev\android-ndk-r28c" `
  -PreloaderRoot "E:\dev\BDS\dev\temp\preloader-android-5d108-inspect" `
  -Clean
```

The script runs host model tests, builds `arm64-v8a`, and writes:

```text
dist/arm64-v8a/melonrind-android.levipack
```

Import the package into a selected LeviLaunchroid Minecraft version. Keep the
mod directory id stable as `melonrind-android` so configuration and launcher
state survive updates; the native Mod Menu owner id follows that runtime
directory name.

## Development notes

`src/model` is platform-independent and can be tested on the host. The Android
runtime uses only public Preloader headers. Version-specific ARM64 signatures
belong under `src/runtime/signatures`; each entry must be validated against a
matching `libminecraftpe.so` before enabling native reads.
