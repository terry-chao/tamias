using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;

namespace Tamias.Mcp;

// stdio ↔ 桥。这里是**哑转发**：不认识 MCP 方法，也不认识工具，
// 只把一行 JSON-RPC 丢给应用内的桥，再把应答原样送回。
// 好处是工具语义只有一份（在 C++ 的 McpServer 里），不会两边漂移。
internal sealed class BridgeClient : IDisposable
{
    private static readonly TimeSpan DefaultTimeout = TimeSpan.FromMinutes(2);

    private readonly HttpClient _http;
    private Discovery? _current;

    public BridgeClient()
    {
        var timeout = DefaultTimeout;
        if (Environment.GetEnvironmentVariable("TAMIAS_MCP_TIMEOUT_MS") is { Length: > 0 } raw &&
            int.TryParse(raw, out var milliseconds) && milliseconds > 0)
        {
            timeout = TimeSpan.FromMilliseconds(milliseconds);
        }
        _http = new HttpClient { Timeout = timeout };
    }

    // 返回桥的应答体；空串 = 通知，调用方不该回包。
    public async Task<string> PostAsync(string payload, CancellationToken cancellationToken)
    {
        var discovery = _current ??= await LoadVerifiedAsync(cancellationToken);
        try
        {
            return await SendAsync(discovery, payload, cancellationToken);
        }
        catch (BridgeUnavailableException)
        {
            // 只重试一次：Tamias 重启会换端口和 token，重读发现文件就能自愈。
        }

        _current = await LoadVerifiedAsync(cancellationToken);
        return await SendAsync(_current, payload, cancellationToken);
    }

    public Discovery? Current => _current;

    public void Dispose() => _http.Dispose();

    private async Task<Discovery> LoadVerifiedAsync(CancellationToken cancellationToken)
    {
        var discovery = Discovery.Load();
        using var probe = new HttpRequestMessage(HttpMethod.Get, discovery.HealthUri());
        probe.Headers.TryAddWithoutValidation("Authorization", $"Bearer {discovery.Token}");
        try
        {
            using var response = await _http.SendAsync(probe, cancellationToken);
            if (!response.IsSuccessStatusCode)
            {
                throw new BridgeUnavailableException(
                    $"Tamias bridge at {discovery.Endpoint} rejected the bridge token " +
                    $"({(int)response.StatusCode}). Restart Tamias with '--mcp'.");
            }
        }
        catch (HttpRequestException exception)
        {
            throw new BridgeUnavailableException(
                $"Tamias bridge at {discovery.Endpoint} is not reachable.", exception);
        }
        return discovery;
    }

    private async Task<string> SendAsync(Discovery discovery, string payload,
                                         CancellationToken cancellationToken)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, discovery.Endpoint)
        {
            Content = new StringContent(payload, new UTF8Encoding(false), "application/json"),
        };
        request.Headers.TryAddWithoutValidation("Authorization", $"Bearer {discovery.Token}");
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));

        HttpResponseMessage response;
        try
        {
            response = await _http.SendAsync(request, cancellationToken);
        }
        catch (HttpRequestException exception)
        {
            throw new BridgeUnavailableException("Lost connection to the Tamias bridge.", exception);
        }
        catch (TaskCanceledException exception) when (!cancellationToken.IsCancellationRequested)
        {
            throw new BridgeUnavailableException(
                "Tamias did not answer in time. It may be busy or showing a modal dialog.",
                exception);
        }

        using (response)
        {
            var body = await response.Content.ReadAsStringAsync(cancellationToken);
            if (response.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden)
            {
                throw new BridgeUnavailableException(
                    "Tamias bridge rejected the token; it was probably restarted.");
            }
            if (!response.IsSuccessStatusCode)
            {
                throw new BridgeUnavailableException(
                    $"Tamias bridge returned {(int)response.StatusCode}: {body}");
            }
            return body;
        }
    }
}
