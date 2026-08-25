namespace Tamias.Api;

public sealed class RibbonPlacement
{
    public string PageId { get; set; } = "home";
    public string GroupId { get; set; } = "plugins";
    public string? IconPath { get; set; }
    public int Order { get; set; }
    public bool Checkable { get; set; }
}
