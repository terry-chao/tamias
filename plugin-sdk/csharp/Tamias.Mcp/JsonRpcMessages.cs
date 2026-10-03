using System.Text;
using System.Text.Json;

namespace Tamias.Mcp;

internal static class JsonRpcMessages
{
    public const int ParseError = -32700;
    public const int ServerError = -32000;

    // 从原始请求里抄回 id（类型要一致：数字还是数字、字符串还是字符串）。
    public static string Error(string request, int code, string message)
    {
        JsonElement? id = null;
        try
        {
            using var document = JsonDocument.Parse(request);
            if (document.RootElement.ValueKind == JsonValueKind.Object &&
                document.RootElement.TryGetProperty("id", out var value))
            {
                id = value.Clone();
            }
        }
        catch (JsonException)
        {
            code = ParseError;
        }

        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            writer.WriteString("jsonrpc", "2.0");
            writer.WritePropertyName("id");
            if (id is { } value)
            {
                value.WriteTo(writer);
            }
            else
            {
                writer.WriteNullValue();
            }
            writer.WritePropertyName("error");
            writer.WriteStartObject();
            writer.WriteNumber("code", code);
            writer.WriteString("message", message);
            writer.WriteEndObject();
            writer.WriteEndObject();
        }
        return Encoding.UTF8.GetString(stream.ToArray());
    }

    // 通知（没有 id）永远不回包——包括失败的时候。
    public static bool IsNotification(string request)
    {
        try
        {
            using var document = JsonDocument.Parse(request);
            return document.RootElement.ValueKind != JsonValueKind.Object ||
                   !document.RootElement.TryGetProperty("id", out _);
        }
        catch (JsonException)
        {
            return false;  // 解析不了就按请求处理，让调用方回一个 parse error
        }
    }
}
