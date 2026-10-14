# Geometry envelope digest tool - the "which side computed this?" instrument.
#
# The browser (wasm), the .NET geometry service and the desktop app each own a piece of
# the same feature model. When their results disagree, do NOT eyeball pixels: hash the
# three sources and see which one already differs (docs/DEBUG-BROWSER-STACK.md).
#
#   1. feature-model blob hash  -> the input left the browser intact
#   2. per-mesh blob hash       -> the server evaluated the same triangles
#   3. .trscn frame digest      -> the renderer drew what it was given
#
# This script does 1 (echoed) and 2, either against the running service or offline.
#
#   # end to end: wrap a feature-model blob, POST it, print the reply summary
#   powershell -File scripts/geometry-digest.ps1 -Request tree.bin -SaveReply reply.bin
#
#   # offline: summarize an already captured reply (regression / CI / diff two runs)
#   powershell -File scripts/geometry-digest.ps1 -Summarize reply.bin
#
#   # machine readable, for a golden test
#   powershell -File scripts/geometry-digest.ps1 -Summarize reply.bin -Json
#
# The wire format is defined in GEOMETRY-SERVICE-PLAN.md sections 5.2 / 5.3 and is NOT
# implemented yet (that plan's P0 is the first unbuilt phase). Until then the POST fails
# and this script says so - a 404 here means "P0 not done", not "the tool is broken".
#
# NOTE: keep this file ASCII-only. scripts\*.cmd launches .ps1 with Windows PowerShell 5.1,
# which reads a BOM-less script using the console codepage (GBK here), so non-ASCII text
# would be mangled and break parsing.
param(
  # Request envelope (TGEV) to POST. Mutually exclusive with -Summarize.
  [string]$Request,
  # Response envelope (TGEM) to parse offline. Mutually exclusive with -Request.
  [string]$Summarize,
  [string]$Url = "http://127.0.0.1:5199",
  [int]$Port = 0,
  [double]$Deflection = 0.05,
  [int]$Flags = 0,
  # Tag echoed back by one single-blob request envelope.
  [long]$Tag = 1,
  # Prepend the algorithm name to each mesh digest, so pasting it into a report says which
  # hash it is. The digest itself is always per mesh (one line per LOD).
  [switch]$PerBlob,
  [string]$SaveReply,
  [switch]$Json
)

$ErrorActionPreference = "Stop"

# -- little-endian cursor (BinaryReader in binary_archive.h is explicit little-endian) --

class TamCursor {
  [byte[]]$Data
  [int]$Pos

  TamCursor([byte[]]$data) {
    $this.Data = $data
    $this.Pos = 0
  }

  [int] Remaining() { return $this.Data.Length - $this.Pos }

  [void] Need([int]$n, [string]$what) {
    if ($n -gt $this.Remaining()) {
      throw ("truncated at offset {0}: need {1} bytes for {2}, {3} left" -f `
        $this.Pos, $n, $what, $this.Remaining())
    }
  }

  [uint32] U32() {
    $this.Need(4, "u32")
    $v = [System.BitConverter]::ToUInt32($this.Data, $this.Pos)
    $this.Pos += 4
    return $v
  }

  [uint16] U16() {
    $this.Need(2, "u16")
    $v = [System.BitConverter]::ToUInt16($this.Data, $this.Pos)
    $this.Pos += 2
    return $v
  }

  [uint64] U64() {
    $this.Need(8, "u64")
    $v = [System.BitConverter]::ToUInt64($this.Data, $this.Pos)
    $this.Pos += 8
    return $v
  }

  [double] F64() {
    $this.Need(8, "f64")
    $v = [System.BitConverter]::ToDouble($this.Data, $this.Pos)
    $this.Pos += 8
    return $v
  }

  [byte[]] Bytes([int]$n) {
    $this.Need($n, "bytes")
    $out = New-Object byte[] $n
    [System.Array]::Copy($this.Data, $this.Pos, $out, 0, $n)
    $this.Pos += $n
    return $out
  }

  [string] Magic() {
    $this.Need(4, "magic")
    $s = [System.Text.Encoding]::ASCII.GetString($this.Data, $this.Pos, 4)
    $this.Pos += 4
    return $s
  }
}

function Get-Sha256Hex([byte[]]$Bytes) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    return ([System.BitConverter]::ToString($sha.ComputeHash($Bytes)) -replace '-', '').ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

function Get-HexPreview([byte[]]$Bytes, [int]$Count) {
  $n = [Math]::Min($Count, $Bytes.Length)
  if ($n -le 0) { return "" }
  return ([System.BitConverter]::ToString($Bytes, 0, $n) -replace '-', '').ToLowerInvariant()
}

# mesh blob = write_mesh_cpu() (mesh_binary.cpp): u64 vertex count, vertices, u64 index
# count, indices, then the AABB. Only the two counts are read here - they are what the
# eye compares first ("the server returned 0 triangles").
function Read-MeshCounts([byte[]]$Blob) {
  if ($Blob.Length -lt 16) { return $null }
  $verts = [System.BitConverter]::ToUInt64($Blob, 0)
  $vertexBytes = [uint64]8 + ($verts * [uint64]44)
  if ($vertexBytes + [uint64]8 -gt [uint64]$Blob.Length) { return $null }
  $idx = [System.BitConverter]::ToUInt64($Blob, [int]$vertexBytes)
  return @{ vertices = $verts; indices = $idx }
}

function New-RequestEnvelope([byte[]]$Blob, [long]$Tag, [double]$Deflection, [int]$Flags) {
  $size = 4 + 2 + 4 + 8 + 4 + 8 + 8 + $Blob.Length
  $buf = New-Object byte[] $size
  [System.Array]::Copy([System.Text.Encoding]::ASCII.GetBytes("TGEV"), 0, $buf, 0, 4)
  [System.Array]::Copy([System.BitConverter]::GetBytes([uint16]1), 0, $buf, 4, 2)
  [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$Flags), 0, $buf, 6, 4)
  [System.Array]::Copy([System.BitConverter]::GetBytes([double]$Deflection), 0, $buf, 10, 8)
  [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]1), 0, $buf, 18, 4)
  [System.Array]::Copy([System.BitConverter]::GetBytes([uint64]([long]$Tag)), 0, $buf, 22, 8)
  [System.Array]::Copy([System.BitConverter]::GetBytes([uint64]([long]$Blob.Length)), 0, $buf, 30, 8)
  [System.Array]::Copy($Blob, 0, $buf, 38, $Blob.Length)
  return $buf
}

function Read-ResponseEnvelope([byte[]]$Bytes) {
  $c = [TamCursor]::new($Bytes)
  $magic = $c.Magic()
  if ($magic -ne "TGEM") {
    $preview = Get-HexPreview $Bytes 32
    $text = ""
    if ($Bytes.Length -gt 0 -and $Bytes[0] -eq 0x7B) {
      $text = [System.Text.Encoding]::UTF8.GetString($Bytes)
    }
    throw ("not a TGEM response envelope (magic '{0}', first bytes {1}){2}" -f `
      $magic, $preview, $(if ($text) { "`n  body: $text" } else { "" }))
  }

  $version = [int]$c.U16()     # u16, and the only field whose width is easy to get wrong
  $status = $c.U32()
  $count = $c.U32()
  $message = ""
  if ($status -ne 0) {
    # Per GEOMETRY-SERVICE-PLAN.md section 5.4 a failing batch still returns HTTP 200 with
    # per-item ok=0; a non-zero envelope status is a request-level error carrying a string.
    # The plan fixes the field order but not this string's width, so mirror the codec:
    # u64 length + raw bytes (BinaryWriter::write_string).
    $len = [int]$c.U64()
    $message = [System.Text.Encoding]::UTF8.GetString($c.Bytes($len))
  }

  $items = New-Object System.Collections.ArrayList
  for ($i = 0; $i -lt [int]$count; $i++) {
    $tag = $c.U64()
    $ok = $c.Bytes(1)[0]
    $meshCount = [int]$c.U32()
    $meshes = New-Object System.Collections.ArrayList
    for ($m = 0; $m -lt $meshCount; $m++) {
      $lod = $c.Bytes(1)[0]
      $bounds = @()
      for ($b = 0; $b -lt 6; $b++) { $bounds += $c.F64() }
      $blobLen = [int]$c.U64()
      $blob = $c.Bytes($blobLen)
      $counts = Read-MeshCounts $blob
      [void]$meshes.Add([pscustomobject]@{
        lod      = [int]$lod
        bounds   = $bounds
        bytes    = $blob.Length
        digest   = Get-Sha256Hex $blob
        vertices = $(if ($counts) { [long]$counts.vertices } else { $null })
        indices  = $(if ($counts) { [long]$counts.indices } else { $null })
      })
    }
    [void]$items.Add([pscustomobject]@{
      tag    = [long]$tag
      ok     = ($ok -ne 0)
      meshes = $meshes
    })
  }

  return [pscustomobject]@{
    magic   = $magic
    version = $version
    status  = [long]$status
    message = $message
    count   = [int]$count
    items   = $items
    rest    = $c.Remaining()
  }
}

function Format-Response($Response, [bool]$PerBlob) {
  $lines = New-Object System.Collections.ArrayList
  [void]$lines.Add(("envelope  {0} version {1} status {2} count {3}" -f `
    $Response.magic, $Response.version, $Response.status, $Response.count))
  if ($Response.message) { [void]$lines.Add("message   $($Response.message)") }
  foreach ($item in $Response.items) {
    [void]$lines.Add(("item tag {0} ok {1} meshes {2}" -f $item.tag, $item.ok, $item.meshes.Count))
    foreach ($mesh in $item.meshes) {
      $b = $mesh.bounds
      [void]$lines.Add(("  lod {0}  verts {1}  tris {2}  bytes {3}" -f `
        $mesh.lod, $mesh.vertices, $(if ($null -ne $mesh.indices) { [int]($mesh.indices / 3) } else { "?" }), $mesh.bytes))
      [void]$lines.Add(("    bounds  min ({0:g6}, {1:g6}, {2:g6})  max ({3:g6}, {4:g6}, {5:g6})" -f `
        $b[0], $b[1], $b[2], $b[3], $b[4], $b[5]))
      [void]$lines.Add(("    digest  {0}" -f $(if ($PerBlob) { "sha256:$($mesh.digest)" } else { $mesh.digest })))
    }
  }
  if ($Response.rest -gt 0) {
    [void]$lines.Add(("WARNING   {0} unparsed trailing bytes - the envelope version is newer than this script" -f $Response.rest))
  }
  return $lines
}

function Fail-Parse([string]$Why) {
  # A malformed or truncated envelope is a data problem, not a script crash: report one
  # line and exit non-zero so a CI step fails on the message rather than on a stack trace.
  Write-Host "cannot parse the response envelope: $Why" -ForegroundColor Red
  exit 1
}

# -- input validation -----------------------------------------------------------------

if ($Request -and $Summarize) {
  throw "-Request and -Summarize are mutually exclusive (one POSTs, the other parses a file)."
}
if (-not $Request -and -not $Summarize) {
  throw "nothing to do: pass -Request <tree.bin> to POST, or -Summarize <reply.bin> to parse."
}
if ($Request -and -not (Test-Path -LiteralPath $Request)) { throw "request blob not found: $Request" }
if ($Summarize -and -not (Test-Path -LiteralPath $Summarize)) { throw "reply envelope not found: $Summarize" }

if ($Port -gt 0) { $Url = ("http://127.0.0.1:{0}" -f $Port) }
$Url = $Url.TrimEnd('/')

# -- offline mode ---------------------------------------------------------------------

if ($Summarize) {
  $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Summarize).Path)
  try {
    $response = Read-ResponseEnvelope $bytes
  } catch {
    Fail-Parse $_.Exception.Message
  }
  if ($Json) {
    $response | ConvertTo-Json -Depth 6
  } else {
    Format-Response $response ([bool]$PerBlob)
  }
  exit 0
}

# -- POST mode ------------------------------------------------------------------------

$blob = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Request).Path)
$envelope = New-RequestEnvelope $blob $Tag $Deflection $Flags
$endpoint = "$Url/v1/geometry/evaluate"

if (-not $Json) {
  Write-Host ("request   {0}  {1} bytes  (feature-model blob {2} bytes, digest {3})" -f `
    $endpoint, $envelope.Length, $blob.Length, (Get-Sha256Hex $blob))
  Write-Host ("          deflection {0}  flags {1}  tag {2}" -f $Deflection, $Flags, $Tag)
}

if (-not ("System.Net.Http.HttpClient" -as [type])) {
  # Windows PowerShell 5.1 does not preload System.Net.Http; PowerShell 7 does. Without this
  # the type lookup throws and the POST path dies before the server ever hears from us.
  Add-Type -AssemblyName System.Net.Http | Out-Null
}

try {
  # NOT Invoke-WebRequest: with a byte-array body PowerShell 7 serializes it as the string
  # "84 71 69 86 ..." (one decimal number per byte) instead of sending raw bytes, and the
  # behaviour differs between 5.1 and 7.x. ByteArrayContent is the only version-proof way
  # to put an opaque binary envelope on the wire.
  $handler = New-Object System.Net.Http.HttpClientHandler
  # HttpClientHandler.Timeout does not exist on .NET Framework; HttpClient.Timeout does,
  # and it is the one that actually governs the wait.
  $client = New-Object System.Net.Http.HttpClient($handler)
  $client.Timeout = [TimeSpan]::FromSeconds(60)
  try {
    $content = New-Object System.Net.Http.ByteArrayContent(@(, $envelope))
    $content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/octet-stream")
    $httpResponse = $client.PostAsync($endpoint, $content).GetAwaiter().GetResult()
    # Read as bytes, never as a string: the reply envelope is binary and would be mangled
    # by any default text encoding.
    $reply = [byte[]]$httpResponse.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
    $statusCode = [int]$httpResponse.StatusCode
  } finally {
    $client.Dispose()
    $handler.Dispose()
  }
  if ($statusCode -ne 200) {
    Write-Host ("POST {0} -> HTTP {1}" -f $endpoint, $statusCode) -ForegroundColor Red
    if ($reply.Length -gt 0) {
      Write-Host ("  body: {0}" -f [System.Text.Encoding]::UTF8.GetString($reply))
    }
    if ($statusCode -eq 404) {
      Write-Host "  /v1/geometry/evaluate is not implemented yet: GEOMETRY-SERVICE-PLAN.md P0 is"
      Write-Host "  still unbuilt (today the service only exposes the /v1/sessions ABI). Nothing is"
      Write-Host "  wrong with this script - it works the moment the endpoint exists."
    }
    exit 1
  }
} catch {
  Write-Host ("POST {0} failed: {1}" -f $endpoint, $_.Exception.Message) -ForegroundColor Red
  Write-Host "  no usable HTTP response at all - is the geometry server up? (GET $Url/health)"
  exit 1
}
if ($SaveReply) {
  # Resolve against the CURRENT directory: Join-Path alone keeps a relative path relative,
  # and .NET's WriteAllBytes then resolves it against the process directory instead.
  $replyPath = if ([System.IO.Path]::IsPathRooted($SaveReply)) {
    $SaveReply
  } else {
    Join-Path (Get-Location).Path $SaveReply
  }
  [System.IO.File]::WriteAllBytes($replyPath, $reply)
  if (-not $Json) { Write-Host ("saved     {0}  ({1} bytes)" -f $replyPath, $reply.Length) }
}

try {
  $response = Read-ResponseEnvelope $reply
} catch {
  Fail-Parse $_.Exception.Message
}
if ($Json) {
  $response | ConvertTo-Json -Depth 6
} else {
  Format-Response $response ([bool]$PerBlob)
}

if ($response.status -ne 0) { exit 2 }
exit 0
