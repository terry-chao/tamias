namespace Tamias.Mcp;

// tamias-mcp：MCP 客户端（Claude Desktop / Cursor / …）拉起的 stdio 进程，
// 把消息转发给正在运行的 Tamias 里的桥。自己不含任何工具语义。
internal static class Program
{
    private static async Task<int> Main()
    {
        var (server, bridge) = StdioServer.CreateDefault();
        using var shutdown = new CancellationTokenSource();
        Console.CancelKeyPress += (_, eventArgs) =>
        {
            eventArgs.Cancel = true;
            shutdown.Cancel();
        };

        try
        {
            await server.RunAsync(shutdown.Token);
        }
        catch (OperationCanceledException)
        {
            // 客户端收工，正常退出。
        }
        finally
        {
            bridge.Dispose();
        }
        return 0;
    }
}
