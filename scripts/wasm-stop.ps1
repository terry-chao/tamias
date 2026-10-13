# Stop the WASM dev services started by the F5 flow (scripts\wasm-preview.ps1):
#   - the static preview server  :3000  (node scripts\serve-wasm.mjs)
#   - the geometry server        :5199  (Tamias.Server, the .NET shell)
#
# Rule: never kill a process we cannot prove is ours.
# Identifying the preview by "whatever owns port 3000" is not good enough here: this
# machine's dynamic port range starts at 1024 (netsh int ipv4 show dynamicport tcp),
# and the desktop tamias.exe asks the OS for a free port for its MCP bridge
# (McpService::start(port = 0)). So port 3000 can legitimately belong to a process
# that has nothing to do with the web viewer - killing it would make the desktop app
# vanish while you were debugging WebGL.
#
# NOTE: keep this file ASCII-only (Windows PowerShell reads it with the console codepage).
param(
  [int]$GeometryPort = 5199
)

$ErrorActionPreference = "SilentlyContinue"
$killed = $false
$geometryKilled = $false

function Stop-PidTree([int]$ProcessId) {
  if ($ProcessId -le 4) { return }
  & taskkill.exe /F /T /PID $ProcessId 2>$null | Out-Null
}

function Get-ProcessName([int]$ProcessId) {
  $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
  if ($p) { return $p.ProcessName }
  return ""
}

# --- preview server: match on its own command line ---------------------------
# serve-wasm.cmd is the launcher window; serve-wasm.mjs is the node server itself.
# Match the process NAME too, not just the command line: a launcher or a job host
# (whatever spawned the preview) also carries "serve-wasm.mjs" in its own command
# line, and killing that would take down more than the preview.
# (Matching the window title "TamiasWasmServe" does not work: a title set with
# `title` never appears in Win32_Process.CommandLine.)
Get-CimInstance Win32_Process |
  Where-Object {
    $_.CommandLine -and (
      ($_.Name -eq "node.exe" -and $_.CommandLine -match "serve-wasm\.mjs") -or
      ($_.Name -eq "cmd.exe" -and $_.CommandLine -match "serve-wasm\.cmd")
    )
  } |
  ForEach-Object {
    Write-Host "Stopping preview PID $($_.ProcessId) ($($_.Name))"
    Stop-PidTree $_.ProcessId
    $killed = $true
  }

# --- port 3000: warn only, never kill ----------------------------------------
# A port is not an identity. Anything may hold 3000, and on this machine the desktop
# tamias.exe can legitimately be handed 3000 for its MCP bridge. So if the preview was
# not identified above by its command line, report the owner and leave it running.
$listeners = @()
try {
  $listeners = @(Get-NetTCPConnection -LocalPort 3000 -State Listen)
} catch {
  $listeners = @()
}
foreach ($conn in $listeners) {
  $name = Get-ProcessName $conn.OwningProcess
  Write-Host ("port 3000 is held by {0} (PID {1}) - not started by us, leaving it alone" -f $name, $conn.OwningProcess)
}

if ($listeners.Count -eq 0) {
  foreach ($line in (& netstat.exe -ano)) {
    if ($line -notmatch ":3000\s+" -or $line -notmatch "LISTENING") { continue }
    $parts = $line.Trim() -split "\s+"
    $procId = 0
    if (-not [int]::TryParse($parts[-1], [ref]$procId) -or $procId -le 4) { continue }
    $name = Get-ProcessName $procId
    Write-Host ("port 3000 is held by {0} (PID {1}) - not started by us, leaving it alone" -f $name, $procId)
  }
}

# --- geometry server: process name is exact, port is only a warning ----------
# Tamias.Server is the .NET shell's own host name, so matching it is precise.
Get-Process -Name "Tamias.Server" -ErrorAction SilentlyContinue | ForEach-Object {
  Write-Host "Stopping Tamias.Server PID $($_.Id)"
  Stop-PidTree $_.Id
  $geometryKilled = $true
}
if (-not $geometryKilled) {
  try {
    $geoListeners = @(Get-NetTCPConnection -LocalPort $GeometryPort -State Listen)
  } catch {
    $geoListeners = @()
  }
  foreach ($conn in $geoListeners) {
    $name = Get-ProcessName $conn.OwningProcess
    Write-Host ("port {0} is held by {1} (PID {2}), which is not Tamias.Server - leaving it alone" -f $GeometryPort, $name, $conn.OwningProcess)
  }
}

if ($killed) {
  Write-Host "WASM preview stopped."
} else {
  Write-Host "No WASM preview server found (port 3000)."
}
if ($geometryKilled) {
  Write-Host "Geometry server stopped."
}
