namespace Tamias.Mcp;

// 「连不上 Tamias」和「Tamias 回了业务错误」要分开：前者对模型是一句可操作的话，
// 后者原样透传桥的应答。只有这个异常会被转成 JSON-RPC error。
internal sealed class BridgeUnavailableException : Exception
{
    public BridgeUnavailableException(string message) : base(message) { }

    public BridgeUnavailableException(string message, Exception inner) : base(message, inner) { }
}
