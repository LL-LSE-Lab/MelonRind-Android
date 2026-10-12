param(
    [string]$Ndk = "",
    [string]$PreloaderRoot = "",
    [string]$BuildType = "Release",
    [switch]$Clean,
    [switch]$SkipHostTests
)

$ErrorActionPreference = "Stop"
$SourceDir = $PSScriptRoot | Split-Path -Parent
$Abi = "arm64-v8a"
$BuildDir = Join-Path $SourceDir "build-android-$Abi-$BuildType"
$DistDir = Join-Path $SourceDir "dist\$Abi"
$PackageDir = Join-Path $DistDir "melonrind-android"

function Resolve-Ndk([string]$Explicit) {
    $candidates = @()
    if ($Explicit) { $candidates += $Explicit }
    if ($env:ANDROID_NDK_HOME) { $candidates += $env:ANDROID_NDK_HOME }
    if ($env:ANDROID_NDK_ROOT) { $candidates += $env:ANDROID_NDK_ROOT }
    foreach ($sdk in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT) | Where-Object { $_ }) {
        $root = Join-Path $sdk "ndk"
        if (Test-Path $root) {
            $candidates += Get-ChildItem $root -Directory | Sort-Object Name -Descending | ForEach-Object FullName
        }
        $bundle = Join-Path $sdk "ndk-bundle"
        if (Test-Path $bundle) { $candidates += $bundle }
    }
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (Test-Path (Join-Path $candidate "build\cmake\android.toolchain.cmake")) { return (Resolve-Path $candidate).Path }
    }
    throw "Android NDK not found. Pass -Ndk or set ANDROID_NDK_HOME/ANDROID_NDK_ROOT/ANDROID_HOME."
}

function Invoke-Checked([string]$File, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    foreach ($candidate in @(
        "D:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files\CMake\bin\cmake.exe"
    )) {
        if (Test-Path $candidate) { $cmake = $candidate; break }
    }
}
if (-not $cmake) { throw "CMake 4.4+ was not found on PATH or in the standard install paths." }
$ndkPath = Resolve-Ndk $Ndk
$makeProgram = Join-Path $ndkPath "prebuilt\windows-x86_64\bin\make.exe"
if (-not (Test-Path $makeProgram)) { throw "GNU make was not found in the selected NDK: $makeProgram" }
if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -LiteralPath $BuildDir -Recurse -Force }

if (-not $SkipHostTests) {
    $hostDir = Join-Path $SourceDir "build-host"
    Invoke-Checked $cmake -S $SourceDir -B $hostDir -DMELONRIND_BUILD_TESTS=ON
    Invoke-Checked $cmake --build $hostDir --config $BuildType
    $test = Join-Path $hostDir "$BuildType\melonrind_model_tests.exe"
    if (-not (Test-Path $test)) { $test = Join-Path $hostDir "melonrind_model_tests.exe" }
    if (Test-Path $test) { Invoke-Checked $test }
}

$toolchain = Join-Path $ndkPath "build\cmake\android.toolchain.cmake"
$preloader = if ($PreloaderRoot) { $PreloaderRoot } else { Join-Path $SourceDir "third_party\preloader-android" }
if (-not (Test-Path (Join-Path $preloader "include\pl\Mod.hpp"))) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "preloader-android 0.2.2 is missing and git is unavailable. Pass -PreloaderRoot."
    }
    New-Item -ItemType Directory -Path (Split-Path $preloader -Parent) -Force | Out-Null
    if (Test-Path $preloader) {
        throw "Preloader directory exists but is not a valid checkout: $preloader"
    }
    Invoke-Checked git clone --depth 1 --branch 0.2.2 https://github.com/LiteLDev/preloader-android.git $preloader
}
Invoke-Checked $cmake -S $SourceDir -B $BuildDir -G "Unix Makefiles" `
    "-DCMAKE_MAKE_PROGRAM=$makeProgram" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DANDROID_ABI=$Abi" `
    "-DANDROID_PLATFORM=android-28" `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DPRELOADER_ANDROID_ROOT=$preloader" `
    "-DMELONRIND_BUILD_TESTS=OFF"
Invoke-Checked $cmake --build $BuildDir --target melonrind_android

if (Test-Path $DistDir) { Remove-Item -LiteralPath $DistDir -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $PackageDir "config") -Force | Out-Null
$library = Join-Path $BuildDir "out\$Abi\libmelonrind_android.so"
if (-not (Test-Path $library)) { throw "Native library not found: $library" }
Copy-Item (Join-Path $SourceDir "manifest.json") (Join-Path $PackageDir "manifest.json")
Copy-Item $library (Join-Path $PackageDir "libmelonrind_android.so")
Copy-Item (Join-Path $SourceDir "config\config.json") (Join-Path $PackageDir "config\config.json")
Copy-Item (Join-Path $SourceDir "config\config.schema.json") (Join-Path $PackageDir "config\config.schema.json")

$archive = Join-Path $DistDir "melonrind-android.levipack"
$zip = Join-Path $DistDir "melonrind-android.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $PackageDir "*") -DestinationPath $zip -Force
Move-Item -LiteralPath $zip -Destination $archive -Force
Write-Host "Built $PackageDir"
Write-Host "Built $archive"
