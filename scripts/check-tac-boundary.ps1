# tac boundary gate.
#
#   1. src/ui/tac/** must not include Qt -- it is the toolkit-agnostic layer.
#   2. src/app/** Qt usage is a ratchet: it may only shrink, never grow.
#   3. No src/ module other than src/app and src/ui/qt may include Qt.
#
# Run:  pwsh -File scripts/check-tac-boundary.ps1
#
# NOTE: keep this file ASCII-only (Windows PowerShell reads .ps1 as ANSI without a BOM).
param(
  # Number of Qt-dependent files under src/app when the ratchet was last lowered.
  [int]$LegacyAppQtBaseline = 131
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Matches "#include <QWidget>", "#include <QtWidgets/QApplication>" and the quoted
# form. Deliberately does not match <queue> or OCCT's <Quantity_Color.hxx>.
$qtInclude = '#\s*include\s*[<"](?:Qt[A-Za-z]+/)?Q[A-Za-z0-9]+(?:/[A-Za-z0-9_]+)*[>"]'
$codeExtensions = @('.h', '.hpp', '.cpp', '.cc', '.cxx', '.inl')

function Get-CodeFiles([string]$dir) {
  if (-not (Test-Path -LiteralPath $dir)) { return @() }
  return Get-ChildItem -LiteralPath $dir -Recurse -File |
    Where-Object { $codeExtensions -contains $_.Extension.ToLowerInvariant() }
}

function Test-HasQtInclude($file) {
  # -CaseSensitive matters: PowerShell regex is case-insensitive by default and
  # would otherwise match <queue>.
  return [bool](Select-String -LiteralPath $file.FullName -Pattern $qtInclude -List -CaseSensitive)
}

$failed = $false

# --- 1. src/ui/tac must be Qt-free -------------------------------------------
$violations = @(Get-CodeFiles (Join-Path $root 'src\ui\tac') | Where-Object { Test-HasQtInclude $_ })
if ($violations.Count -gt 0) {
  Write-Host "tac boundary violated: src/ui/tac must not include Qt." -ForegroundColor Red
  $violations | ForEach-Object { Write-Host "  $($_.FullName)" }
  $failed = $true
}

# --- 3. Qt is only allowed in src/app and src/ui/qt ----------------------
$allowedPrefixes = @(
  ((Join-Path $root 'src\app') + '\'),
  ((Join-Path $root 'src\ui\qt') + '\')
)
$outside = @(Get-CodeFiles (Join-Path $root 'src') | Where-Object {
  $path = $_.FullName
  $allowed = $false
  foreach ($prefix in $allowedPrefixes) { if ($path.StartsWith($prefix)) { $allowed = $true } }
  (-not $allowed) -and (Test-HasQtInclude $_)
})
if ($outside.Count -gt 0) {
  Write-Host "tac boundary violated: Qt include outside src/app and src/ui/qt." -ForegroundColor Red
  $outside | ForEach-Object { Write-Host "  $($_.FullName)" }
  $failed = $true
}

# --- 2. src/app ratchet -----------------------------------------------------
$appQt = @(Get-CodeFiles (Join-Path $root 'src\app') | Where-Object { Test-HasQtInclude $_ })
Write-Host "tac boundary: src/ui/tac clean; src/app Qt files = $($appQt.Count) (baseline $LegacyAppQtBaseline)"
if ($appQt.Count -gt $LegacyAppQtBaseline) {
  Write-Host "src/app gained Qt-dependent files. Route new code through tac instead," -ForegroundColor Red
  Write-Host "or lower the ratchet in scripts/check-tac-boundary.ps1 when you migrate files out." -ForegroundColor Red
  $failed = $true
}

if ($failed) { exit 1 }
exit 0
