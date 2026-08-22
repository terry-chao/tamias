# Stop the WASM preview server (port 3000 and the TamiasWasmServe window).
$ErrorActionPreference = "SilentlyContinue"
$killed = $false

function Stop-PidTree([int]$ProcessId) {
  if ($ProcessId -le 4) { return }
  & taskkill.exe /F /T /PID $ProcessId 2>$null | Out-Null
}

Get-CimInstance Win32_Process |
  Where-Object { $_.Name -eq "cmd.exe" -and $_.CommandLine -match "TamiasWasmServe" } |
  ForEach-Object {
    Write-Host "Stopping TamiasWasmServe PID $($_.ProcessId)"
    Stop-PidTree $_.ProcessId
    $killed = $true
  }

$listeners = @()
try {
  $listeners = @(Get-NetTCPConnection -LocalPort 3000 -State Listen)
} catch {
  $listeners = @()
}
foreach ($conn in $listeners) {
  Write-Host "Stopping PID $($conn.OwningProcess) on port 3000"
  Stop-PidTree $conn.OwningProcess
  $killed = $true
}

if ($listeners.Count -eq 0) {
  foreach ($line in (& netstat.exe -ano)) {
    if ($line -notmatch ":3000\s+" -or $line -notmatch "LISTENING") { continue }
    $parts = $line.Trim() -split "\s+"
    $procId = 0
    if ([int]::TryParse($parts[-1], [ref]$procId) -and $procId -gt 4) {
      Write-Host "Stopping PID $procId on port 3000"
      Stop-PidTree $procId
      $killed = $true
    }
  }
}

if ($killed) {
  Write-Host "WASM preview stopped."
} else {
  Write-Host "No WASM preview server on port 3000."
}
