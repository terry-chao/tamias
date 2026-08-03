param(
  [Parameter(Mandatory = $true)]
  [string]$Preset
)

$ErrorActionPreference = "Continue"
Get-Process -Name tamias -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

Set-Location (Resolve-Path (Join-Path $PSScriptRoot ".."))
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
$buildCommand = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && cmake --build --preset $Preset --parallel"
& $env:ComSpec /d /c $buildCommand
exit $LASTEXITCODE
