namespace Tamias.Host;

enum ExtensionKind
{
    // 预编译：直接是一个 .dll（老的插件形态，或者目录里放 dll）。
    Precompiled,
    // 源码：目录里有 main.cs（或清单指定的入口），加载时用 Roslyn 编译。
    Source,
}

// 扫描出来的一个扩展候选。真正的加载在 ExtensionLoader 里。
sealed class ExtensionCandidate
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required ExtensionKind Kind { get; init; }
    // 预编译是 DLL 路径，源码是入口 .cs 路径。
    public required string Path { get; init; }
    // 扩展自己所在的那层目录（源码是扩展目录，预编译是 DLL 所在目录）。
    public required string Directory { get; init; }
    public required string Root { get; init; }
    // 来自第一个根（随版本发布的目录）就是内置。**由根决定，不由扩展自报。**
    public required bool BuiltIn { get; init; }
    public ExtensionManifest? Manifest { get; init; }
    public string Version { get; init; } = "";
    public string Author { get; init; } = "";
    public string Description { get; init; } = "";
    public string ReleaseDate { get; init; } = "";
    public string Homepage { get; init; } = "";
    public string IconPath { get; init; } = "";
}

// 扫约定目录，得到要加载的扩展清单（先扫全部、再决定加载谁，这样 id 冲突能提前发现）。
//
// 目录约定（和 SketchUp 的 Plugins/main.rb 同构）：
//
//   <root>/SomeExt.dll              平铺的预编译扩展
//   <root>/SomeExt/main.cs          源码扩展（清单缺省、入口缺省）
//   <root>/SomeExt/extension.json   可选清单：id / 名称 / 版本 / 作者 / 入口 / 图标
//   <root>/SomeExt/SomeExt.dll      目录形式的预编译扩展
//
// 多个根按顺序扫：**后面的根同 id 覆盖前面的**（用户的盖内置的），覆盖时记一条日志。
static class ExtensionScanner
{
    // 只跳过宿主自己那两份运行时程序集和框架程序集。
    // **别写成 "Tamias."**——示例插件就叫 Tamias.Hello / Tamias.Nurbs，会被一起跳掉。
    static readonly string[] SkipPrefixes =
        ["Tamias.Api", "Tamias.Host", "System.", "Microsoft.", "netstandard", "mscorlib"];

    public static List<ExtensionCandidate> Scan(IReadOnlyList<string> roots, Action<string> log,
                                                Action<string> warn)
    {
        var candidates = new List<ExtensionCandidate>();
        for (var i = 0; i < roots.Count; ++i)
        {
            var root = roots[i];
            if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
            {
                warn($"Extension root not found, skipped: {root}");
                continue;
            }
            var builtIn = i == 0;  // 第一个根 = 随版本发布的内置目录
            log($"Scanning extensions in {root}");
            foreach (var dll in Directory.GetFiles(root, "*.dll"))
            {
                if (ShouldSkip(Path.GetFileName(dll)))
                {
                    continue;
                }
                candidates.Add(Precompiled(dll, directory: root, root, builtIn, manifest: null,
                                           fallbackId: Path.GetFileNameWithoutExtension(dll)));
            }
            foreach (var dir in Directory.GetDirectories(root))
            {
                var candidate = FromDirectory(dir, root, builtIn, warn);
                if (candidate is not null)
                {
                    candidates.Add(candidate);
                }
            }
        }
        return Merge(candidates, warn);
    }

    static ExtensionCandidate? FromDirectory(string dir, string root, bool builtIn,
                                             Action<string> warn)
    {
        var folderName = Path.GetFileName(dir);
        var manifestPath = Path.Combine(dir, ExtensionManifest.FileName);
        var manifest = File.Exists(manifestPath) ? ExtensionManifest.Read(manifestPath, warn) : null;

        var entryName = string.IsNullOrWhiteSpace(manifest?.Entry)
                            ? ExtensionManifest.DefaultEntry
                            : manifest!.Entry!;
        var entry = Path.GetFullPath(Path.Combine(dir, entryName));
        // 清单是手写的，别让它把入口指到目录外去。
        var prefix = Path.GetFullPath(dir) + Path.DirectorySeparatorChar;
        if (entry.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && File.Exists(entry))
        {
            return new ExtensionCandidate
            {
                Id = Pick(manifest?.Id, folderName),
                Name = Pick(manifest?.Name, folderName),
                Kind = ExtensionKind.Source,
                Path = entry,
                Directory = dir,
                Root = root,
                BuiltIn = builtIn,
                Manifest = manifest,
                Version = Normalize(manifest?.Version),
                Author = Normalize(manifest?.Author),
                Description = Normalize(manifest?.Description),
                ReleaseDate = Normalize(manifest?.ReleaseDate),
                Homepage = Normalize(manifest?.Homepage),
                IconPath = ResolveIcon(dir, manifest?.Icon),
            };
        }

        var dll = Directory.GetFiles(dir, "*.dll")
                      .FirstOrDefault(f => !ShouldSkip(Path.GetFileName(f)));
        if (dll is not null)
        {
            return Precompiled(dll, dir, root, builtIn, manifest, folderName);
        }

        warn($"Skipping {dir}: no {entryName} and no .dll inside");
        return null;
    }

    static ExtensionCandidate Precompiled(string dll, string directory, string root, bool builtIn,
                                          ExtensionManifest? manifest, string fallbackId)
    {
        return new ExtensionCandidate
        {
            Id = Pick(manifest?.Id, fallbackId),
            Name = Pick(manifest?.Name, fallbackId),
            Kind = ExtensionKind.Precompiled,
            Path = Path.GetFullPath(dll),
            Directory = directory,
            Root = root,
            BuiltIn = builtIn,
            Manifest = manifest,
            Version = Normalize(manifest?.Version),
            Author = Normalize(manifest?.Author),
            Description = Normalize(manifest?.Description),
            ReleaseDate = Normalize(manifest?.ReleaseDate),
            Homepage = Normalize(manifest?.Homepage),
            IconPath = ResolveIcon(directory, manifest?.Icon),
        };
    }

    // 后扫的根覆盖先扫的；被覆盖的那条要留痕，不然「我改了怎么没生效」没法查。
    static List<ExtensionCandidate> Merge(List<ExtensionCandidate> candidates, Action<string> warn)
    {
        var byId = new Dictionary<string, ExtensionCandidate>(StringComparer.OrdinalIgnoreCase);
        var order = new List<string>();
        foreach (var candidate in candidates)
        {
            if (byId.TryGetValue(candidate.Id, out var previous))
            {
                warn($"Extension '{candidate.Id}': {candidate.Directory} overrides {previous.Directory}");
            }
            else
            {
                order.Add(candidate.Id);
            }
            byId[candidate.Id] = candidate;
        }
        return order.Select(id => byId[id]).ToList();
    }

    static bool ShouldSkip(string fileName)
    {
        foreach (var prefix in SkipPrefixes)
        {
            if (fileName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    static string Pick(string? value, string fallback) =>
        string.IsNullOrWhiteSpace(value) ? fallback : value.Trim();

    static string Normalize(string? value) => string.IsNullOrWhiteSpace(value) ? "" : value.Trim();

    static string ResolveIcon(string directory, string? icon) =>
        string.IsNullOrWhiteSpace(icon) ? "" : Path.GetFullPath(Path.Combine(directory, icon));
}
