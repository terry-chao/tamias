# 端到端验证 Meshy 接入：假的 Meshy 服务端 + 真的 tamias.exe，走通
# 「提示词 → 建任务 → 轮询 → 下载 GLB → 作为导入网格进当前文档」这条回路。
#
# 用的是调试入口 --meshy-prompt（面板还没做，见 docs/PLAN-MESHY.md §6 的 M3）。
# 脚本会临时改两处用户状态，结束都会还原：<exe>/config/tamias.ini 的 [meshy] 段、
# 凭据管理器里的 tamias/meshy/api-key。
#
# 用法：pwsh -NoProfile -File scripts\check-meshy.ps1
param(
  [string]$Prompt = 'a small wooden chair',
  [int]$Port = 52991
)

$ErrorActionPreference = 'Stop'

$root = 'C:\dev\tamias'
$tamias = Join-Path $root 'build\bin\Debug\tamias.exe'
$ini = Join-Path $root 'build\bin\Debug\config\tamias.ini'
$sample = Join-Path $root 'assets\samples\cube.obj'
$server = Join-Path $root 'scripts\fake-meshy.py'
# 运行产物留在 build/（那个目录是 gitignore 的），脚本本身在 scripts/。
$log = Join-Path $root 'build\fake-meshy.jsonl'
$err = Join-Path $root 'build\fake-meshy.app.err.txt'
$slot = 'tamias/meshy/api-key'
$key = 'msy_fake_key_for_verify'

$appProc = $null
$pyProc = $null
$iniOriginal = $null

function Write-Ini([string] $text) {
  [System.IO.File]::WriteAllText($ini, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# 凭据必须按**应用自己的约定**写：UTF-8 的 blob。
# cmdkey /generic 会把口令按 UTF-16 塞进 CredentialBlob，于是应用按 UTF-8 读出来的
# 是 NUL 交错的乱码（踩过一次：请求头里的 key 直接变得没法用，服务端回 401）。
# 这里的字段与 src/app/base/secret_store.cpp 的 CredWriteW 对齐：
# CRED_TYPE_GENERIC=1、CRED_PERSIST_LOCAL_MACHINE=2、UserName="tamias"。
Add-Type -Namespace Verify -Name Cred -MemberDefinition @'
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential,
                                             CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public struct CREDENTIAL {
    public uint Flags;
    public uint Type;
    public string TargetName;
    public string Comment;
    public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
    public uint CredentialBlobSize;
    public System.IntPtr CredentialBlob;
    public uint Persist;
    public uint AttributeCount;
    public System.IntPtr Attributes;
    public string TargetAlias;
    public string UserName;
}

[System.Runtime.InteropServices.DllImport("advapi32.dll",
    CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
private static extern bool CredWriteW(ref CREDENTIAL credential, uint flags);

[System.Runtime.InteropServices.DllImport("advapi32.dll",
    CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
private static extern bool CredDeleteW(string target, uint type, uint flags);

public static bool WriteSecret(string target, string secret) {
    byte[] blob = System.Text.Encoding.UTF8.GetBytes(secret);
    System.IntPtr buffer = System.Runtime.InteropServices.Marshal.AllocHGlobal(blob.Length);
    try {
        System.Runtime.InteropServices.Marshal.Copy(blob, 0, buffer, blob.Length);
        CREDENTIAL cred = new CREDENTIAL();
        cred.Type = 1;
        cred.TargetName = target;
        cred.CredentialBlobSize = (uint)blob.Length;
        cred.CredentialBlob = buffer;
        cred.Persist = 2;
        cred.UserName = "tamias";
        return CredWriteW(ref cred, 0);
    } finally {
        System.Runtime.InteropServices.Marshal.FreeHGlobal(buffer);
    }
}

public static bool DeleteSecret(string target) {
    return CredDeleteW(target, 1, 0);
}
'@

try {
  # 1) 设置：备份 ini，剥掉旧的 [meshy] 段，写一段指向假服务端的。
  if (Test-Path -LiteralPath $ini) { $iniOriginal = Get-Content -LiteralPath $ini -Raw }
  $kept = New-Object System.Collections.Generic.List[string]
  $skipping = $false
  foreach ($line in ($iniOriginal -split "`r?`n")) {
    if ($line -match '^\s*\[') { $skipping = $line -match '^\s*\[meshy\]' }
    if (-not $skipping) { $kept.Add($line) }
  }
  $kept.Add('[meshy]')
  $kept.Add("base_url=http://127.0.0.1:$Port/openapi/v2")
  $kept.Add('model=')
  Write-Ini ($kept -join "`r`n")
  Write-Output "ini     : $ini（已临时加 [meshy]）"

  # 2) 密钥走凭据管理器（与面板同一个槽位）。
  if (-not [Verify.Cred]::WriteSecret($slot, $key)) {
    throw "写凭据失败：$slot（error $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())）"
  }
  Write-Output "cred    : $slot（临时写入，UTF-8 blob）"

  # 3) 假服务端。
  Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
  $pyProc = Start-Process -FilePath 'python' -ArgumentList @($server, $Port, $log) `
    -PassThru -WindowStyle Hidden
  Start-Sleep -Milliseconds 1200
  Write-Output "fake svc: pid=$($pyProc.Id) port=$Port"

  # 4) 起应用：带上样例文档（导入要有活动视口），并让它自己发提示词。
  Remove-Item -LiteralPath $err -Force -ErrorAction SilentlyContinue
  $argumentLine = '--meshy-prompt="' + $Prompt + '" "' + $sample + '"'
  $appProc = Start-Process -FilePath $tamias -ArgumentList $argumentLine `
    -PassThru -WindowStyle Hidden -RedirectStandardError $err
  Write-Output "tamias  : pid=$($appProc.Id)"

  # 5) 等它跑完（建任务 + 两轮轮询 + 下载 + 导入，约 10 秒）。
  $deadline = (Get-Date).AddSeconds(60)
  $imported = $false
  while ((Get-Date) -lt $deadline) {
    if ($appProc.HasExited) { throw "tamias 提前退出（exit $($appProc.ExitCode)）" }
    $text = if (Test-Path $err) { Get-Content -LiteralPath $err -Raw } else { '' }
    # 成功判据：导入落地的审计行（`Meshy: imported … as node …`）。只有
    # import_mesh_file 返回了非 0（命令真的执行、节点真的建出来）才会打这一行。
    if ($text -match 'Meshy: imported') { $imported = $true; break }
    if ($text -match 'Meshy failed') { break }
    Start-Sleep -Milliseconds 400
  }
  Start-Sleep -Milliseconds 500
  $text = if (Test-Path $err) { Get-Content -LiteralPath $err -Raw } else { '' }

  # 6) 断言：服务端收到了哪些请求 + 应用侧的结论。
  $records = if (Test-Path $log) {
    @(Get-Content $log | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
  } else { @() }
  $create = $records | Where-Object { $_.method -eq 'POST' } | Select-Object -First 1
  $polls = @($records | Where-Object { $_.path -like '*/text-to-3d/*' })
  $download = $records | Where-Object { $_.path -eq '/asset/model.glb' } | Select-Object -First 1

  Write-Output ''
  Write-Output "建任务   : authorized=$($create.authorized) prompt=$($create.body.prompt) mode=$($create.body.mode)"
  Write-Output "轮询次数 : $($polls.Count)"
  Write-Output "下载请求 : $(if ($download) { '已发出 /asset/model.glb' } else { '没有' })"
  Write-Output "应用日志 : $(($text -split "`r?`n" | Where-Object { $_ -match 'Meshy' }) -join ' | ')"
  Write-Output ''

  if (-not $create) { throw '服务端一次建任务请求都没收到' }
  if (-not $create.authorized) { throw '建任务请求没带 Bearer 头（密钥没从凭据管理器读出来）' }
  if ($create.body.mode -ne 'preview') { throw "建任务的 mode 不是 preview：$($create.body.mode)" }
  if ($polls.Count -lt 2) { throw "只轮询了 $($polls.Count) 次，没走到终态" }
  if (-not $download) { throw '没有发出下载模型的请求' }
  if (-not $imported) { throw "导入没成功；应用日志见上（$err）" }

  Write-Output '回路结论：提示词 → 建任务 → 轮询 → 下载 → 导入 这条链是通的。'
}
finally {
  if ($appProc -and -not $appProc.HasExited) { Stop-Process -Id $appProc.Id -Force }
  if ($pyProc -and -not $pyProc.HasExited) { Stop-Process -Id $pyProc.Id -Force }
  [Verify.Cred]::DeleteSecret($slot) | Out-Null
  if ($null -ne $iniOriginal) {
    Write-Ini $iniOriginal
  } else {
    Remove-Item -LiteralPath $ini -Force -ErrorAction SilentlyContinue
  }
  # 下载落盘位置和缩略图/脚本同一个 AppData 目录，测试产物顺手清掉。
  $downloaded = Join-Path $env:APPDATA 'tamias\tamias\meshy'
  # 只认这个明确的目标：解析后的绝对路径必须以 tamias\meshy 结尾，避免删错地方。
  if ((Test-Path -LiteralPath $downloaded) -and
      ([System.IO.Path]::GetFullPath($downloaded).TrimEnd('\') -like '*\tamias\tamias\meshy')) {
    Remove-Item -LiteralPath $downloaded -Recurse -Force
    Write-Output "removed  : $downloaded"
  }
  Write-Output 'cleanup done（ini 与凭据已还原）'
}
