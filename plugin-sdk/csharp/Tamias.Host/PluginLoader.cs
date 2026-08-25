using System.Reflection;
using System.Runtime.Loader;
using Tamias.Api;

namespace Tamias.Host;

sealed class PluginLoadContext : AssemblyLoadContext
{
    readonly AssemblyDependencyResolver resolver_;

    public PluginLoadContext(string pluginPath) : base(isCollectible: false)
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

static class PluginLoader
{
    public static void LoadAll(Host host, string? pluginsDir)
    {
        var found = ResolvePluginsDirectory(pluginsDir);
        if (found == null)
        {
            host.Log("Plugin directory not found (exe/plugins). BaseDirectory=" + AppContext.BaseDirectory);
            return;
        }
        host.Log("Loading plugins from " + found);

        foreach (var dll in Directory.GetFiles(found, "*.dll"))
        {
            var fileName = Path.GetFileName(dll);
            if (fileName.StartsWith("Tamias.Api", StringComparison.OrdinalIgnoreCase) ||
                fileName.StartsWith("Tamias.Host", StringComparison.OrdinalIgnoreCase) ||
                fileName.StartsWith("System.", StringComparison.OrdinalIgnoreCase) ||
                fileName.StartsWith("Microsoft.", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            try
            {
                LoadOne(host, dll);
            }
            catch (Exception ex)
            {
                host.Log("Failed to load plugin " + fileName + ": " + ex.Message);
            }
        }
    }

    static void LoadOne(Host host, string dll)
    {
        var alc = new PluginLoadContext(dll);
        var assembly = alc.LoadFromAssemblyPath(Path.GetFullPath(dll));
        foreach (var type in assembly.GetExportedTypes())
        {
            if (type.IsAbstract || !typeof(IPlugin).IsAssignableFrom(type))
            {
                continue;
            }
            if (Activator.CreateInstance(type) is not IPlugin plugin)
            {
                continue;
            }
            var pluginId = type.FullName ?? type.Name;
            host.RegisterPlugin(pluginId, type.Name);
            plugin.Load(host);
            host.Log("Loaded plugin " + pluginId);
        }
    }

    static string? ResolvePluginsDirectory(string? pluginsDir)
    {
        var candidates = new List<string>();
        if (!string.IsNullOrWhiteSpace(pluginsDir))
        {
            candidates.Add(Path.GetFullPath(pluginsDir));
        }
        var baseDir = AppContext.BaseDirectory;
        candidates.Add(Path.GetFullPath(Path.Combine(baseDir, "..", "plugins")));
        candidates.Add(Path.GetFullPath(Path.Combine(baseDir, "plugins")));

        foreach (var path in candidates.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (Directory.Exists(path))
            {
                return path;
            }
        }
        return null;
    }
}
