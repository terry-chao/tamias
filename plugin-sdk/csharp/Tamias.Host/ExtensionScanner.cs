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
//   <root>/loader.cs                根顶层的总入口：里面 LoadExtension 决定还要装谁
//   <root>/SomeExt/main.cs          源码扩展（清单缺省、入口缺省）
//   <root>/SomeExt/extension.json   可选清单：id / 名称 / 版本 / 作者 / 入口 / 图标
//   <root>/SomeExt/SomeExt.dll      目录形式的预编译扩展
//
// 多个根按顺序扫：**后面的根同 id 覆盖前面的**（用户的盖内置的），覆盖时记一条日志。
static class ExtensionScanner
{
    // 根顶层的 loader.cs：固定约定的总入口。
    // 宿主启动时、以及它一被改动就执行；里面用 host.LoadExtension(path) 决定还要装哪些
    // 工程——那些工程可以放在任何地方，不必在本目录里。
    //
    // **id 按根区分**（<根目录名>.loader）：官方根和用户根各放一个时两个都得跑。
    // 固定成一个 "loader" 的话，后扫的根会覆盖先扫的——用户一写自己的 loader，
    // 官方那个就不执行了。
    public const string LoaderFileName = "loader.cs";

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
            log($"Scanning extensions in {root}");
            // 一个"根"也可能是**文件**：LoadExtension 直接指了某个入口 .cs / .dll 时就是这种。
            candidates.AddRange(File.Exists(root)
                                    ? FromFile(root, builtIn: i == 0, warn)
                                    : ScanRoot(root, builtIn: i == 0, warn));
        }
        return Merge(candidates, warn);
    }

    // 扫一个根。两种布局都认：
    //
    //   <root>/main.cs        根**自己**就是一个扩展（host.LoadExtension 指过来的工程目录
    //                         就是这种，重扫时也得认，否则它会被当成"被删掉了"）
    //   <root>/...            否则看根顶层的 loader.cs、根下平铺的 *.dll、一级子目录
    public static List<ExtensionCandidate> ScanRoot(string root, bool builtIn, Action<string> warn)
    {
        // 根自己是不是一个扩展？**只看入口文件，不看 dll**：约定根（<exe>/plugins）
        // 顶层就平铺着 dll，那要被当成"装着一批扩展"，不是"自己是一个扩展"。
        var rootManifest = ReadManifest(root, warn);
        if (EntryFile(root, rootManifest) is { } selfEntry)
        {
            return [Source(selfEntry, root, root, builtIn, rootManifest, Path.GetFileName(root))];
        }

        var candidates = new List<ExtensionCandidate>();
        var loader = Path.Combine(root, LoaderFileName);
        if (File.Exists(loader))
        {
            // loader 不看清单：id 只跟根走，免得根里随手放个 extension.json 就把它换了名字。
            candidates.Add(Source(loader, root, root, builtIn, manifest: null, LoaderId(root),
                                  LoaderName(root)));
        }
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
        // 空手而归但目录里躺着 .cs：多半是"把工程目录指过来了"。编译型工程的入口是它的
        // **输出**（.dll），源码式的入口得叫 main.cs / 清单里写清楚——说一句，别让人干等。
        if (candidates.Count == 0 && Directory.GetFiles(root, "*.cs").Length > 0)
        {
            warn($"Skipping {root}: no {ExtensionManifest.DefaultEntry} / {ExtensionManifest.FileName} " +
                 "and no *.dll at the root. Point at a source extension's entry file, or at a " +
                 "compiled project's publish output (a .dll).");
        }
        return candidates;
    }

    // host.LoadExtension(path)：把一个**任意**路径变成候选。
    //
    //   目录 —— 先看它自己是不是一个扩展（有入口文件），不是再当成"装着一批扩展的根"来扫
    //   文件 —— .dll 按预编译，.cs 按源码入口（想让工程里的入口文件直接生效就走这条）
    public static List<ExtensionCandidate> FromPath(string path, Action<string> warn)
    {
        var full = Path.GetFullPath(path);
        if (File.Exists(full))
        {
            return FromFile(full, builtIn: false, warn);
        }

        // 目录：ScanRoot 先看它自己是不是扩展，不是再看里面（和扫描约定根同一套规则）。
        return ScanRoot(full, builtIn: false, warn);
    }

    // 一个文件当入口：.dll 走预编译，.cs 走源码；**它所在目录就是扩展目录**（图标、资源
    // 和 ExtensionContext.SourcePath 都认这个）。
    //
    // .cs 也读同目录的 extension.json——不然"指文件"和"指目录"会得到两个不同的 id 和
    // 两套元数据，同一个扩展换个写法就变了样。
    static List<ExtensionCandidate> FromFile(string file, bool builtIn, Action<string> warn)
    {
        var name = Path.GetFileName(file);
        var dir = Path.GetDirectoryName(file) ?? "";
        if (ShouldSkip(name))
        {
            warn($"Skipping {file}: it is part of the plugin host itself");
            return [];
        }
        if (file.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
        {
            return [Precompiled(file, dir, dir, builtIn, manifest: null,
                                fallbackId: Path.GetFileNameWithoutExtension(file))];
        }
        if (file.EndsWith(".cs", StringComparison.OrdinalIgnoreCase))
        {
            return [Source(file, dir, dir, builtIn, ReadManifest(dir, warn),
                           Path.GetFileNameWithoutExtension(file))];
        }
        warn($"Skipping {file}: expected a directory, a .cs entry or a .dll");
        return [];
    }

    static ExtensionCandidate? FromDirectory(string dir, string root, bool builtIn,
                                             Action<string> warn)
    {
        var folderName = Path.GetFileName(dir);
        var manifest = ReadManifest(dir, warn);
        if (EntryFile(dir, manifest) is { } entry)
        {
            return Source(entry, dir, root, builtIn, manifest, folderName);
        }

        var dll = Directory.GetFiles(dir, "*.dll")
                      .FirstOrDefault(f => !ShouldSkip(Path.GetFileName(f)));
        if (dll is not null)
        {
            return Precompiled(dll, dir, root, builtIn, manifest, folderName);
        }

        warn($"Skipping {dir}: no {EntryName(manifest)} and no .dll inside");
        return null;
    }

    static ExtensionManifest? ReadManifest(string dir, Action<string> warn)
    {
        var path = Path.Combine(dir, ExtensionManifest.FileName);
        return File.Exists(path) ? ExtensionManifest.Read(path, warn) : null;
    }

    // 目录里声明/约定的入口文件（清单里写的那个，缺省 main.cs）。
    // 清单是手写的，别让它把入口指到目录外去——目录外的一律当没有。
    static string? EntryFile(string dir, ExtensionManifest? manifest)
    {
        var entry = Path.GetFullPath(Path.Combine(dir, EntryName(manifest)));
        var prefix = Path.GetFullPath(dir) + Path.DirectorySeparatorChar;
        return entry.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && File.Exists(entry)
                   ? entry
                   : null;
    }

    static string EntryName(ExtensionManifest? manifest) =>
        string.IsNullOrWhiteSpace(manifest?.Entry) ? ExtensionManifest.DefaultEntry : manifest!.Entry!;

    // 一个根的 loader id / 显示名。用根目录名（plugins / extensions / 你的工程目录名），
    // 这样官方和用户各一个 loader 时互不覆盖。
    static string LoaderId(string root)
    {
        var folder = Path.GetFileName(Path.TrimEndingDirectorySeparator(root));
        return string.IsNullOrEmpty(folder) ? "loader" : folder + ".loader";
    }

    static string LoaderName(string root)
    {
        var folder = Path.GetFileName(Path.TrimEndingDirectorySeparator(root));
        return string.IsNullOrEmpty(folder) ? "扩展入口" : $"扩展入口（{folder}）";
    }

    static ExtensionCandidate Source(string entry, string directory, string root, bool builtIn,
                                     ExtensionManifest? manifest, string fallbackId,
                                     string? name = null)
    {
        return new ExtensionCandidate
        {
            Id = Pick(manifest?.Id, fallbackId),
            Name = Pick(manifest?.Name, name ?? fallbackId),
            Kind = ExtensionKind.Source,
            Path = Path.GetFullPath(entry),
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
