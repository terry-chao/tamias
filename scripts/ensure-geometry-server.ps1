# Make the geometry service ready for the managed debugger to attach.
#
# Why this exists: a name-based attach ("processName": "Tamias.Server") fails with
# "No process with the specified name is currently running" when the service is not up yet.
# VS Code cannot start the service and attach in one step by itself, but it CAN run a
# preLaunchTask before the attach - so this script makes the service ready and returns.
#
# Two modes:
#   (default)  ensure one is running, then wait until something listens on the port
#   -Stop      stop whatever is running (the build task calls this before relinking the DLL)
#
# Idempotent on purpose: if something is already listening it does NOT touch it, so the
# attach config can be launched next to the F5 browser chain (which starts the service
# itself) without a port fight.
#
# It RE-USE ONLY: it never builds. Build first with scripts\build-server.ps1 -WithShell
# (task "Tamias: build geometry service"), otherwise there is no Tamias.Server.exe to start.
#
# NOTE: keep this file ASCII-only. scripts\*.cmd launches .ps1 with Windows PowerShell 5.1,
# which reads a BOM-less script using the console codepage (GBK here), so non-ASCII text
# would be mangled and break parsing.
param(
  [int]$Port = 5199,
  [switch]$Stop,
  [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

function Get-ServiceDirectory {
  # tamias_server.dll and the published Tamias.Server.exe MUST sit in the SAME directory:
  # P/Invoke resolves the conventional name "tamias_server" against the app base directory.
  # Same candidate order as scripts\wasm-preview.ps1, so both agree on which build is live.
  $candidates = @(
    (Join-Path $Root "build\bin\RelWithDebInfo"),
    (Join-Path $Root "build\bin\Release"),
    (Join-Path $Root "build\bin\Debug")
  )
  foreach ($dir in $candidates) {
    if ((Test-Path (Join-Path $dir "tamias_server.dll")) -and
        (Test-Path (Join-Path $dir "Tamias.Server.exe")) -and
        (Test-Path (Join-Path $dir "Tamias.Server.dll"))) {
      return $dir
    }
  }
  return $null
}

function Test-PortListening([int]$p) {
  # Raw socket probe ON PURPOSE: with nothing listening, Invoke-WebRequest waits out the
  # whole timeout (measured: 1.0 s for -TimeoutSec 1) before throwing, while a refused TCP
  # connect fails immediately. Polling with the socket makes the wait loop ~10x more
  # responsive and avoids HTTP proxy settings ever being consulted.
  $client = New-Object System.Net.Sockets.TcpClient
  try {
    $pending = $client.BeginConnect("127.0.0.1", $p, $null, $null)
    if (-not $pending.AsyncWaitHandle.WaitOne(300, $false)) { return $false }
    $client.EndConnect($pending)
    return $true
  } catch {
    return $false
  } finally {
    $client.Close()
  }
}

function Get-RunningServer {
  return @(Get-Process -Name "Tamias.Server" -ErrorAction SilentlyContinue)
}

if ($Stop) {
  $running = Get-RunningServer
  if ($running.Count -eq 0) {
    Write-Host "geometry server not running - nothing to stop"
    exit 0
  }
  $running | ForEach-Object { Write-Host "stopping Tamias.Server pid $($_.Id)" }
  $running | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 400
  $after = Get-RunningServer
  if ($after.Count -gt 0) {
    Write-Host "could not stop Tamias.Server (still $($after.Count) running): close it by hand so tamias_server.dll can be relinked"
    exit 1
  }
  Write-Host "geometry server stopped"
  exit 0
}

# Already serving? Then leave it alone: this is what makes the compound / F5 coexistence work.
if (Test-PortListening $Port) {
  Write-Host "something is already listening on port $Port - reusing it (the attach is safe now)"
  exit 0
}

$running = Get-RunningServer
$startedByUs = $false
if ($running.Count -gt 0) {
  # A process with our name exists but the port is not up yet (e.g. another debug session is
  # starting it). Wait for it instead of spawning a second one on the same port.
  Write-Host "Tamias.Server pid $($running[0].Id) is already running - waiting for port $Port"
} else {
  $serviceDir = Get-ServiceDirectory
  if (-not $serviceDir) {
    Write-Host "geometry service artifacts not found (need tamias_server.dll + Tamias.Server.exe + Tamias.Server.dll in one directory):"
    Write-Host "  looked in: build\bin\RelWithDebInfo, build\bin\Release, build\bin\Debug"
    Write-Host "  build them first: Tasks -> 'Tamias: build geometry service'"
    Write-Host "                   or: powershell -File scripts\build-server.ps1 -WithShell -Offline"
    exit 1
  }
  Write-Host "starting geometry service from $serviceDir (window: TamiasGeometryServe)"
  # Start-Process without -NoNewWindow: serve-geometry.cmd opens its own console window and
  # must NOT inherit this task's stdio, otherwise VS Code waits for that window to close
  # before the preLaunchTask is considered done (the same trap scripts\wasm-preview.ps1
  # documents).
  Start-Process -FilePath (Join-Path $PSScriptRoot "serve-geometry.cmd") -ArgumentList "`"$serviceDir`"", $Port | Out-Null
  $startedByUs = $true
}

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline) {
  if (Test-PortListening $Port) {
    Write-Host "geometry server ready on http://127.0.0.1:$Port - the attach can proceed"
    exit 0
  }
  if ($startedByUs -and (Get-RunningServer).Count -eq 0) {
    Write-Host "Tamias.Server exited during startup - read the TamiasGeometryServe window for the reason"
    Write-Host "  (the shell refuses to start when the native ABI version and the shell disagree;"
    Write-Host "   rebuilding both with 'Tamias: build geometry service' is the fix)"
    exit 1
  }
  Start-Sleep -Milliseconds 250
}

Write-Host "geometry server did not listen on port $Port within $TimeoutSeconds s"
Write-Host "  check: the TamiasGeometryServe window (it refuses to start when the native ABI and the shell disagree)"
Write-Host "  check: is something else already listening on $Port"
exit 1
