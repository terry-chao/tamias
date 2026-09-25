using System.Security.Cryptography;

namespace Tamias.Host;

// 一个已经装上的扩展：候选信息 + 内容指纹 + 可卸载的加载上下文。
sealed class LoadedExtension
{
    public required ExtensionCandidate Candidate { get; init; }
    // 入口文件（+ 清单）的内容哈希。文件监视只告诉我们"目录里有动静"，
    // **变没变**由它说了算——编辑器写临时文件、我们自己的写入都不该触发重载。
    public required string Fingerprint { get; init; }
    // 这次装载真正登记上去的插件 id（代码自报的，可能和扫描到的 id 不同；一个 dll
    // 也可能登记多个）。摘的时候按它们摘，扫描 id 只是兜底。
    public required IReadOnlyList<string> RegisteredIds { get; init; }
    public required PreparedExtension Prepared { get; init; }
}

// 扩展加载入口：Bootstrap.Initialize 给约定目录，Bootstrap.Reload 让它重扫。
//
// 两个来源是一套东西：
//
//   <exe>/plugins/                        随版本发布的内置扩展（第一个根）
//   <AppData>/tamias/tamias/extensions/   用户自己装的
//
// 每个来源里都可以放源码扩展（main.cs / extension.json）或预编译扩展（.dll）。
static class PluginLoader
{
    static List<string> roots_ = [];
    // host.LoadExtension(path) 登记进来的路径：会话内一直参与重扫（改了就热重载、
    // 删掉就跟着摘掉），所以它们和约定根一样进 roots，只是排在后面（不覆盖内置的）。
    static readonly List<string> extraRoots_ = [];
    static readonly Dictionary<string, LoadedExtension> loaded_ = new(StringComparer.OrdinalIgnoreCase);

    // roots 用换行分隔（路径里不会出现换行，比分号安全）。
    public static void LoadAll(Host host, string? roots)
    {
        roots_ = SplitRoots(roots);
        loaded_.Clear();
        var candidates = ExtensionScanner.Scan(roots_, host.Log, host.Log);
        if (candidates.Count == 0)
        {
            host.Log("No extensions found. Searched: " + string.Join(" | ", roots_));
            return;
        }
        host.Log($"Discovered {candidates.Count} extension(s)");
        foreach (var candidate in candidates)
        {
            TryLoad(host, candidate, out _);
        }
    }

    // 文件监视说"目录里有动静"，由这里判断到底谁真的变了。
    // 返回给用户看的摘要；**没有任何变化时返回空串**（壳据此决定要不要重建 Ribbon）。
    public static string ReloadChanged(Host host)
    {
        if (roots_.Count == 0 && extraRoots_.Count == 0)
        {
            return "";
        }
        var candidates = ExtensionScanner.Scan(ScanRoots(), host.Log, host.Log);
        var present = new Dictionary<string, ExtensionCandidate>(StringComparer.OrdinalIgnoreCase);
        foreach (var candidate in candidates)
        {
            present[candidate.Id] = candidate;
        }

        var removed = new List<string>();
        var added = new List<string>();
        var reloaded = new List<string>();
        var failed = new List<string>();

        // 目录里没了的：摘掉（连同它登记的命令）。
        foreach (var id in loaded_.Keys.ToList())
        {
            if (!present.ContainsKey(id))
            {
                Unload(host, id);
                removed.Add(id);
            }
        }

        foreach (var candidate in candidates)
        {
            if (!loaded_.TryGetValue(candidate.Id, out var existing))
            {
                if (TryLoad(host, candidate, out var error))
                {
                    added.Add(candidate.Id);
                }
                else
                {
                    failed.Add($"{candidate.Id}: {error}");
                }
                continue;
            }

            var fingerprint = Fingerprint(candidate);
            if (fingerprint == existing.Fingerprint)
            {
                continue;  // 只是隔壁文件动了一下，这个扩展本身没改
            }

            // **先编译再摘旧的**：编译不过就留着旧版本继续用。
            // 编辑器保存到一半就是语法错误，那一下最需要旧工具还在。
            PreparedExtension prepared;
            try
            {
                prepared = ExtensionLoader.Prepare(candidate);
            }
            catch (Exception ex)
            {
                failed.Add($"{candidate.Id}: {ex.Message}");
                continue;
            }

            Unload(host, candidate.Id);
            host.RegistrationBatch.Clear();
            try
            {
                ExtensionLoader.Activate(host, prepared);
                loaded_[candidate.Id] = new LoadedExtension
                {
                    Candidate = candidate,
                    Fingerprint = fingerprint,
                    Prepared = prepared,
                    RegisteredIds = [.. host.RegistrationBatch],
                };
                reloaded.Add(candidate.Id);
            }
            catch (Exception ex)
            {
                prepared.Dispose();
                failed.Add($"{candidate.Id}: {ex.Message}");
            }
        }

        var changed = reloaded.Count + added.Count + removed.Count;
        if (changed == 0 && failed.Count == 0)
        {
            return "";
        }
        var lines = new List<string>();
        if (changed > 0)
        {
            var names = reloaded.Concat(added).Concat(removed).ToList();
            lines.Add($"Extensions updated: {reloaded.Count} reloaded, {added.Count} added, " +
                      $"{removed.Count} removed (" + string.Join(", ", names) + ")");
        }
        lines.AddRange(failed.Select(f => "Extension failed: " + f));
        return string.Join("\n", lines);
    }

    // host.LoadExtension(path)：把一个任意路径装进来。
    //
    // 目录可以"自己是一个扩展"，也可以"装着一批扩展"；.dll / .cs 文件按入口处理
    // （判定见 ExtensionScanner.FromPath）。相对路径按调用者所在目录解析：loader.cs 里
    // 写相对路径就是相对它自己那个目录。
    //
    // 路径登记进 extraRoots_，于是它会跟着每轮重扫——**改那个工程里的源码，保存即生效**。
    // 找不到路径只记一条日志，不让整个 loader 挂掉：一处写错不该毁掉其余的。
    public static void LoadFrom(Host host, string path)
    {
        string full;
        try
        {
            full = Path.GetFullPath(path, BaseDirectory());
        }
        catch (Exception ex)
        {
            host.Log($"LoadExtension: bad path '{path}': {ex.Message}");
            return;
        }
        if (!Directory.Exists(full) && !File.Exists(full))
        {
            host.Log($"LoadExtension: path not found, skipped: {full}");
            return;
        }
        if (!extraRoots_.Contains(full, StringComparer.OrdinalIgnoreCase))
        {
            extraRoots_.Add(full);
        }
        foreach (var candidate in ExtensionScanner.FromPath(full, host.Log))
        {
            if (loaded_.ContainsKey(candidate.Id))
            {
                continue;  // 已经装过（重扫也会看到它）——重复调用不该报错
            }
            TryLoad(host, candidate, out _);
        }
    }

    // C++ 侧的文件监视要盯的根：约定根 + LoadExtension 登记进来的（换行分隔）。
    public static string RootsText() => string.Join('\n', ScanRoots());

    // 约定根 + 额外根（去重，约定根在前——同 id 时后扫的覆盖先扫的）。
    static List<string> ScanRoots()
    {
        var all = new List<string>(roots_);
        foreach (var root in extraRoots_)
        {
            if (!all.Contains(root, StringComparer.OrdinalIgnoreCase))
            {
                all.Add(root);
            }
        }
        return all;
    }

    // 相对路径的基准：Load 期间是扩展自己的目录（loader.cs 里写相对路径就是相对它），
    // 其余时候是进程的工作目录。
    static string BaseDirectory()
    {
        var source = Tamias.Api.ExtensionContext.SourcePath;
        return string.IsNullOrWhiteSpace(source) ? Environment.CurrentDirectory : source;
    }

    static bool TryLoad(Host host, ExtensionCandidate candidate, out string error)
    {
        error = "";
        PreparedExtension? prepared = null;
        host.RegistrationBatch.Clear();
        try
        {
            prepared = ExtensionLoader.Prepare(candidate);
            ExtensionLoader.Activate(host, prepared);
            loaded_[candidate.Id] = new LoadedExtension
            {
                Candidate = candidate,
                Fingerprint = Fingerprint(candidate),
                Prepared = prepared,
                RegisteredIds = [.. host.RegistrationBatch],
            };
            return true;
        }
        catch (Exception ex)
        {
            prepared?.Dispose();
            // 装到一半失败（比如 Load 抛了）也要把已登记的部分摘掉，别留个点了没反应的按钮。
            // 按**真正登记过的** id 摘，再加扫描到的 id 兜底——扩展可以在代码里自报另一个
            // id，一个 dll 也可能登记了多个。
            Unregister(host, host.RegistrationBatch, candidate.Id);
            error = ex.Message;
            host.Log($"Failed to load extension '{candidate.Id}' from {candidate.Directory}: " +
                     ex.Message);
            return false;
        }
    }

    static void Unload(Host host, string id)
    {
        if (!loaded_.Remove(id, out var entry))
        {
            return;
        }
        // 顺序要紧：先摘命令（那些委托钉着旧程序集），再卸加载上下文。
        Unregister(host, entry.RegisteredIds, id);
        entry.Prepared.Dispose();
        // ALC.Unload() 只是"标记可回收"，真正的回收在下次 GC。连续重载时推一把，
        // 不然旧版本会一届一届堆着。
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        host.Log($"Unloaded extension {id}");
    }

    // 摘掉一个扩展登记的所有插件 id（+ 扫描 id 兜底）。RemovePlugin 本身是幂等的。
    static void Unregister(Host host, IReadOnlyList<string> registered, string scannedId)
    {
        foreach (var id in registered)
        {
            host.RemovePlugin(id);
        }
        host.RemovePlugin(scannedId);
    }

    static string Fingerprint(ExtensionCandidate candidate)
    {
        using var sha = SHA256.Create();
        using var content = new MemoryStream();
        Append(content, candidate.Path);
        var manifest = Path.Combine(candidate.Directory, ExtensionManifest.FileName);
        if (File.Exists(manifest))
        {
            Append(content, manifest);
        }
        return Convert.ToHexString(sha.ComputeHash(content.ToArray()));
    }

    static void Append(Stream stream, string path)
    {
        using var file = File.OpenRead(path);
        file.CopyTo(stream);
    }

    static List<string> SplitRoots(string? roots)
    {
        var list = new List<string>();
        if (!string.IsNullOrWhiteSpace(roots))
        {
            foreach (var part in roots.Split(
                         '\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                list.Add(Path.GetFullPath(part));
            }
        }
        if (list.Count == 0)
        {
            // 兜底：宿主自己从 managed/ 往上猜（老行为）。正常路径由 C++ 侧显式给根目录。
            var baseDir = AppContext.BaseDirectory;
            list.Add(Path.GetFullPath(Path.Combine(baseDir, "..", "plugins")));
            list.Add(Path.GetFullPath(Path.Combine(baseDir, "plugins")));
        }
        return list;
    }
}
