using Tamias.Api;

namespace Tamias.Nurbs;

public sealed class NurbsPlugin : IPlugin
{
    public PluginMetadata Metadata => new()
    {
        Id = "tamias.nurbs",
        Name = "NURBS",
        Author = "Tamias",
        IsBuiltIn = true,
        Version = "1.0.0",
        ReleaseDate = "2026-08-25",
        Description = "通过控制点创建 NURBS 曲线。",
        HomepageUrl = "https://github.com/terry-chao/tamias",
        IconPath = "nurbs.svg",
    };

    public void Load(IHost host)
    {
        var icon = Path.Combine(
            Path.GetDirectoryName(typeof(NurbsPlugin).Assembly.Location) ?? "",
            "nurbs.svg");
        host.AddCommand(
            "tamias.nurbs.create",
            "NURBS",
            () => BeginCreate(host),
            "Create a NURBS from control points",
            new RibbonPlacement
            {
                PageId = "home",
                GroupId = "draw",
                IconPath = icon,
                Order = 700,
                Checkable = true,
            });
    }

    static void BeginCreate(IHost host)
    {
        host.BeginPointInput(
            new PointInputOptions
            {
                MinPoints = 2,
                MaxPoints = 0,
                AllowConfirm = true,
                GridSnap = true,
                PreviewKind = PointInputPreviewKind.Curve,
                PreviewCurveKind = "nurbs",
            },
            result =>
            {
                if (result.Cancelled)
                {
                    return;
                }
                host.Dispatch(
                    "create_curve",
                    new CommandArgs()
                        .SetString("curve_kind", "nurbs")
                        .SetPoints("points", result.Points));
            });
    }
}
