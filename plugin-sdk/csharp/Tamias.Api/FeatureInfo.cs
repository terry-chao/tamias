namespace Tamias.Api;

// 特征树里的一条，只读快照。Inputs 是它依赖的上游特征 id（依赖在前，拓扑序）。
// 拿到它就能做两件以前做不到的事：按名字改参数（不必再猜 feature_id）、
// 给脚本编辑器补全参数名。
public readonly record struct FeatureInfo(
    ulong Id,
    FeatureKind Kind,
    IReadOnlyList<ulong> Inputs,
    IReadOnlyList<FeatureParam> Params);
