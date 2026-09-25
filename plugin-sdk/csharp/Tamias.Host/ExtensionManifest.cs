using System.Text.Json;

namespace Tamias.Host;

// extension.json —— 目录式扩展的清单。
//
// **整个文件是可选的**：缺了就从目录名推断 id / 名称，入口按 main.cs 找。
// 这是 SketchUp 的 main.rb 那套路数——约定优先，清单只用来补充说明。
sealed class ExtensionManifest
{
    public const string FileName = "extension.json";
    public const string DefaultEntry = "main.cs";

    public string? Id { get; set; }
    public string? Name { get; set; }
    public string? Version { get; set; }
    public string? Author { get; set; }
    public string? Description { get; set; }
    public string? ReleaseDate { get; set; }
    public string? Homepage { get; set; }
    // 图标与入口都相对扩展目录；入口缺省 main.cs。
    public string? Icon { get; set; }
    public string? Entry { get; set; }

    public static ExtensionManifest? Read(string path, Action<string> warn)
    {
        try
        {
            return JsonSerializer.Deserialize<ExtensionManifest>(File.ReadAllText(path), Options)
                   ?? new ExtensionManifest();
        }
        catch (Exception ex)
        {
            warn($"Ignoring malformed {FileName} at {path}: {ex.Message}");
            return null;
        }
    }

    static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,  // 手写的清单允许注释与尾逗号
        AllowTrailingCommas = true,
    };
}
