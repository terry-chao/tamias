# WASM preview entry point (VS Code F5 runs this; it also works standalone).
#
#   ensure emsdk env -> configure if needed -> build tamias_viewer -> start preview -> wait ready
#
# The preview server runs in its own cmd window (title "TamiasWasmServe"):
# **closing that window stops the preview** - no separate "stop preview" preset needed.
#
# -NoBuild: only start the server (no configure/build).
# -NoBrowser: do not open the browser (VS Code's F5 debug config opens it itself).
# NOTE: keep this file ASCII-only. Windows PowerShell reads .ps1 as ANSI (GBK here)
# unless it has a UTF-8 BOM, so non-ASCII text without a BOM breaks parsing.
param(
  [switch]$NoBuild,
  [switch]$NoBrowser
)

$Port = 3000

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Resolve-Emsdk {
  $candidates = @($env:EMSDK, "C:\dev\emsdk", "C:\emsdk", "$env:USERPROFILE\emsdk")
  foreach ($dir in $candidates) {
    if ($dir -and (Test-Path (Join-Path $dir "upstream\emscripten\emcc.exe"))) {
      return (Resolve-Path $dir).Path
    }
  }
  throw "emsdk not found (no upstream\emscripten\emcc.exe). Install the Emscripten SDK or set EMSDK."
}

function Stop-Preview {
  # wasm-stop.ps1 kills by window title + port; it only prints when nothing is there.
  try {
    & (Join-Path $PSScriptRoot "wasm-stop.ps1") *> $null
  } catch {
    # nothing to stop
  }
}

$wasmDir = Join-Path $Root "build\wasm"
$binDir = Join-Path $wasmDir "bin"

if (-not $NoBuild) {
  # emsdk is only needed to configure/build; -NoBuild just serves static files.
  $emsdk = Resolve-Emsdk
  $env:EMSDK = $emsdk
  $env:Path = "$emsdk\upstream\emscripten;$emsdk;$env:Path"

  $cache = Join-Path $wasmDir "CMakeCache.txt"
  if (Test-Path $cache) {
    $toolchain = (Select-String -Path $cache -Pattern "^CMAKE_TOOLCHAIN_FILE" -ErrorAction SilentlyContinue |
      Select-Object -First 1).Line
    if (-not $toolchain -or $toolchain -notmatch "Emscripten") {
      throw "build\wasm was configured with a different toolchain ($toolchain). Delete build\wasm and retry."
    }
  } else {
    Write-Host "Configuring the wasm preset..."
    $toolchainFile = Join-Path $emsdk "upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake"
    & emcmake cmake --preset wasm -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile"
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
  }

  Write-Host "Building tamias_viewer..."
  cmake --build $wasmDir --target tamias_viewer
  if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
}

Stop-Preview

# Spawn the preview window with Start-Process: the new process must NOT inherit our
# stdio. With `start ... cmd /k` the child keeps the parent's pipe open, so this
# script (and VS Code's preLaunchTask) would hang until the window is closed.
Start-Process -FilePath (Join-Path $PSScriptRoot "serve-wasm.cmd") `
  -ArgumentList "`"$binDir`"", $Port | Out-Null

# Wait until the port answers, then let VS Code open the browser (otherwise the first
# paint can race the server startup).
$ready = $false
for ($i = 0; $i -lt 60; $i++) {
  try {
    $resp = Invoke-WebRequest "http://localhost:$Port/" -UseBasicParsing -TimeoutSec 1
    if ($resp.StatusCode -eq 200) { $ready = $true; break }
  } catch {
    Start-Sleep -Milliseconds 250
  }
}
if (-not $ready) { throw "preview server did not come up on port $Port" }

Write-Host "preview ready: http://localhost:$Port"
Write-Host "close the TamiasWasmServe window to stop the preview"

if (-not $NoBrowser) {
  Start-Process "http://localhost:$Port"
}
