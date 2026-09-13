# Guard for the desktop debug launch config.
#
# "CMake Launch Target" builds the desktop target `tamias`, which only exists in the
# desktop configure preset (msvc / linux). With the `wasm` configure preset active,
# ninja only says "unknown target 'tamias'", which is easy to misread as a broken
# build. Fail here instead, with an actionable message.
#
# NOTE: keep this file ASCII-only (Windows PowerShell reads .ps1 as ANSI without a BOM).
param(
  [string]$ConfigurePreset = ""
)

if ($ConfigurePreset -eq "" -or $ConfigurePreset -match "^\$") {
  # Variable did not resolve (CMake Tools not active) - let the build report it.
  exit 0
}

$desktop = @("msvc", "linux")
if ($desktop -contains $ConfigurePreset) {
  exit 0
}

Write-Host ""
Write-Host "Cannot debug the desktop app while Configure preset is '$ConfigurePreset'." -ForegroundColor Yellow
Write-Host "The desktop target 'tamias' only exists in the desktop tree (msvc / linux)." -ForegroundColor Yellow
Write-Host ""
Write-Host "  - Preview in the browser? Pick the launch config 'Browser: WASM preview' and press F5."
Write-Host "  - Debug the desktop app?   Switch the Configure preset back to msvc / linux first."
Write-Host ""
exit 1
