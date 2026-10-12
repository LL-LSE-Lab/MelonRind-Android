# MelonRind Android

MelonRind Android is a client-side AppleSkin-style nutrition HUD for
LeviLaunchroid. The first development release is `0.1.0-dev.1`.

## Current scope

- ARM64 Android native mod using `preloader-android`.
- Minecraft Bedrock `1.26.50.x`, `1.26.51.x` and `1.26.52.x` manifest gates.
- Saturation, held-food hunger/saturation preview, and regeneration preview
  model code.
- Mod Menu configuration and draw-command HUD output.

The supplied Minecraft 1.26.51.1 ARM64 APK has been inspected. Its RTTI-backed
`HudHungerRenderer` and `HudHeartRenderer` vtables resolve render slot 17, so
the Android adapter now publishes the vanilla hunger icon count from that
render hook. Attribute, saturation, health and food-component access remain
separate capabilities and stay disabled until their object layouts are
validated. Unresolved capabilities never read guessed offsets or draw fake
player values.

## Build

Requirements: Android NDK 28.2, CMake/Ninja, PowerShell, and a checkout of
`preloader-android` 0.2.2. The launcher checkout can provide that dependency
through `-PreloaderRoot`.

```powershell
.\tools\package.ps1 `
  -Ndk "D:\dev\android-ndk-r30" `
  -PreloaderRoot "E:\dev\BDS\dev\temp\preloader-android-0.2.2-inspect2" `
  -SkipHostTests
```

The script runs host model tests, builds `arm64-v8a`, and writes:

```text
dist/arm64-v8a/melonrind-android.levipack
```

Import the package into a selected LeviLaunchroid Minecraft version. Keep the
mod directory id stable as `melonrind_android` so configuration and launcher
state survive updates.

## Development notes

`src/model` is platform-independent and can be tested on the host. The Android
runtime uses only public Preloader headers. Version-specific ARM64 signatures
belong under `src/runtime/signatures`; each entry must be validated against a
matching `libminecraftpe.so` before enabling native reads.
