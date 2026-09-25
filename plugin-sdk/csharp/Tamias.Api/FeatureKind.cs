namespace Tamias.Api;

// 特征种类：数值与 C++ FeatureKind 同序（只追加，不复用旧值）。
public enum FeatureKind
{
    Unknown = -1,
    RectProfile = 0,
    Extrude = 1,
    CircleProfile = 2,
    Boolean = 3,
    Fillet = 4,
    Chamfer = 5,
    Line = 6,
    Polyline = 7,
    CircleWire = 8,
    Arc = 9,
    Bezier = 10,
    RectWire = 11,
    BSpline = 12,
    Nurbs = 13,
    PolygonProfile = 14,
    Transform = 15,
    Cylinder = 16,
}
