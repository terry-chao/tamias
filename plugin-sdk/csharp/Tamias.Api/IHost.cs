namespace Tamias.Api;

public interface IHost
{
    string DocumentName { get; }
    IReadOnlyList<EntityInfo> Entities { get; }
    // 某个实体的特征树（只读）。实体不存在时返回空表，不抛异常。
    IReadOnlyList<FeatureInfo> Features(ulong entityId);
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
    // 批量编辑：using 包起来，显式 Commit 才留下一条撤销记录。
    ITransaction BeginTransaction(string? name = null);
    ulong BeginPointInput(PointInputOptions options, Action<PointInputResult> callback);
    ulong BeginEntityInput(EntityInputOptions options, Action<EntityInputResult> callback);
    void CancelPointInput(ulong requestId);
}
