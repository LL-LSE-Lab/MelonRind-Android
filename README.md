# MelonRind Android

MelonRind Android is a client-side AppleSkin-style nutrition HUD for
LeviLaunchroid. The first development release is `0.1.0-dev.1`.

## Current scope

- ARM64 Android native mod using `preloader-android`.
- Minecraft Bedrock `1.26.50.x`, `1.26.51.x` and `1.26.52.x` manifest gates.
- Saturation, held-food hunger/saturation preview, and regeneration preview
  model code.
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
