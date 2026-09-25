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
    // 源码扩展在代码里声明的元数据（`public static PluginMetadata Metadata`），可省。
    public MemberInfo? MetadataMember { get; init; }
    public Assembly? Assembly { get; init; }

    public void Dispose() => Context.Unload();
}

// 把一个扩展候选变成可装载的，再装上去。
//
// 源码扩展的约定：程序集里任意一个类型带 `public static void Load(IHost)`——不用实现
// IPlugin（那需要工程和引用），一个文件就能是个扩展。
//
// 元数据**代码优先**：类型上带 `public static PluginMetadata Metadata` 就用它，
// 缺的字段用同目录的 extension.json 补，再缺就退回目录名。所以源码扩展**不要**自己调
// RegisterPlugin：宿主已经登记过了。这和预编译扩展是同一条规则（那边是 IPlugin.Metadata）。
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
            MetadataMember = FindMetadata(assembly),
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
        var metadata = MetadataFor(host, prepared);
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

    // 源码扩展的元数据：**代码优先，清单补缺，目录名兜底**——和预编译扩展那条
    // Normalize 是同一条规则，只是"代码"从 IPlugin.Metadata 变成静态成员。
    //
    // candidate 里的 Id / Name / ... 已经是"清单 > 目录名"的结果，所以这里只要拿代码
    // 声明的字段去盖它。
    static PluginMetadata MetadataFor(Host host, PreparedExtension prepared)
    {
        var candidate = prepared.Candidate;
        var declared = ReadDeclaredMetadata(host, prepared);
        if (declared is not null && !string.IsNullOrWhiteSpace(declared.Id) &&
            !string.Equals(declared.Id.Trim(), candidate.Id, StringComparison.Ordinal))
        {
            // 扫描期的身份（跨根覆盖、启停持久化、装载表）用的是候选 id，代码里又写了一个——
            // 按代码的走（和预编译一致），但这两种 id 不一样迟早会咬人，说一声。
            host.Log($"Extension '{candidate.Id}': code declares Id '{declared.Id.Trim()}'. " +
                     "Rename the folder (or drop one of them) so they match.");
        }
        var metadata = new PluginMetadata
        {
            Id = ValueOrDefault(declared?.Id, candidate.Id),
            Name = ValueOrDefault(declared?.Name, candidate.Name),
            Author = ValueOrDefault(declared?.Author, candidate.Author),
            // 内置与否由**根**决定，不由扩展自报。
            IsBuiltIn = candidate.BuiltIn,
            Version = ValueOrDefault(declared?.Version, candidate.Version),
            ReleaseDate = ValueOrDefault(declared?.ReleaseDate, candidate.ReleaseDate),
            Description = ValueOrDefault(declared?.Description, candidate.Description),
            HomepageUrl = ValueOrDefault(declared?.HomepageUrl, candidate.Homepage),
            IconPath = string.IsNullOrWhiteSpace(declared?.IconPath)
                           ? candidate.IconPath
                           : Path.GetFullPath(Path.Combine(candidate.Directory, declared!.IconPath!)),
        };
        Validate(host, metadata);
        return metadata;
    }

    // 代码里声明的元数据。取不到（没写、类型不对、getter 抛了）就当没写——
    // 元数据读失败不该让整个扩展装不上，记一条日志继续用清单兜着。
    static PluginMetadata? ReadDeclaredMetadata(Host host, PreparedExtension prepared)
    {
        switch (prepared.MetadataMember)
        {
            case PropertyInfo property:
                try
                {
                    return property.GetValue(null) as PluginMetadata;
                }
                catch (TargetInvocationException ex) when (ex.InnerException is not null)
                {
                    host.Log($"Extension '{prepared.Candidate.Id}': Metadata threw " +
                             $"{ex.InnerException.GetType().Name}: {ex.InnerException.Message}");
                    return null;
                }
                catch (Exception ex)
                {
                    host.Log($"Extension '{prepared.Candidate.Id}': Metadata unreadable: {ex.Message}");
                    return null;
                }
            case FieldInfo field:
                try
                {
                    return field.GetValue(null) as PluginMetadata;
                }
                catch (Exception ex)
                {
                    host.Log($"Extension '{prepared.Candidate.Id}': Metadata unreadable: {ex.Message}");
                    return null;
                }
            default:
                return null;
        }
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
        foreach (var type in SafeTypes(assembly))
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

    // 代码里声明的元数据成员：`public static PluginMetadata Metadata`，属性或字段都行。
    // 只看类型，不调用——真正的取值在 ReadDeclaredMetadata（那边才需要 host 来记日志）。
    static MemberInfo? FindMetadata(Assembly assembly)
    {
        foreach (var type in SafeTypes(assembly))
        {
            var property = type.GetProperty("Metadata", BindingFlags.Public | BindingFlags.Static,
                                            binder: null, typeof(PluginMetadata), Type.EmptyTypes,
                                            modifiers: null);
            if (property?.GetMethod is not null)
            {
                return property;
            }
            var field = type.GetField("Metadata", BindingFlags.Public | BindingFlags.Static);
            if (field is not null && field.FieldType == typeof(PluginMetadata))
            {
                return field;
            }
        }
        return null;
    }

    static Type[] SafeTypes(Assembly assembly)
    {
        try
        {
            return assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException ex)
        {
            return ex.Types.OfType<Type>().ToArray();
        }
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
