# 把 Meshy 的 API key 写进 Windows 凭据管理器（槽位 tamias/meshy/api-key）。
#
# 为什么需要专门一个脚本：cmdkey /generic 会把口令按 UTF-16 塞进 CredentialBlob，
# 而 tamias 按 UTF-8 读（见 src/app/base/secret_store.cpp），读出来是 NUL 交错的乱码——
# 请求会被服务端拒掉，但应用侧看起来「已经配置好了」。这里按应用的写法写 UTF-8 blob。
#
# 用法：
#   pwsh -NoProfile -File scripts\set-meshy-key.ps1             # 交互输入，不回显
#   pwsh -NoProfile -File scripts\set-meshy-key.ps1 -Remove     # 删除
#   pwsh -NoProfile -File scripts\set-meshy-key.ps1 -Key msy_…  # 非交互（注意命令行历史）
#
# 面板做好之后（docs/PLAN-MESHY.md §6 的 M3）设置里有正式的入口，这个脚本留给脚本 / CI 用。
param(
  [string]$Key,
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$slot = 'tamias/meshy/api-key'

# 字段与 src/app/base/secret_store.cpp 的 CredWriteW 对齐：
# CRED_TYPE_GENERIC=1、CRED_PERSIST_LOCAL_MACHINE=2、UserName="tamias"、UTF-8 blob。
Add-Type -Namespace Tamias -Name MeshyCredential -MemberDefinition @'
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
private static extern bool CredReadW(string target, uint type, uint flags,
                                     out System.IntPtr credential);

[System.Runtime.InteropServices.DllImport("advapi32.dll",
    CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
private static extern bool CredDeleteW(string target, uint type, uint flags);

[System.Runtime.InteropServices.DllImport("advapi32.dll")]
private static extern void CredFree(System.IntPtr buffer);

public static bool Write(string target, string secret) {
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

public static string Read(string target) {
    System.IntPtr ptr;
    if (!CredReadW(target, 1, 0, out ptr)) { return null; }
    try {
        CREDENTIAL cred = (CREDENTIAL)System.Runtime.InteropServices.Marshal.PtrToStructure(
            ptr, typeof(CREDENTIAL));
        byte[] blob = new byte[cred.CredentialBlobSize];
        System.Runtime.InteropServices.Marshal.Copy(cred.CredentialBlob, blob, 0, blob.Length);
        return System.Text.Encoding.UTF8.GetString(blob);
    } finally {
        CredFree(ptr);
    }
}

public static bool Delete(string target) { return CredDeleteW(target, 1, 0); }
'@

function Get-LastError {
  [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
}

if ($Remove) {
  if ([Tamias.MeshyCredential]::Delete($slot)) {
    Write-Output "已删除凭据：$slot"
  } else {
    Write-Output "删除失败或本来就不存在：$slot（error $(Get-LastError)）"
  }
  return
}

if ([string]::IsNullOrWhiteSpace($Key)) {
  $secure = Read-Host -Prompt 'Meshy API key（输入不回显）' -AsSecureString
  $Key = [System.Net.NetworkCredential]::new('', $secure).Password
}
$Key = $Key.Trim()
if ([string]::IsNullOrEmpty($Key)) {
  throw '没有拿到密钥（空输入）。'
}

if (-not [Tamias.MeshyCredential]::Write($slot, $Key)) {
  throw "写凭据失败：$slot（error $(Get-LastError)）"
}
# 读回来核对长度：只报长度，不打印密钥本身。
$read_back = [Tamias.MeshyCredential]::Read($slot)
if ($null -eq $read_back -or $read_back.Length -ne $Key.Length) {
  throw "写入后读回不一致（槽位 $slot）。"
}
Write-Output "已写入凭据：$slot（$($Key.Length) 字符，读回一致）"
Write-Output '现在可以用了：'
Write-Output '  .\build\bin\Debug\tamias.exe --meshy-prompt="a small wooden chair" .\assets\samples\cube.obj'
