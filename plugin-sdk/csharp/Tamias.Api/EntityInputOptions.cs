namespace Tamias.Api;

public sealed class EntityInputOptions
{
    public int MinCount { get; set; } = 1;
    public int MaxCount { get; set; } = 1;
    public bool AllowConfirm { get; set; }
    public EntityKind? FilterKind { get; set; }
}
