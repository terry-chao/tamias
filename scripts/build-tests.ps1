# Build tamias_tests with CMake (MSVC + Ninja Multi-Config).
# Cursor / plain PowerShell have no VS INCLUDE/LIB; this loads vcvars64 first.
param(
  [ValidateSet("debug", "relwithdebinfo", "release")]
  [string]$Preset = "debug",
  [switch]$BuildOnly,
  [string]$Filter = "RenderSceneGolden*",
  # Extra build targets (default: tamias_tests only). Pass tamias to compile the Qt shell.
  [string[]]$Targets = @("tamias_tests")
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
  debug = @{ Folder = "Debug"; Ctest = "Debug" }
  relwithdebinfo = @{ Folder = "RelWithDebInfo"; Ctest = "RelWithDebInfo" }
  release = @{ Folder = "Release"; Ctest = "Release" }
}[$Preset]

$vcvars = Find-Vcvars64

if (-not (Test-Path (Join-Path $Root "build\CMakeCache.txt"))) {
  Write-Host "Configuring msvc preset..."
  cmd /c "call `"$vcvars`" && cmake --preset msvc"
  if ($LASTEXITCODE -ne 0) {
    throw "cmake --preset msvc failed ($LASTEXITCODE)"
  }
}

$targetList = $Targets -join " "
Write-Host "Building $targetList ($Preset)..."
cmd /c "call `"$vcvars`" && cmake --build --preset $Preset --target $targetList"
if ($LASTEXITCODE -ne 0) {
  throw "cmake --build --preset $Preset --target $targetList failed ($LASTEXITCODE)"
}

# Building the app alone leaves nothing to run: stop here.
if ($Targets -notcontains "tamias_tests") {
  return
}

$exe = Join-Path $Root "build\bin\$($config.Folder)\tamias_tests.exe"
if (-not (Test-Path $exe)) {
  throw "missing $exe"
}
Write-Host "Built $exe"

if ($BuildOnly) {
  return
}

Write-Host "Running $exe --gtest_filter=$Filter"
& $exe "--gtest_filter=$Filter"
if ($LASTEXITCODE -ne 0) {
  throw "tamias_tests failed ($LASTEXITCODE)"
}
