namespace Tamias.Api;

public sealed class PointInputOptions
{
    public int MinPoints { get; set; } = 1;
    public int MaxPoints { get; set; } = 1;
    public bool AllowConfirm { get; set; }
    public bool GridSnap { get; set; }
    public bool PickEntities { get; set; }
    public bool EntitiesOnly { get; set; }
    public EntityKind? FilterKind { get; set; }
    public float WorkPlaneY { get; set; }
    public PointInputPreviewKind PreviewKind { get; set; }
    public string? PreviewCurveKind { get; set; }

    internal int Flags =>
        (AllowConfirm ? 1 << 0 : 0) |
        (GridSnap ? 1 << 1 : 0) |
        (PickEntities || EntitiesOnly ? 1 << 2 : 0) |
        (EntitiesOnly ? 1 << 3 : 0);
}
