using Tamias.Api;

namespace Tamias.Host;

// 控制台脚本里的全局对象：脚本直接写 `host.Dispatch(...)`。
//
// **必须是 public**：Roslyn 的每一次求值都编译成一个独立的提交程序集，它要能看见
// 这个类型和字段。放在 internal 类里（哪怕嵌套类型声明成 public）都会被 CS0122 挡住。
public sealed class ScriptGlobals
{
    public IHost host = null!;
}
