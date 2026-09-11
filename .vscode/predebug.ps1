# Stop the workspace tamias.exe so Restart can relink. The debug adapter
# keeps the process alive until after preLaunchTask, which locks the binary.
$ErrorActionPreference = "Continue"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$candidates = @(
  (Join-Path $root "build\bin\Debug\tamias.exe"),
  (Join-Path $root "build\bin\RelWithDebInfo\tamias.exe"),
  (Join-Path $root "build\bin\Release\tamias.exe")
) | ForEach-Object { [System.IO.Path]::GetFullPath($_) }

Get-CimInstance Win32_Process -Filter "Name = 'tamias.exe'" -ErrorAction SilentlyContinue |
  ForEach-Object {
    $path = $_.ExecutablePath
    if (-not $path) { return }
    $full = [System.IO.Path]::GetFullPath($path)
    if ($candidates -contains $full) {
      Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
  }

function Test-FileUnlocked([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) { return $true }
  try {
    $fs = [System.IO.File]::Open($Path, "Open", "ReadWrite", "None")
    $fs.Close()
    return $true
  } catch {
    return $false
  }
}

$deadline = (Get-Date).AddSeconds(8)
foreach ($exe in $candidates) {
  while (-not (Test-FileUnlocked $exe) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 150
  }
}

exit 0
