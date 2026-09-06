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
        Version = "1.1.0",
        ReleaseDate = "2026-09-06",
        Description = "演示选择、对话框和脚本化建墙。",
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

        host.AddCommand(
            "hello.about",
            "关于示例",
            () =>
            {
                host.Ui.ShowMessage(
                    "示例工具",
                    $"文档 {host.DocumentName}\n实体 {host.Entities.Count}\n选中 {host.Selection.Count}");
            },
            "弹出当前文档摘要");

        host.AddCommand(
            "hello.create_wall",
            "创建墙",
            () => BeginCreateWall(host),
            "对话框填尺寸，视口点两点后创建墙");

        host.AddCommand(
            "hello.pick_entities",
            "拾取对象",
            () =>
            {
                host.BeginEntityInput(
                    new EntityInputOptions
                    {
                        MinCount = 1,
                        MaxCount = 0,
                        AllowConfirm = true,
                    },
                    result =>
                    {
                        if (result.Cancelled || result.EntityIds.Count == 0)
                        {
                            return;
                        }
                        host.SetSelection(result.EntityIds);
                        host.Ui.ShowMessage("拾取对象", $"已选中 {result.EntityIds.Count} 个对象");
                    });
            },
            "在视口点击对象，Enter 确认后写入选择");
    }

    static void BeginCreateWall(IHost host)
    {
        var form = new PromptForm { Title = "创建墙" }
            .AddNumber("thickness", "厚度 (m)", 0.2, 0.01, 5)
            .AddNumber("height", "高度 (m)", 3, 0.1, 50);
        if (!host.Ui.ShowForm(form))
        {
            return;
        }
        var thickness = form.Number("thickness");
        var height = form.Number("height");
        host.BeginPointInput(
            new PointInputOptions
            {
                MinPoints = 2,
                MaxPoints = 2,
                GridSnap = true,
                PreviewKind = PointInputPreviewKind.Wall,
            },
            result =>
            {
                if (result.Cancelled || result.Points.Count < 2)
                {
                    return;
                }
                host.Wall(result.Points[0], result.Points[1], thickness, height);
                host.Log("已创建墙");
            });
    }
}
