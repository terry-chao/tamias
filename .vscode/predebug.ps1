param(
  [Parameter(Mandatory = $true)]
  [string]$Preset
)

$ErrorActionPreference = "Continue"
Get-Process -Name tamias -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

Set-Location (Resolve-Path (Join-Path $PSScriptRoot ".."))
cmake --build --preset $Preset --parallel
exit $LASTEXITCODE
