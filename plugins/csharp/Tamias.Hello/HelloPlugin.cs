using Tamias.Api;

namespace Tamias.Hello;

public sealed class HelloPlugin : IPlugin
{
    public PluginMetadata Metadata => new()
    {
        Id = "tamias.hello",
        Name = "示例工具",
        Author = "Tamias",
        IsBuiltIn = true,
        Version = "1.0.0",
        ReleaseDate = "2026-08-25",
        Description = "提供选择列表与删除所选对象功能的示例工具。",
        HomepageUrl = "https://github.com/terry-chao/tamias",
        IconPath = "hello.svg",
    };

    public void Load(IHost host)
    {
        host.AddCommand(
            "hello.list_selection",
            "列出选择",
            () =>
            {
                var sel = host.Selection;
                if (sel.Count == 0)
                {
                    host.Log("未选择对象");
                    return;
                }
                var entities = host.Entities.ToDictionary(e => e.Id);
                foreach (var id in sel)
                {
                    if (entities.TryGetValue(id, out var info))
                    {
                        host.Log($"#{info.Id} {info.Kind} {info.Name}");
                    }
                    else
                    {
                        host.Log($"#{id}");
                    }
                }
            },
            "把当前选择写到状态栏");

        host.AddCommand(
            "hello.delete_selected",
            "删除所选",
            () =>
            {
                var ids = host.Selection.ToList();
                if (ids.Count == 0)
                {
                    host.Log("未选择对象");
                    return;
                }
                foreach (var id in ids)
                {
                    host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
                }
            },
            "对当前选择逐个 dispatch delete_entity");
    }
}
