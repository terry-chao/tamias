namespace Tamias.Api;

public interface IPlugin
{
    PluginMetadata Metadata => new();

    void Load(IHost host);
}
