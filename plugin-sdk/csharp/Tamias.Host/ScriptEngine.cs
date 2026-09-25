using System.Collections;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Scripting;
using Microsoft.CodeAnalysis.Scripting;
using Microsoft.CodeAnalysis.Scripting.Hosting;
using Tamias.Api;

namespace Tamias.Host;

// 命令控制台的求值器：把用户敲的一段 C# 当脚本跑。
//
// 三件事先说清楚：
// 1) **全信任，不是沙箱。** 脚本在 Tamias 进程里跑，能拿到 `IHost` 能做的一切——
//    和 FreeCAD 的 Python 控制台一个性质：给操作者自己用的工具，不是安全边界。
// 2) **每次求值 = 一个事务。** 改错了按一次 Ctrl+Z 全部退回；这是敢让人随手敲代码的前提。
//    所以脚本里**不要**自己再开事务（不支持嵌套）。
// 3) 输出靠 `host.Log(...)`（落到控制台面板），最后那个表达式的值由返回值带回。
static class ScriptEngine
{
    static ScriptOptions? options_;
    static InteractiveAssemblyLoader? loader_;
    static readonly object optionsLock_ = new();

    public static string Evaluate(IHost host, string code)
    {
        using var tx = host.BeginTransaction("控制台");
        var (options, loader) = Setup();
        object? value;
        try
        {
            // 最后那个表达式的值就是返回值（REPL 语义）。
            value = CSharpScript
                .Create<object?>(code, options, typeof(ScriptGlobals), loader)
                .RunAsync(new ScriptGlobals { host = host })
                .GetAwaiter()
                .GetResult()
                .ReturnValue;
        }
        catch
        {
            tx.Abort();  // 异常路径不留半截改动
            throw;
        }
        tx.Commit();
        return Describe(value);
    }

    public static string DescribeException(Exception ex) =>
        string.IsNullOrWhiteSpace(ex.Message) ? ex.GetType().Name : ex.Message;

    static (ScriptOptions Options, InteractiveAssemblyLoader Loader) Setup()
    {
        lock (optionsLock_)
        {
            if (options_ is not null)
            {
                return (options_, loader_!);
            }
            var references = CompilationReferences.All();

            // 关键：把宿主**已经在用**的那两份程序集登记给脚本加载器。
            // 不登记的话 Roslyn 会在自己的加载上下文里再加载一份 Tamias.Host / Tamias.Api，
            // 于是 globals 的运行时类型对不上，脚本一跑就报
            // "Tamias.Host.ScriptGlobals cannot be cast to Tamias.Host.ScriptGlobals"。
            // （Tamias.Host 是由 hostfxr 装进独立 ALC 的，所以这两份是"外来"程序集。）
            loader_ = new InteractiveAssemblyLoader();
            loader_.RegisterDependency(typeof(ScriptGlobals).Assembly);
            loader_.RegisterDependency(typeof(IHost).Assembly);

            options_ = ScriptOptions.Default
                .WithReferences(references)
                .WithImports("System", "System.Collections.Generic", "System.Linq", "Tamias.Api");
            return (options_, loader_);
        }
    }

    // REPL 手感：`host.Entities` 这类表达式应该打印成一行，而不是类型名。
    static string Describe(object? value)
    {
        switch (value)
        {
            case null:
                return "";
            case string text:
                return text;
            case IEnumerable items:
            {
                var parts = new List<string>();
                foreach (var item in items)
                {
                    if (parts.Count == 20)
                    {
                        parts.Add("…");
                        break;
                    }
                    parts.Add(item?.ToString() ?? "null");
                }
                return string.Join(", ", parts);
            }
            default:
                return value.ToString() ?? "";
        }
    }
}
