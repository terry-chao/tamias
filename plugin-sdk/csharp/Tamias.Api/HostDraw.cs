namespace Tamias.Api;

public static class HostDraw
{
    public static void Wall(this IHost host, PickPoint start, PickPoint end,
        double thickness = 0.2, double height = 3.0)
    {
        Dispatch(host, "create_wall", new CommandArgs()
            .SetDouble("thickness", thickness)
            .SetDouble("height", height)
            .SetPoints("points", [start, end]));
    }

    public static void Beam(this IHost host, PickPoint start, PickPoint end,
        double width = 0.3, double depth = 0.5)
    {
        Dispatch(host, "create_beam", new CommandArgs()
            .SetDouble("width", width)
            .SetDouble("depth", depth)
            .SetPoints("points", [start, end]));
    }

    public static void Slab(this IHost host, PickPoint a, PickPoint b, double thickness = 0.2,
        double? elevation = null)
    {
        var args = new CommandArgs()
            .SetDouble("thickness", thickness)
            .SetPoints("points", [a, b]);
        if (elevation.HasValue)
        {
            args.SetDouble("elevation", elevation.Value);
        }
        Dispatch(host, "create_slab", args);
    }

    public static void Box(this IHost host, PickPoint origin) => Primitive(host, "create_box", origin);

    public static void Cylinder(this IHost host, PickPoint origin) =>
        Primitive(host, "create_cylinder", origin);

    public static void Column(this IHost host, PickPoint origin) =>
        Primitive(host, "create_column", origin);

    public static void Door(this IHost host, PickPoint origin, ulong hostId = 0) =>
        Primitive(host, "create_door", origin, hostId);

    public static void Window(this IHost host, PickPoint origin, ulong hostId = 0) =>
        Primitive(host, "create_window", origin, hostId);

    public static void Line(this IHost host, PickPoint a, PickPoint b) =>
        Sketch(host, "create_line", [a, b]);

    public static void Polyline(this IHost host, IEnumerable<PickPoint> points) =>
        Sketch(host, "create_polyline", points);

    public static void Circle(this IHost host, PickPoint center, PickPoint radiusPoint) =>
        Sketch(host, "create_circle", [center, radiusPoint]);

    public static void Arc(this IHost host, PickPoint start, PickPoint through, PickPoint end) =>
        Sketch(host, "create_arc", [start, through, end]);

    public static void Rectangle(this IHost host, PickPoint a, PickPoint b) =>
        Sketch(host, "create_rectangle", [a, b]);

    public static void Bezier(this IHost host, IEnumerable<PickPoint> points) =>
        Sketch(host, "create_bezier", points);

    public static void BSpline(this IHost host, IEnumerable<PickPoint> points) =>
        Sketch(host, "create_bspline", points);

    static void Primitive(IHost host, string command, PickPoint origin, ulong hostId = 0)
    {
        var args = new CommandArgs().SetPoints("points", [origin]);
        if (hostId != 0)
        {
            args.SetInt("host_id", (long)hostId);
        }
        Dispatch(host, command, args);
    }

    static void Sketch(IHost host, string command, IEnumerable<PickPoint> points)
    {
        Dispatch(host, command, new CommandArgs().SetPoints("points", points));
    }

    static void Dispatch(IHost host, string command, CommandArgs args)
    {
        ArgumentNullException.ThrowIfNull(host);
        host.Dispatch(command, args);
    }
}
