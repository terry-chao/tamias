# RAG 装配的最小验证（PLAN-RAG §3 M3）：起 Tamias → 走真正的 MCP 桥 →
# 确认检索工具在工具表里、并且真能查到带 url 的片段。
#
#   pwsh -File scripts/check-rag.ps1
#   pwsh -File scripts/check-rag.ps1 -Config RelWithDebInfo -Query "怎么建墙"
#
# 这里直连 loopback HTTP，不经 stdio 转发器 —— 转发器那一段由 check-mcp.ps1 覆盖，
# RAG 加的是后端不是传输，两者正交。
param(
  [string]$Config = "Debug",
  [string]$Query = "怎么建墙",
  [int]$K = 3,
  # 默认只读：检索是读工具，这一档下本来就该放行 —— 顺带验了 PLAN-RAG M2 的验收点。
  [ValidateSet('read-only', 'auto')]
  [string]$Policy = "read-only"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $root "build\bin\$Config"
$tamias = Join-Path $binDir 'tamias.exe'
$discovery = Join-Path $env:APPDATA 'tamias\tamias\mcp.json'

if (-not (Test-Path $tamias)) {
  throw "找不到 $tamias。先构建：pwsh -File scripts/build-tests.ps1 -Preset $($Config.ToLower()) -Targets tamias"
}

$tamiasProc = $null
try {
  Remove-Item -LiteralPath $discovery -Force -ErrorAction SilentlyContinue

  $sample = Join-Path $root 'assets\samples\cube.obj'
  $arguments = @('--mcp', "--mcp-policy=$Policy")
  if (Test-Path $sample) { $arguments += $sample }
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
  if (-not $bridge) { throw "等不到发现文件 $discovery" }

  $headers = @{ Authorization = "Bearer $($bridge.token)" }
  $endpoint = "http://127.0.0.1:$($bridge.port)/mcp"
  Write-Output "桥           : $endpoint   策略 $Policy"

  $nextId = 1
  function Call-Tool([string]$name, $arguments) {
    $body = @{
      jsonrpc = '2.0'; id = $script:nextId++; method = 'tools/call'
      params  = @{ name = $name; arguments = $arguments }
    } | ConvertTo-Json -Depth 10 -Compress
    return Invoke-RestMethod -Uri $endpoint -Method Post -Headers $headers `
      -ContentType 'application/json' -Body ([System.Text.Encoding]::UTF8.GetBytes($body))
  }

  $listBody = @{ jsonrpc = '2.0'; id = $nextId++; method = 'tools/list'; params = @{} } |
    ConvertTo-Json -Depth 5 -Compress
  $tools = Invoke-RestMethod -Uri $endpoint -Method Post -Headers $headers `
    -ContentType 'application/json' -Body ([System.Text.Encoding]::UTF8.GetBytes($listBody))
  $names = $tools.result.tools | ForEach-Object { $_.name }
  Write-Output "工具表       : $($names.Count) 个"
  if ($names -notcontains 'tamias_search_docs') {
    throw "工具表里没有 tamias_search_docs —— 索引没装配上（先跑 scripts/rag/build_index.py）"
  }

  $result = Call-Tool 'tamias_search_docs' @{ query = $Query; k = $K }
  if ($result.result.isError) { throw "检索报错：$($result.result.content[0].text)" }
  $payload = $result.result.content[0].text | ConvertFrom-Json
  Write-Output "检索「$Query」: 命中 $($payload.count) 条"
  if ($payload.count -eq 0) { throw '一条都没命中' }

  foreach ($hit in $payload.results) {
    Write-Output ("  {0,7:N2}  {1}#{2}" -f $hit.score, $hit.path, $hit.anchor)
    Write-Output "           $($hit.url)"
    if (-not $hit.url) { throw '结果里没有 url' }
    if ($hit.snippet -notmatch '<untrusted_doc>') { throw '片段没有包在不可信边界里' }
  }

  # 只读档下写工具仍应被拒 —— 确认检索放行不是把闸门整个绕过去了。
  $write = Call-Tool 'tamias_create_wall' @{ points = @(@{x=0;y=0;z=0}, @{x=1;y=0;z=0}) }
  if ($Policy -eq 'read-only') {
    if ($write.result.content[0].text -notmatch '只读') {
      throw '只读档下写工具没有被拒 —— 闸门可能被检索后端带偏了'
    }
    Write-Output '只读档       : 检索放行、写操作仍被拒 ✓'
  }

  Write-Output ''
  Write-Output '通了：检索工具出现在 tools/list，结果带可点的 url，且没有放宽写策略。'
}
finally {
  if ($tamiasProc -and -not $tamiasProc.HasExited) {
    try { Stop-Process -Id $tamiasProc.Id -Force } catch { }
  }
  Remove-Item -LiteralPath $discovery -Force -ErrorAction SilentlyContinue
}
