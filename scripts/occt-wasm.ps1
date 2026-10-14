# Cross-compile OCCT 7.9.3 to wasm32-emscripten, for the browser viewer's modeling
# kernel (see docs/WEB.md). Produces a static OCCT install that the "wasm" preset
# points at via TAMIAS_OCCT_PREFIX.
#
# Module set: FoundationClasses + ModelingData + ModelingAlgorithms only.
# Visualization / ApplicationFramework / DataExchange / Draw are OFF: the wasm
# viewer needs BRep modeling + tessellation and nothing else. In particular the
# STEP/IGES/BREP import path (occt_shape_ops.cpp) is NOT part of the wasm build
# (TAMIAS_OCCT_DATA_EXCHANGE=OFF), and that is the only thing that would need
# XCAF -- which on OCCT 7.9 drags Visualization in with it.
#
# Result: <Root>\prefix\lib\libTK*.a + lib\cmake\opencascade\OpenCASCADEConfig.cmake
#
# Usage:
#   powershell -File scripts/occt-wasm.ps1              # clone, configure, build, install
#   powershell -File scripts/occt-wasm.ps1 -Reuse      # reuse an existing checkout
#   powershell -File scripts/occt-wasm.ps1 -ConfigureOnly
#
# NOTE: keep this file ASCII-only (same convention as scripts/build-server.ps1).
param(
  [string]$Root = "C:\dev\occt-wasm",
  [string]$Emsdk = "C:\dev\emsdk",
  [switch]$Reuse,
  [switch]$ConfigureOnly
)

$ErrorActionPreference = "Stop"

$Src = Join-Path $Root "src"
$Build = Join-Path $Root "build"
$Prefix = Join-Path $Root "prefix"
$Log = Join-Path $Root "build.log"

# Ninja ships inside the Visual Studio CMake extension here; keep the same one the
# CMakePresets "wasm" preset uses so both paths agree.
$NinjaDir = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

New-Item -ItemType Directory -Force -Path $Root | Out-Null

$env:EMSDK = $Emsdk
$node = Join-Path $Emsdk "node\24.19.0_64bit\node.exe"
$py = Join-Path $Emsdk "python\3.13.3_64bit\python.exe"
if (Test-Path $node) { $env:EMSDK_NODE = $node }
if (Test-Path $py) { $env:EMSDK_PYTHON = $py }
$env:PATH = "$Emsdk;$Emsdk\upstream\emscripten;$NinjaDir;$env:PATH"

# CRITICAL: clear the vcpkg toolchain this shell may inherit. emcmake checks
# CMAKE_TOOLCHAIN_FILE and, when it is already set, SKIPS injecting the Emscripten
# toolchain -- the configure then runs under vcpkg + MSVC and dies with
# "No CMAKE_C_COMPILER could be found". (The "wasm" preset clears VCPKG_ROOT for
# the same reason.) Symptom if you forget: the log shows "-- Running vcpkg install"
# and "The C compiler identification is unknown".
$toolchain = Join-Path $Emsdk "upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake"
if (-not (Test-Path $toolchain)) { throw "Emscripten toolchain not found: $toolchain" }
$env:CMAKE_TOOLCHAIN_FILE = $toolchain
Remove-Item Env:\VCPKG_ROOT -ErrorAction SilentlyContinue
Remove-Item Env:\VCPKG_INSTALLATION_ROOT -ErrorAction SilentlyContinue

# Emscripten 6.x ships .exe wrappers (there is no emcmake.bat any more).
$emcmake = Join-Path $Emsdk "upstream\emscripten\emcmake.exe"
$ninja = Join-Path $NinjaDir "ninja.exe"
if (-not (Test-Path $emcmake)) { throw "emcmake not found: $emcmake" }
if (-not (Test-Path $ninja)) { throw "ninja not found: $ninja" }

function Write-Phase($text) {
  $line = "=== $text " + ("=" * [Math]::Max(0, 64 - $text.Length))
  Write-Host $line
  Add-Content -Path $Log -Value $line
}

Set-Content -Path $Log -Value "OCCT wasm build started $(Get-Date -Format s)"

# --- 1) source -----------------------------------------------------------------
if ($Reuse -and (Test-Path (Join-Path $Src "CMakeLists.txt"))) {
  Write-Phase "reusing source at $Src"
} else {
  if (Test-Path $Src) { Remove-Item -Recurse -Force $Src }
  Write-Phase "cloning OCCT V7_9_3 (shallow)"
  git clone --depth 1 --branch V7_9_3 https://github.com/Open-Cascade-SAS/OCCT.git $Src 2>&1 |
    Tee-Object -FilePath $Log -Append
  if ($LASTEXITCODE -ne 0) { throw "git clone failed ($LASTEXITCODE)" }
}

# --- 2) configure --------------------------------------------------------------
# Always clean the build dir: a cache written by a mis-toolchained configure
# poisons every later run (see the vcpkg note above).
if (Test-Path $Build) { Remove-Item -Recurse -Force $Build }
Write-Phase "configure (emcmake + Ninja)"
$cfg = @(
  "-S", $Src, "-B", $Build, "-G", "Ninja",
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
  "-DVCPKG_MANIFEST_MODE=OFF",
  "-DCMAKE_BUILD_TYPE=Release",
  "-DCMAKE_MAKE_PROGRAM=$ninja",
  "-DCMAKE_INSTALL_PREFIX=$Prefix",
  "-DINSTALL_DIR=$Prefix",
  "-DBUILD_LIBRARY_TYPE=Static",
  "-DBUILD_MODULE_FoundationClasses=ON",
  "-DBUILD_MODULE_ModelingData=ON",
  "-DBUILD_MODULE_ModelingAlgorithms=ON",
  "-DBUILD_MODULE_Visualization=OFF",
  "-DBUILD_MODULE_ApplicationFramework=OFF",
  "-DBUILD_MODULE_DataExchange=OFF",
  "-DBUILD_MODULE_Draw=OFF",
  "-DUSE_FREETYPE=OFF",
  "-DUSE_TBB=OFF",
  "-DUSE_OPENGL=OFF",
  "-DUSE_GLES2=OFF",
  "-DUSE_VTK=OFF",
  "-DUSE_TK=OFF",
  "-DUSE_RAPIDJSON=OFF",
  "-DUSE_OPENVR=OFF",
  "-DUSE_FFMPEG=OFF",
  "-DUSE_FREEIMAGE=OFF",
  "-DUSE_DRACO=OFF",
  "-DUSE_XLIB=OFF",
  # OCCT throws Standard_Failure all over the place; Emscripten needs exceptions
  # explicitly (the viewer links with -fexceptions + DISABLE_EXCEPTION_CATCHING=0).
  "-DCMAKE_CXX_FLAGS=-fexceptions",
  "-DCMAKE_C_FLAGS=-fexceptions"
)
& $emcmake cmake @cfg 2>&1 | Tee-Object -FilePath $Log -Append
if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
if ($ConfigureOnly) { Write-Phase "configure-only run finished OK"; exit 0 }

# --- 3) build ------------------------------------------------------------------
Write-Phase "build (parallel)"
& cmake --build $Build --parallel 2>&1 | Tee-Object -FilePath $Log -Append
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

# --- 4) install ----------------------------------------------------------------
Write-Phase "install to $Prefix"
& cmake --install $Build 2>&1 | Tee-Object -FilePath $Log -Append
if ($LASTEXITCODE -ne 0) { throw "install failed ($LASTEXITCODE)" }

Write-Phase "DONE"
$libs = Get-ChildItem $Prefix -Recurse -Filter "*.a" -ErrorAction SilentlyContinue
Write-Host ("static libs installed: {0}" -f $libs.Count)
$libs | Select-Object -ExpandProperty Name | Sort-Object | ForEach-Object { Write-Host "  $_" }
