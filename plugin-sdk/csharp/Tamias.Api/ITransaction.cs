namespace Tamias.Api;

// 一次编辑事务：BeginTransaction 与 Commit 之间 dispatch 的命令合计**一条**撤销记录。
//
// 语义是「显式提交」：只有 Commit() 才留下撤销记录；没提交就 Dispose（包括异常从
// using 里逃出去）一律回滚。宁可什么都不做，也不要留下半截改动。
//
// 不能嵌套；事务里不能 dispatch 交互式命令（点齐的时刻由鼠标决定，不在事务窗口里）。
public interface ITransaction : IDisposable
{
    string Name { get; }
    void Commit();
    void Abort();
}
