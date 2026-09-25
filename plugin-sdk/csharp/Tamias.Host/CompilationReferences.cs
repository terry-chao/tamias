using Microsoft.CodeAnalysis;

namespace Tamias.Host;

// Roslyn 编译 / 求值共用的一份程序集引用表。
// 单独拎出来是因为它有两个消费者（脚本引擎、源码扩展加载器），
// 而且顺序有讲究：运行时程序集 + 我们自己的 Tamias.Api。
static class CompilationReferences
{
    static List<MetadataReference>? cached_;
    static readonly object lock_ = new();

    public static List<MetadataReference> All()
    {
        lock (lock_)
        {
            if (cached_ is not null)
            {
                return cached_;
            }
            var references = new List<MetadataReference>();
            // 运行时里的程序集全给上：脚本和扩展都该能随手用 List<> / Linq / Task / IO。
            var trusted = (string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES");
            if (!string.IsNullOrEmpty(trusted))
            {
                foreach (var path in trusted.Split(Path.PathSeparator))
                {
                    if (!string.IsNullOrWhiteSpace(path))
                    {
                        references.Add(MetadataReference.CreateFromFile(path));
                    }
                }
            }
            references.Add(MetadataReference.CreateFromFile(typeof(Tamias.Api.IHost).Assembly.Location));
            cached_ = references;
            return cached_;
        }
    }
}
