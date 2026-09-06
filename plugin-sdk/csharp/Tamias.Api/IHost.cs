namespace Tamias.Api;

public interface IHost
{
    string DocumentName { get; }
    IReadOnlyList<EntityInfo> Entities { get; }
    IReadOnlyList<ulong> Selection { get; }
    IUi Ui { get; }
    void Log(string message);
    void Dispatch(string command, CommandArgs? args = null);
    void AddCommand(
        string id,
        string title,
        Action action,
        string? tooltip = null,
        RibbonPlacement? placement = null);
    void SetSelection(IEnumerable<ulong> ids);
    void ClearSelection();
    ulong BeginPointInput(PointInputOptions options, Action<PointInputResult> callback);
    ulong BeginEntityInput(EntityInputOptions options, Action<EntityInputResult> callback);
    void CancelPointInput(ulong requestId);
}
