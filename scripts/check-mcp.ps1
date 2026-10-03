# AI 接入（MCP）的最小验证：起 Tamias → 看桥是否活着 → 用 stdio 转发器走一遍
# 真实客户端会走的路。见 docs/DECISION-AI-INTEGRATION.md §5。
#
#   pwsh -File scripts/check-mcp.ps1
#   pwsh -File scripts/check-mcp.ps1 -Document .\my_model.tdoc -Config RelWithDebInfo
param(
  [string]$Config = "Debug",
  [string]$Document = "",
  # 写策略。默认 read-only 时下面那次建墙会被拒绝——那正是要看到的行为。
  # ask 会弹确认框等人点，脚本里不跑（手动试）。
  [ValidateSet('read-only', 'auto')]
  [string]$Policy = "auto",
  [switch]$KeepOpen
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $root "build\bin\$Config"
$tamias = Join-Path $binDir 'tamias.exe'
$forwarder = Join-Path $binDir 'tamias-mcp.exe'
$discovery = Join-Path $env:APPDATA 'tamias\tamias\mcp.json'

if (-not (Test-Path $tamias)) {
  throw "找不到 $tamias。先构建：pwsh -File scripts/build-tests.ps1 -Preset $($Config.ToLower()) -Targets tamias"
}
if (-not (Test-Path $forwarder)) {
  throw "找不到 $forwarder —— 这个构建还没有 MCP 转发器。重新构建 tamias 目标（CMake 会自动 dotnet publish 它）。"
}
if (-not $Document) {
  $sample = Join-Path $root 'assets\samples\cube.obj'
  if (Test-Path $sample) { $Document = $sample }
}

function Stop-Quietly($process) {
  if ($process -and -not $process.HasExited) {
    try { Stop-Process -Id $process.Id -Force } catch { }
  }
}

$tamiasProc = $null
$fwd = $null
try {
  Remove-Item -LiteralPath $discovery -Force -ErrorAction SilentlyContinue

  # ── 1. 起 Tamias，开桥 ──────────────────────────────────────────────
  $arguments = @('--mcp', "--mcp-policy=$Policy")
  if ($Document) { $arguments += $Document }
  $tamiasProc = Start-Process -FilePath $tamias -ArgumentList $arguments -PassThru -WindowStyle Hidden

  $bridge = $null
  $deadline = (Get-Date).AddSeconds(90)
  while ((Get-Date) -lt $deadline) {
    if ($tamiasProc.HasExited) { throw "Tamias 启动就退出了（exit $($tamiasProc.ExitCode)）" }
    if (Test-Path $discovery) {
      $candidate = $null
      try { $candidate = Get-Content $discovery -Raw | ConvertFrom-Json } catch { }
      if ($candidate -and [int64]$candidate.pid -eq $tamiasProc.Id) { $bridge = $candidate; break }
    }
    Start-Sleep -Milliseconds 400
  }
  if (-not $bridge) { throw "等不到发现文件 $discovery（Tamias 可能没起来）" }

  # ── 2. 探活（两行命令的最小验证就是这一步）──────────────────────────
  # 文档是异步打开的：等 /health 报出文档名，否则会看到空文档名而以为没连上。
  $health = $null
  $deadline = (Get-Date).AddSeconds(30)
  while ((Get-Date) -lt $deadline) {
    try {
      $health = Invoke-RestMethod -Uri "http://127.0.0.1:$($bridge.port)/health" `
        -Headers @{ Authorization = "Bearer $($bridge.token)" }
      if ($health.document) { break }
    } catch { }
    Start-Sleep -Milliseconds 300
  }
  if (-not $health) { throw '探活失败：桥没有应答 /health' }
  Write-Output "写策略       : $Policy"
  Write-Output "Tamias      : pid=$($tamiasProc.Id)  文档='$($health.document)'"
  Write-Output "桥           : $($bridge.endpoint)"
  Write-Output "探活         : ok=$($health.ok) document='$($health.document)'"

  # ── 3. 给 AI 客户端粘的配置 ────────────────────────────────────────
  $clientConfig = [ordered]@{
    mcpServers = [ordered]@{ tamias = [ordered]@{ command = $forwarder; args = @() } }
  } | ConvertTo-Json -Depth 5 -Compress
  Write-Output "客户端配置    : $clientConfig"

  # ── 4. 用 stdio 转发器走一遍真实客户端的路 ─────────────────────────
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $forwarder
  $psi.RedirectStandardInput = $true
  $psi.RedirectStandardOutput = $true
  $psi.UseShellExecute = $false
  $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
  $fwd = [System.Diagnostics.Process]::Start($psi)

  function Send($json) { $fwd.StandardInput.WriteLine($json); $fwd.StandardInput.Flush() }
  function Receive([int] $timeoutMs = 60000) {
    $task = $fwd.StandardOutput.ReadLineAsync()
    if (-not $task.Wait($timeoutMs)) { throw '等 stdio 应答超时' }
    if ($null -eq $task.Result) { throw '转发器把 stdout 关了' }
    return ($task.Result | ConvertFrom-Json)
  }

  Send '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"check-mcp","version":"0"}}}'
  $init = Receive
  Write-Output "stdio 握手   : $($init.result.serverInfo.name) v$($init.result.serverInfo.version) / protocol $($init.result.protocolVersion)"

  Send '{"jsonrpc":"2.0","method":"notifications/initialized"}'
  Send '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
  $tools = Receive
  Write-Output "stdio 工具   : $($tools.result.tools.Count) 个"

  Send '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"tamias_document_info","arguments":{}}}'
  $before = (Receive).result.content[0].text | ConvertFrom-Json
  Write-Output "先读一下     : 文档 '$($before.name)'，实体 $($before.entityCount) 个"

  Send '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"tamias_create_wall","arguments":{"points":[{"x":0,"y":0,"z":0},{"x":5,"y":0,"z":0}],"thickness":0.2,"height":3}}}'
  $wall = Receive
  Write-Output "建一面墙     : isError=$($wall.result.isError) $($wall.result.content[0].text)"

  Send '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"tamias_document_info","arguments":{}}}'
  $after = (Receive).result.content[0].text | ConvertFrom-Json
  Write-Output "再看一眼     : 实体 $($after.entityCount) 个 -> $($after.byKind | ConvertTo-Json -Compress)"

  Send '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"tamias_undo","arguments":{}}}'
  $undone = Receive
  Write-Output "撤销         : $($undone.result.content[0].text)"

  Write-Output ''
  if ($Policy -eq 'read-only') {
    Write-Output '上面那次建墙被策略拒绝了 —— 这就是 --mcp-policy=read-only 的预期行为。'
  } else {
    Write-Output '通了：AI 能读文档、能改文档，而且一次调用只占一步撤销（Ctrl+Z 就退回去）。'
  }
  Write-Output '默认策略是 read-only；--mcp-policy=ask 会在每次写操作前弹确认框（要人在 Tamias 里点）。'
  Write-Output '这次改动只在内存里，样例文件没被保存。'
  Write-Output '日常用法：保持 Tamias 开着，把上面那行客户端配置粘进 Claude Desktop / Cursor 再重启它。'
}
finally {
  if ($fwd -and -not $fwd.HasExited) {
    try { $fwd.StandardInput.Close() } catch { }
    if (-not $fwd.WaitForExit(3000)) { Stop-Quietly $fwd }
  }
  if (-not $KeepOpen) {
    Stop-Quietly $tamiasProc
    Remove-Item -LiteralPath $discovery -Force -ErrorAction SilentlyContinue
  } elseif ($tamiasProc) {
    Write-Output "保留 Tamias 运行中（pid $($tamiasProc.Id)），桥地址见上面的发现文件。"
  }
}
