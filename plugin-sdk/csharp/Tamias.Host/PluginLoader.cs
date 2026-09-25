using System.Security.Cryptography;

namespace Tamias.Host;

// 一个已经装上的扩展：候选信息 + 内容指纹 + 可卸载的加载上下文。
sealed class LoadedExtension
{
    public required ExtensionCandidate Candidate { get; init; }
    // 入口文件（+ 清单）的内容哈希。文件监视只告诉我们"目录里有动静"，
    // **变没变**由它说了算——编辑器写临时文件、我们自己的写入都不该触发重载。
    public required string Fingerprint { get; init; }
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
        if (roots_.Count == 0)
        {
            return "";
        }
        var candidates = ExtensionScanner.Scan(roots_, host.Log, host.Log);
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
            try
            {
                ExtensionLoader.Activate(host, prepared);
                loaded_[candidate.Id] = new LoadedExtension
                {
                    Candidate = candidate,
                    Fingerprint = fingerprint,
                    Prepared = prepared,
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

    static bool TryLoad(Host host, ExtensionCandidate candidate, out string error)
    {
        error = "";
        PreparedExtension? prepared = null;
        try
        {
            prepared = ExtensionLoader.Prepare(candidate);
            ExtensionLoader.Activate(host, prepared);
            loaded_[candidate.Id] = new LoadedExtension
            {
                Candidate = candidate,
                Fingerprint = Fingerprint(candidate),
                Prepared = prepared,
            };
            return true;
        }
        catch (Exception ex)
        {
            prepared?.Dispose();
            // 装到一半失败（比如 Load 抛了）也要把已登记的部分摘掉，别留个点了没反应的按钮。
            // 注意：预编译扩展若自报的 id 与文件名不一致，这里摘不干净——重启即恢复。
            host.RemovePlugin(candidate.Id);
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
        host.RemovePlugin(id);
        entry.Prepared.Dispose();
        // ALC.Unload() 只是"标记可回收"，真正的回收在下次 GC。连续重载时推一把，
        // 不然旧版本会一届一届堆着。
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        host.Log($"Unloaded extension {id}");
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
