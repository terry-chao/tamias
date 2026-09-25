namespace Tamias.Api;

// 正在加载的扩展目录（预编译扩展给出 DLL 所在目录）。
//
// `Load(IHost)` 的参数里没有路径，但扩展常常要读自己的资源（图标、模板、示例文件）——
// 由宿主告诉它，而不是让扩展去猜 AppContext.BaseDirectory。
//
// **只在 Load 期间有效**：Load 返回后作用域就还回去了（那时可能正在装下一个扩展）。
// 命令回调里要用，就在 Load 里先存进自己的字段。
public static class ExtensionContext
{
    static string source_ = "";

    public static string SourcePath => source_;

    // **宿主专用**：加载某个扩展前进入它的目录作用域，装完还原。
    public static IDisposable Enter(string sourcePath)
    {
        var previous = source_;
        source_ = sourcePath;
        return new Scope(previous);
    }

    sealed class Scope(string previous) : IDisposable
    {
        public void Dispose() => source_ = previous;
    }
}
