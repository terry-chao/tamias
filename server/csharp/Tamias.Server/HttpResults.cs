namespace Tamias.Server;

/// <summary>
/// 原生错误码 → HTTP 的映射。**只有这一处**：端点里不写状态码，不然同一类失败在不同
/// 端点上会给出不同的码，客户端没法规避。
/// </summary>
internal static class HttpResults
{
    /// <summary>跑一段可能抛 <see cref="ServerException"/> 的代码，把异常翻成结果。</summary>
    public static IResult Run(Func<IResult> action)
    {
        try
        {
            return action();
        }
        catch (ServerException e)
        {
            return Error(e);
        }
    }

    /// <summary>调用方用错了（缺参数、空命令表）。</summary>
    public static IResult Misuse(string message)
        => Results.Json(new ErrorResponse(message, NativeServer.BadArg),
                        statusCode: StatusCodes.Status400BadRequest);

    /// <summary>会话不存在（或已销毁）。</summary>
    public static IResult Missing(Guid id)
        => Results.Json(new ErrorResponse($"session {id} not found", NativeServer.BadHandle),
                        statusCode: StatusCodes.Status404NotFound);

    private static IResult Error(ServerException e) => e.Code switch
    {
        NativeServer.BadHandle => Results.Json(new ErrorResponse(e.Message, e.Code),
                                               statusCode: StatusCodes.Status404NotFound),
        NativeServer.BadArg => Results.Json(new ErrorResponse(e.Message, e.Code),
                                            statusCode: StatusCodes.Status400BadRequest),
        // 不认识的文档类型 = 不支持的媒体类型，不是"服务端坏了"。
        NativeServer.Unsupported => Results.Json(new ErrorResponse(e.Message, e.Code),
                                                 statusCode: StatusCodes.Status415UnsupportedMediaType),
        // 其余（命令不存在、内核没注册、几何失败）都是"这个作业做不了"：409 而不是 500，
        // 因为重试同一个请求还会失败——客户端该改请求，不该重试。
        _ => Results.Json(new ErrorResponse(e.Message, e.Code),
                          statusCode: StatusCodes.Status409Conflict),
    };
}
