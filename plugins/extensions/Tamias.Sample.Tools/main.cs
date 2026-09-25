using System.Linq;
using Tamias.Api;

// 目录式扩展的入口约定：程序集里任意一个类型带
// `public static void Load(IHost host)` 就会被调用。
// 不用实现 IPlugin，也不用工程文件——一个 main.cs 就能是个扩展。
//
// 元数据（id / 名称 / 版本 / 作者 / 图标）来自同目录的 extension.json，
// 所以这里**不要**再调 host.RegisterPlugin：清单已经登记过了。
public static class Entry
{
    // Load 期间才拿得到自己的目录（见 ExtensionContext 的说明）——要用就先存下来。
    static string source_ = "";

    public static void Load(IHost host)
    {
        source_ = ExtensionContext.SourcePath;
        var placement = new RibbonPlacement { PageId = "home", GroupId = "plugins", Order = 90 };
        host.AddCommand("sample.features", "列出特征", () => ListFeatures(host),
                        "把选中实体的特征树与参数写到控制台", placement);
        host.AddCommand("sample.set_depth", "改拉伸深度", () => SetDepth(host),
                        "把所有拉伸特征的深度改成给定值（整批一步撤销）", placement);
        host.AddCommand("sample.where", "我从哪来", () => WhereAmI(host),
                        "报告这个扩展是从哪个目录加载的", placement);
    }

    // v6 宽读：特征树 + 参数。
    static void ListFeatures(IHost host)
    {
        if (host.Selection.Count == 0)
        {
            host.Log("未选择对象");
            return;
        }
        foreach (var id in host.Selection)
        {
            foreach (var feature in host.Features(id))
            {
                var args = string.Join(", ", feature.Params.Select(p => $"{p.Name}={p.Value:0.###}"));
                var deps = feature.Inputs.Count == 0
                    ? ""
                    : $" <- [{string.Join(", ", feature.Inputs)}]";
                host.Log($"#{id} #{feature.Id} {feature.Kind}({args}){deps}");
            }
        }
    }

    // v7 事务：改 N 个参数只占一步撤销。
    static void SetDepth(IHost host)
    {
        var ids = host.Selection.ToList();
        if (ids.Count == 0)
        {
            host.Log("未选择对象");
            return;
        }
        if (host.Ui.PromptNumber("改拉伸深度", "深度 (m)", 3.0, 0.01, 50.0) is not double depth)
        {
            return;
        }

        var changed = 0;
        using var tx = host.BeginTransaction("改拉伸深度");
        foreach (var id in ids)
        {
            foreach (var feature in host.Features(id).Where(f => f.Kind == FeatureKind.Extrude))
            {
                if (!feature.Params.Any(p => p.Name == "depth"))
                {
                    continue;
                }
                host.Dispatch("set_param", new CommandArgs()
                    .SetInt("entity_id", (long)id)
                    .SetInt("feature_id", (long)feature.Id)
                    .SetString("param_name", "depth")
                    .SetDouble("value", depth));
                ++changed;
            }
        }
        tx.Commit();
        host.Log(changed == 0
                     ? "没找到带 depth 的拉伸特征"
                     : $"已改 {changed} 个拉伸深度（一步撤销）");
    }

    static void WhereAmI(IHost host)
    {
        host.Log($"扩展目录：{source_}");
        host.Log($"文档：{host.DocumentName}，实体 {host.Entities.Count}，选中 {host.Selection.Count}");
    }
}
