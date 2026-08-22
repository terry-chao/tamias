# Configure / build / optionally serve the Emscripten viewer on Windows.
# emcmake does not read CMakePresets.json for -G, and this repo's Ninja lives
# inside Visual Studio rather than on PATH.
param(
  [switch]$ConfigureOnly,
  [switch]$BuildOnly,
  [switch]$Serve
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Find-NinjaDir {
  $candidates = @(
    "$env:ProgramFiles\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja",
    "$env:ProgramFiles\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
  )
  foreach ($dir in $candidates) {
    if (Test-Path (Join-Path $dir "ninja.exe")) {
      return $dir
    }
  }
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
    if ($install) {
      $dir = Join-Path $install "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
      if (Test-Path (Join-Path $dir "ninja.exe")) {
        return $dir
      }
    }
  }
  $cmd = Get-Command ninja -ErrorAction SilentlyContinue
  if ($cmd) {
    return Split-Path -Parent $cmd.Source
  }
  throw "ninja.exe not found. Install the VS CMake tools or add Ninja to PATH."
}

function Ensure-Emscripten {
  if (Get-Command emcmake -ErrorAction SilentlyContinue) {
    return
  }
  $emsdkCandidates = @(
    $env:EMSDK,
    "C:\emsdk",
    "C:\dev\emsdk",
    "$env:USERPROFILE\emsdk"
  ) | Where-Object { $_ }
  foreach ($root in $emsdkCandidates) {
    $envBat = Join-Path $root "emsdk_env.ps1"
    if (Test-Path $envBat) {
      . $envBat
      if (Get-Command emcmake -ErrorAction SilentlyContinue) {
        return
      }
    }
  }
  throw "emcmake not found. Activate emsdk_env.ps1 first (or set EMSDK)."
}

function Test-WasmCacheReady {
  $cacheFile = Join-Path $Root "build\wasm\CMakeCache.txt"
  if (-not (Test-Path $cacheFile)) {
    return $false
  }
  $cache = Get-Content $cacheFile -Raw
  $toolchainLine = [regex]::Match($cache, "CMAKE_TOOLCHAIN_FILE:\w+=(.+)")
  $toolchainPath = if ($toolchainLine.Success) { $toolchainLine.Groups[1].Value.Trim() } else { "" }
  $hasCompiler = $cache -match "CMAKE_(C|CXX)_COMPILER:(FILEPATH|STRING)=\S+"
  if ($cache -match "vcpkg\.cmake") { return $false }
  if (-not $toolchainPath) { return $false }
  if (-not (Test-Path $toolchainPath)) { return $false }
  if ($toolchainPath -notmatch "Emscripten") { return $false }
  if (-not $hasCompiler) { return $false }
  return $true
}

function Invoke-WasmConfigure {
  Ensure-Emscripten
  Write-Host "Configuring wasm preset..."
  if (-not $env:EMSDK) {
    throw "EMSDK is not set after activating Emscripten."
  }
  $toolchain = Join-Path $env:EMSDK "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
  if (-not (Test-Path $toolchain)) {
    throw "Emscripten toolchain missing: $toolchain"
  }
  $wasmBuild = Join-Path $Root "build\wasm"
  if ((Test-Path (Join-Path $wasmBuild "CMakeCache.txt")) -and -not (Test-WasmCacheReady)) {
    Write-Host "Removing stale $wasmBuild (wrong toolchain)"
    Remove-Item -Recurse -Force $wasmBuild
  }
  # emcmake does not put -DCMAKE_TOOLCHAIN_FILE on the command line when
  # --preset is used; the preset must name Emscripten, and we pass it too.
  & emcmake cmake --preset wasm -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
  if ($LASTEXITCODE -ne 0) {
    throw "emcmake cmake --preset wasm failed ($LASTEXITCODE)"
  }
}

$ninjaDir = Find-NinjaDir
$env:PATH = "$ninjaDir;$env:PATH"
$env:TAMIAS_WASM = "1"
Write-Host "Using Ninja at $ninjaDir"

if ($ConfigureOnly -or -not $BuildOnly -or -not (Test-WasmCacheReady)) {
  Invoke-WasmConfigure
}

if (-not $ConfigureOnly) {
  Ensure-Emscripten
  Write-Host "Building tamias_viewer..."
  cmake --build (Join-Path $Root "build\wasm") --target tamias_viewer
  if ($LASTEXITCODE -ne 0) {
    throw "cmake --build tamias_viewer failed ($LASTEXITCODE)"
  }
  Write-Host "Output: $Root\build\wasm\bin"
}

if ($Serve) {
  npx --yes serve "$Root\build\wasm\bin"
}
