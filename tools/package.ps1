param(
    [string]$Ndk = "",
    [string]$PreloaderRoot = "",
    [string]$PreloaderCommit = "5d108da350ce3e7f13f51415f81e65344ecd5d14",
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

function Assert-Ndk28([string]$NdkPath) {
    $requiredRevision = "28.2.13676358"
    $propertiesPath = Join-Path $NdkPath "source.properties"
    if (-not (Test-Path $propertiesPath)) {
        throw "Selected Android NDK has no source.properties: $NdkPath"
    }
    $revisionLine = Select-String -LiteralPath $propertiesPath -Pattern '^Pkg\.Revision\s*=\s*(.+)$' | Select-Object -First 1
    $revision = if ($revisionLine) { $revisionLine.Matches[0].Groups[1].Value.Trim() } else { "" }
    if ($revision -ne $requiredRevision) {
        throw "This build must use Android NDK $requiredRevision; selected NDK reports '$revision'."
    }
}

function Invoke-Checked([string]$File, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

function Assert-Preloader([string]$Root, [string]$ExpectedCommit) {
    $head = (& git -C $Root rev-parse HEAD 2>$null).Trim()
    if ($LASTEXITCODE -ne 0 -or $head -ne $ExpectedCommit) {
        throw "Preloader checkout mismatch. Expected $ExpectedCommit, found '$head'."
    }
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
Assert-Ndk28 $ndkPath
$makeProgram = Join-Path $ndkPath "prebuilt\windows-x86_64\bin\make.exe"
if (-not (Test-Path $makeProgram)) { throw "GNU make was not found in the selected NDK: $makeProgram" }
if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -LiteralPath $BuildDir -Recurse -Force }

if (-not $SkipHostTests) {
    $hostDir = Join-Path $SourceDir "build-host"
    Invoke-Checked $cmake -S $SourceDir -B $hostDir -DMELONRIND_BUILD_TESTS=ON
    Invoke-Checked $cmake --build $hostDir --config $BuildType
    foreach ($testName in @("melonrind_model_tests", "melonrind_runtime_tests")) {
        $test = Join-Path $hostDir "$BuildType\$testName.exe"
        if (-not (Test-Path $test)) { $test = Join-Path $hostDir "$testName.exe" }
        if (-not (Test-Path $test)) { throw "Host test executable not found: $testName" }
        Invoke-Checked $test
    }
}

$toolchain = Join-Path $ndkPath "build\cmake\android.toolchain.cmake"
$preloader = if ($PreloaderRoot) { $PreloaderRoot } else { Join-Path $SourceDir "third_party\preloader-android" }
if (-not (Test-Path (Join-Path $preloader "include\pl\Mod.hpp"))) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "The required preloader-android checkout is missing and git is unavailable. Pass -PreloaderRoot."
    }
    New-Item -ItemType Directory -Path (Split-Path $preloader -Parent) -Force | Out-Null
    if (Test-Path $preloader) {
        throw "Preloader directory exists but is not a valid checkout: $preloader"
    }
    Invoke-Checked git clone https://github.com/LiteLDev/preloader-android.git $preloader
    Invoke-Checked git -C $preloader checkout $PreloaderCommit
}
Assert-Preloader $preloader $PreloaderCommit
Invoke-Checked $cmake -S $SourceDir -B $BuildDir -G "Unix Makefiles" `
    "-DCMAKE_MAKE_PROGRAM=$makeProgram" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DANDROID_ABI=$Abi" `
    "-DANDROID_PLATFORM=android-28" `
    "-DANDROID_STL=c++_shared" `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DPRELOADER_ANDROID_ROOT=$preloader" `
    "-DMELONRIND_BUILD_TESTS=OFF"
Invoke-Checked $cmake --build $BuildDir --target melonrind_android

if (Test-Path $DistDir) { Remove-Item -LiteralPath $DistDir -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $PackageDir "config") -Force | Out-Null
$library = Join-Path $BuildDir "out\$Abi\libmelonrind_android.so"
if (-not (Test-Path $library)) { throw "Native library not found: $library" }

$llvmBin = Join-Path $ndkPath "toolchains\llvm\prebuilt\windows-x86_64\bin"
$llvmReadelf = Join-Path $llvmBin "llvm-readelf.exe"
$llvmNm = Join-Path $llvmBin "llvm-nm.exe"
if (-not (Test-Path $llvmReadelf) -or -not (Test-Path $llvmNm)) {
    throw "LLVM ELF inspection tools are missing from the selected NDK."
}
$elfHeader = (& $llvmReadelf -h $library | Out-String)
if ($elfHeader -notmatch 'Machine:\s+AArch64' -or $elfHeader -notmatch 'Type:\s+DYN') {
    throw "Native library is not an AArch64 shared object: $library"
}
$undefined = (& $llvmNm -D --undefined-only $library | Out-String)
if ($undefined -match '__hash_memory') {
    throw "Native library still references incompatible libc++ symbol __hash_memory"
}
$defined = (& $llvmNm -D --defined-only $library | Out-String)
if ($defined -notmatch 'PLGetModRegistration') {
    throw "Native library does not export PLGetModRegistration: $library"
}
Copy-Item (Join-Path $SourceDir "manifest.json") (Join-Path $PackageDir "manifest.json")
Copy-Item $library (Join-Path $PackageDir "libmelonrind_android.so")
Copy-Item (Join-Path $SourceDir "config\config.json") (Join-Path $PackageDir "config\config.json")
Copy-Item (Join-Path $SourceDir "config\config.schema.json") (Join-Path $PackageDir "config\config.schema.json")

$archive = Join-Path $DistDir "melonrind-android.levipack"
$zip = Join-Path $DistDir "melonrind-android.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $PackageDir "*") -DestinationPath $zip -Force
Move-Item -LiteralPath $zip -Destination $archive -Force
$zipArchive = [System.IO.Compression.ZipFile]::OpenRead($archive)
$archiveEntries = $zipArchive.Entries.FullName
$zipArchive.Dispose()
foreach ($requiredEntry in @("manifest.json", "libmelonrind_android.so", "config/config.json", "config/config.schema.json")) {
    if ($archiveEntries -notcontains $requiredEntry) {
        throw "Package root is missing required entry '$requiredEntry': $archive"
    }
}
Write-Host "Built $PackageDir"
Write-Host "Built $archive"
