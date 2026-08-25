namespace Tamias.Api;

public interface IHost
{
    string DocumentName { get; }
    IReadOnlyList<EntityInfo> Entities { get; }
    IReadOnlyList<ulong> Selection { get; }
    void Log(string message);
    void Dispatch(string command, CommandArgs? args = null);
    void AddCommand(string id, string title, Action action, string? tooltip = null);
}
