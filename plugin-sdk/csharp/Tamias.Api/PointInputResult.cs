namespace Tamias.Api;

public sealed class PointInputResult
{
    public PointInputResult(IReadOnlyList<PickPoint> points, bool cancelled)
    {
        ArgumentNullException.ThrowIfNull(points);
        Points = points;
        Cancelled = cancelled;
    }

    public IReadOnlyList<PickPoint> Points { get; }
    public bool Cancelled { get; }
}
