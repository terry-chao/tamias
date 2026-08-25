namespace Tamias.Api;

public sealed class RibbonPlacement
{
    public string PageId { get; set; } = "plugins";
    public string GroupId { get; set; } = "commands";
    public string? IconPath { get; set; }
    public int Order { get; set; }
    public bool Checkable { get; set; }
}
