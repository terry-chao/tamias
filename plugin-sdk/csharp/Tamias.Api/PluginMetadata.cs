namespace Tamias.Api;

public sealed class PluginMetadata
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Author { get; set; } = "";
    public bool IsBuiltIn { get; set; }
    public string Version { get; set; } = "";
    public string ReleaseDate { get; set; } = "";
    public string Description { get; set; } = "";
    public string HomepageUrl { get; set; } = "";
    public string IconPath { get; set; } = "";
}
