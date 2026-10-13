# Build the headless server host (tamias_server) with CMake (MSVC + Ninja Multi-Config).
#
# vcvars64 first, for the same reason build-tests.ps1 does it: the msvc preset sets
# CMAKE_C_COMPILER to cl.exe, which is not on a plain PowerShell PATH, so
# `cmake --preset msvc` fails with "cl.exe ... was not found in the PATH".
#
# This builds incrementally in the existing build/ (the engine libraries are already
# compiled there; only two new translation units get compiled, then linked).
# For a clean standalone tree use the `server` configure preset (binaryDir build/server).
#
# Output: build\bin\<Config>\tamias_server.dll
#   The .NET shell (server/csharp/Tamias.Server) P/Invokes it and is published next to it.
#
# NOTE: keep this file ASCII-only. scripts\server-build.cmd launches it with
# Windows PowerShell 5.1, which reads a BOM-less script using the console codepage,
# so non-ASCII comments would be mangled and break parsing.
param(
  [ValidateSet("debug", "relwithdebinfo", "release")]
  [string]$Preset = "relwithdebinfo",
  # Also let CMake publish the .NET shell next to the native library
  # (TAMIAS_BUILD_SERVER_SHELL=ON).
  [switch]$WithShell,
  # Network unavailable: do not let FetchContent re-download third-party sources,
  # reuse what is already under build/_deps.
  [switch]$Offline
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Find-Vcvars64 {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($install) {
      $bat = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
      if (Test-Path $bat) {
        return $bat
      }
    }
  }
  $candidates = @(
    "$env:ProgramFiles\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    "$env:ProgramFiles\Microsoft Visual Studio\17\Community\VC\Auxiliary\Build\vcvars64.bat"
  )
  foreach ($bat in $candidates) {
    if (Test-Path $bat) {
      return $bat
    }
  }
  throw "vcvars64.bat not found. Install VS with C++ tools, or run vswhere."
}

$config = @{
  debug = "Debug"
  relwithdebinfo = "RelWithDebInfo"
  release = "Release"
}[$Preset]

$vcvars = Find-Vcvars64

# TAMIAS_BUILD_SERVER defaults to OFF so it cannot affect the desktop / wasm product
# lines; turn it on explicitly here. It only adds one target.
$shellFlag = if ($WithShell) { "ON" } else { "OFF" }
$extra = @(
  "-DTAMIAS_BUILD_SERVER=ON",
  "-DTAMIAS_BUILD_SERVER_SHELL=$shellFlag"
)
if ($Offline) {
  $extra += "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
}
$extraFlags = $extra -join " "

Write-Host "Configuring msvc preset ($extraFlags)..."
cmd /c "call `"$vcvars`" && cmake --preset msvc $extraFlags"
if ($LASTEXITCODE -ne 0) {
  throw "cmake --preset msvc failed ($LASTEXITCODE)"
}

Write-Host "Building tamias_server ($config)..."
cmd /c "call `"$vcvars`" && cmake --build build --config $config --target tamias_server"
if ($LASTEXITCODE -ne 0) {
  throw "building tamias_server failed ($LASTEXITCODE)"
}

$dll = Join-Path $Root "build\bin\$config\tamias_server.dll"
if (Test-Path $dll) {
  Write-Host "Built $dll"
} else {
  Write-Host "Build finished, but $dll is missing - check the output above."
  exit 1
}
