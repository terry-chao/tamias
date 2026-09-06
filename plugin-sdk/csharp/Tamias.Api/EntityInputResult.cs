namespace Tamias.Api;

public sealed class EntityInputResult
{
    public EntityInputResult(IReadOnlyList<PickPoint> hits, bool cancelled)
    {
        Hits = hits;
        Cancelled = cancelled;
        EntityIds = hits
            .Where(hit => hit.EntityId != 0)
            .Select(hit => hit.EntityId)
            .Distinct()
            .ToList();
    }

    public IReadOnlyList<PickPoint> Hits { get; }
    public IReadOnlyList<ulong> EntityIds { get; }
    public bool Cancelled { get; }
}
