using System.Diagnostics;
using System.Text.Json;

namespace Tamias.Mcp;

// 发现文件由 Tamias 在启动桥时写：%APPDATA%/tamias/tamias/mcp.json
// （和 QStandardPaths::AppDataLocation 对齐：<Org>/<App> = tamias/tamias）。
internal sealed record Discovery(string Endpoint, string Token, long Pid, string Document)
{
    public static string FilePath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "tamias", "tamias", "mcp.json");

    public static Discovery Load()
    {
        if (!File.Exists(FilePath))
        {
            throw new BridgeUnavailableException(
                $"Tamias MCP bridge not found. Start Tamias with '--mcp' " +
                $"(expected bridge address in {FilePath}).");
        }

        JsonElement root;
        try
        {
            using var json = JsonDocument.Parse(File.ReadAllBytes(FilePath));
            root = json.RootElement.Clone();
        }
        catch (JsonException exception)
        {
            throw new BridgeUnavailableException(
                $"Bridge discovery file {FilePath} is not valid JSON.", exception);
        }

        if (!root.TryGetProperty("endpoint", out var endpoint) ||
            !root.TryGetProperty("token", out var token) ||
            !root.TryGetProperty("pid", out var pid))
        {
            throw new BridgeUnavailableException(
                $"Bridge discovery file {FilePath} is missing endpoint/token/pid.");
        }

        var processId = pid.GetInt64();
        if (!IsProcessAlive(processId))
        {
            // 崩溃或强杀会留下这份文件；这里挡掉「连到一个不存在的端口」那种超时。
            throw new BridgeUnavailableException(
                $"Tamias (pid {processId}) is no longer running. Start Tamias with '--mcp' first.");
        }

        return new Discovery(
            endpoint.GetString() ?? string.Empty,
            token.GetString() ?? string.Empty,
            processId,
            root.TryGetProperty("document", out var document) ? document.GetString() ?? string.Empty
                                                              : string.Empty);
    }

    public Uri HealthUri()
    {
        var builder = new UriBuilder(Endpoint) { Path = "/health" };
        return builder.Uri;
    }

    private static bool IsProcessAlive(long pid)
    {
        try
        {
            using var process = Process.GetProcessById((int)pid);
            return !process.HasExited;
        }
        catch (ArgumentException)
        {
            return false;
        }
    }
}
