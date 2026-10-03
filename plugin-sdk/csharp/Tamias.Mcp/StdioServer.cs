using System.Text;

namespace Tamias.Mcp;

// MCP 的 stdio 传输：一行一条 JSON-RPC，**stdout 只许走协议**，日志一律 stderr。
// 客户端（Claude Desktop 等）把 stdout 当协议通道，混进去一行日志就整条连接废掉。
internal sealed class StdioServer
{
    private readonly BridgeClient _bridge;
    private readonly TextReader _input;
    private readonly TextWriter _output;
    private readonly TextWriter _log;

    public StdioServer(BridgeClient bridge, TextReader input, TextWriter output, TextWriter log)
    {
        _bridge = bridge;
        _input = input;
        _output = output;
        _log = log;
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        while (await _input.ReadLineAsync(cancellationToken) is { } line)
        {
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            string? response;
            try
            {
                response = await _bridge.PostAsync(line, cancellationToken);
            }
            catch (BridgeUnavailableException exception)
            {
                if (JsonRpcMessages.IsNotification(line))
                {
                    await _log.WriteLineAsync($"[tamias-mcp] {exception.Message}");
                    await _log.FlushAsync(cancellationToken);
                    continue;
                }
                response = JsonRpcMessages.Error(line, JsonRpcMessages.ServerError,
                                                 exception.Message);
            }

            if (string.IsNullOrEmpty(response))
            {
                continue;  // 通知：桥回了 202，没有应答体
            }
            await _output.WriteLineAsync(response);
            await _output.FlushAsync(cancellationToken);
        }
    }

    public static (StdioServer Server, BridgeClient Bridge) CreateDefault()
    {
        var utf8 = new UTF8Encoding(false);
        var input = new StreamReader(Console.OpenStandardInput(), utf8);
        var output = new StreamWriter(Console.OpenStandardOutput(), utf8) { AutoFlush = false };
        var log = new StreamWriter(Console.OpenStandardError(), utf8) { AutoFlush = true };
        var bridge = new BridgeClient();
        return (new StdioServer(bridge, input, output, log), bridge);
    }
}
