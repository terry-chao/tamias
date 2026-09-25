using System.Globalization;
using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Tamias.Api;

namespace Tamias.Host;

// 预编译扩展（.dll）的加载上下文：把 Tamias.Api 解析到宿主那一份，
// 其余依赖交给 AssemblyDependencyResolver 按 dll 旁边的 .deps.json 找。
//
// isCollectible：重载要能把它连同里面那份程序集一起卸掉。
sealed class PluginLoadContext : AssemblyLoadContext
{
    readonly AssemblyDependencyResolver resolver_;

    public PluginLoadContext(string pluginPath) : base("tamias.extension", isCollectible: true)
    {
        resolver_ = new AssemblyDependencyResolver(pluginPath);
    }

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        if (assemblyName.Name == "Tamias.Api")
        {
            return typeof(IPlugin).Assembly;
        }
        var path = resolver_.ResolveAssemblyToPath(assemblyName);
        return path == null ? null : LoadFromAssemblyPath(path);
    }
}

// 源码扩展的加载上下文：**只**管把 Tamias.Api 解析到宿主那一份，别的交给默认上下文。
//
// 不这么做的话，默认上下文会从 managed/ 再加载一份 Tamias.Api，于是
// Entry.Load 收到的 IHost 与宿主的 IHost 是两个不同类型——
// 和 Roslyn globals 那个「cannot be cast to」是同一个坑。
sealed class SourceLoadContext : AssemblyLoadContext
{
    public SourceLoadContext() : base("tamias.source-extension", isCollectible: true) { }

    protected override Assembly? Load(AssemblyName assemblyName) =>
        assemblyName.Name == "Tamias.Api" ? typeof(IHost).Assembly : null;
}

// 已经「准备好、但还没装上去」的一个扩展。
// 拆成准备 / 装载两段是为了**编译失败不毁掉旧版本**——编辑器保存到一半就是语法错误，
// 那一下最需要旧的还在跑。
sealed class PreparedExtension : IDisposable
{
    public required ExtensionCandidate Candidate { get; init; }
    public required AssemblyLoadContext Context { get; init; }
    // 源码扩展的入口；预编译扩展为 null（它的入口是程序集里的 IPlugin）。
    public MethodInfo? Entry { get; init; }
    public Assembly? Assembly { get; init; }

    public void Dispose() => Context.Unload();
}

// 把一个扩展候选变成可装载的，再装上去。
//
// 源码扩展的约定：程序集里任意一个类型带 `public static void Load(IHost)`——不用实现
// IPlugin（那需要工程和引用），一个文件就能是个扩展。元数据来自 extension.json，
// 所以源码扩展**不要**自己调 RegisterPlugin：清单已经登记过了。
static class ExtensionLoader
{
    public static PreparedExtension Prepare(ExtensionCandidate candidate) =>
        candidate.Kind == ExtensionKind.Source ? PrepareSource(candidate) : PreparePrecompiled(candidate);

    public static void Activate(Host host, PreparedExtension prepared)
    {
        if (prepared.Candidate.Kind == ExtensionKind.Source)
        {
            ActivateSource(host, prepared);
        }
        else
        {
            ActivatePrecompiled(host, prepared);
        }
    }

    static PreparedExtension PrepareSource(ExtensionCandidate candidate)
    {
        var assembly = Compile(candidate);
        var entry = FindEntry(assembly);
        if (entry is null)
        {
            throw new InvalidOperationException(
                $"no public static void Load(IHost) found in {candidate.Path}");
        }
        return new PreparedExtension
        {
            Candidate = candidate,
            Context = CurrentContext(assembly),
            Assembly = assembly,
            Entry = entry,
        };
    }

    static PreparedExtension PreparePrecompiled(ExtensionCandidate candidate)
    {
        var context = new PluginLoadContext(candidate.Path);
        try
        {
            var assembly = context.LoadFromAssemblyPath(candidate.Path);
            return new PreparedExtension
            {
                Candidate = candidate,
                Context = context,
                Assembly = assembly,
            };
        }
        catch
        {
            context.Unload();
            throw;
        }
    }

    static void ActivateSource(Host host, PreparedExtension prepared)
    {
        var candidate = prepared.Candidate;
        var metadata = MetadataFor(prepared);
        Validate(host, metadata);
        host.RegisterPlugin(metadata);

        using (Tamias.Api.ExtensionContext.Enter(candidate.Directory))
        {
            try
            {
                prepared.Entry!.Invoke(null, [host]);
            }
            catch (TargetInvocationException ex) when (ex.InnerException is not null)
            {
                throw ex.InnerException;  // 报错要指向扩展自己那行，而不是反射那层壳
            }
        }
        host.Log($"Loaded extension {metadata.Id} ({metadata.Version}) from {candidate.Directory}");
    }

    static void ActivatePrecompiled(Host host, PreparedExtension prepared)
    {
        var candidate = prepared.Candidate;
        var loaded = 0;
        using (Tamias.Api.ExtensionContext.Enter(candidate.Directory))
        {
            foreach (var type in prepared.Assembly!.GetExportedTypes())
            {
                if (type.IsAbstract || !typeof(IPlugin).IsAssignableFrom(type))
                {
                    continue;
                }
                if (Activator.CreateInstance(type) is not IPlugin plugin)
                {
                    continue;
                }
                var metadata = Normalize(host, plugin.Metadata, type, prepared.Assembly!, candidate);
                host.RegisterPlugin(metadata);
                plugin.Load(host);
                host.Log($"Loaded plugin {metadata.Id} from {candidate.Directory}");
                ++loaded;
            }
        }
        if (loaded == 0)
        {
            host.Log($"No IPlugin found in {candidate.Path}");
        }
    }

    static PluginMetadata MetadataFor(PreparedExtension prepared)
    {
        var candidate = prepared.Candidate;
        return new PluginMetadata
        {
            Id = candidate.Id,
            Name = candidate.Name,
            Author = candidate.Author,
            IsBuiltIn = candidate.BuiltIn,
            Version = candidate.Version,
            ReleaseDate = candidate.ReleaseDate,
            Description = candidate.Description,
            HomepageUrl = candidate.Homepage,
            IconPath = candidate.IconPath,
        };
    }

    static Assembly Compile(ExtensionCandidate candidate)
    {
        var tree = CSharpSyntaxTree.ParseText(
            File.ReadAllText(candidate.Path),
            new CSharpParseOptions(LanguageVersion.Latest),
            path: candidate.Path);
        var compilation = CSharpCompilation.Create(
            "tamias.extension." + Sanitize(candidate.Id),
            [tree],
            CompilationReferences.All(),
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                                         optimizationLevel: OptimizationLevel.Release));
        using var stream = new MemoryStream();
        var result = compilation.Emit(stream);
        if (!result.Success)
        {
            throw new InvalidOperationException(Describe(result.Diagnostics));
        }
        stream.Position = 0;
        return new SourceLoadContext().LoadFromStream(stream);
    }

    static MethodInfo? FindEntry(Assembly assembly)
    {
        Type[] types;
        try
        {
            types = assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException ex)
        {
            types = ex.Types.OfType<Type>().ToArray();
        }
        foreach (var type in types)
        {
            var method = type.GetMethod("Load", BindingFlags.Public | BindingFlags.Static,
                                        binder: null, [typeof(IHost)], modifiers: null);
            if (method is not null && method.ReturnType == typeof(void))
            {
                return method;
            }
        }
        return null;
    }

    // dll 扩展可以继续自己声明元数据；缺失的字段用清单 / 程序集信息补。
    static PluginMetadata Normalize(Host host, PluginMetadata? source, Type type,
                                    Assembly assembly, ExtensionCandidate candidate)
    {
        source ??= new PluginMetadata();
        var manifest = candidate.Manifest;
        var metadata = new PluginMetadata
        {
            Id = ValueOrDefault(source.Id, ValueOrDefault(manifest?.Id, type.FullName ?? type.Name)),
            Name = ValueOrDefault(source.Name, ValueOrDefault(manifest?.Name, type.Name)),
            Author = ValueOrDefault(source.Author, manifest?.Author ?? ""),
            // 内置与否由**根**决定，不由扩展自报：用户在用户目录放的东西不该自称内置。
            IsBuiltIn = candidate.BuiltIn,
            Version = ValueOrDefault(
                source.Version,
                ValueOrDefault(manifest?.Version, assembly.GetName().Version?.ToString() ?? "")),
            ReleaseDate = ValueOrDefault(source.ReleaseDate, manifest?.ReleaseDate ?? ""),
            Description = ValueOrDefault(source.Description, manifest?.Description ?? ""),
            HomepageUrl = ValueOrDefault(source.HomepageUrl, manifest?.Homepage ?? ""),
            IconPath = string.IsNullOrWhiteSpace(source.IconPath) ? candidate.IconPath : source.IconPath,
        };
        if (!string.IsNullOrEmpty(source.IconPath) && !Path.IsPathRooted(source.IconPath))
        {
            metadata.IconPath = Path.GetFullPath(Path.Combine(candidate.Directory, source.IconPath));
        }
        Validate(host, metadata);
        return metadata;
    }

    static void Validate(Host host, PluginMetadata metadata)
    {
        if (!string.IsNullOrEmpty(metadata.ReleaseDate) &&
            !DateOnly.TryParseExact(metadata.ReleaseDate, "yyyy-MM-dd", CultureInfo.InvariantCulture,
                                    DateTimeStyles.None, out _))
        {
            host.Log($"Extension '{metadata.Id}' has invalid ReleaseDate " +
                     $"'{metadata.ReleaseDate}'; expected yyyy-MM-dd");
            metadata.ReleaseDate = "";
        }
        if (!string.IsNullOrEmpty(metadata.HomepageUrl) &&
            (!Uri.TryCreate(metadata.HomepageUrl, UriKind.Absolute, out var homepage) ||
             (homepage.Scheme != Uri.UriSchemeHttp && homepage.Scheme != Uri.UriSchemeHttps)))
        {
            host.Log($"Extension '{metadata.Id}' has invalid Homepage " +
                     $"'{metadata.HomepageUrl}'; expected an absolute HTTP(S) URL");
            metadata.HomepageUrl = "";
        }
    }

    static string Describe(IEnumerable<Diagnostic> diagnostics)
    {
        var errors = diagnostics.Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
        return errors.Count == 0 ? "compilation failed" : string.Join("\n", errors.Select(d => d.ToString()));
    }

    static string Sanitize(string id)
    {
        var chars = id.Select(c => char.IsLetterOrDigit(c) ? c : '_').ToArray();
        return new string(chars);
    }

    static string ValueOrDefault(string? value, string fallback) =>
        string.IsNullOrWhiteSpace(value) ? fallback : value;

    // Compile() 里新建的上下文没留在手上，只留了程序集——从程序集反查回去，卸载时要用。
    static AssemblyLoadContext CurrentContext(Assembly assembly) =>
        AssemblyLoadContext.GetLoadContext(assembly)
        ?? throw new InvalidOperationException("extension assembly has no load context");
}
