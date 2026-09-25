namespace Tamias.Api;

// 特征参数：模型里参数就是一个名字 + 一个 double（没有类型 / 范围元数据）。
public readonly record struct FeatureParam(string Name, double Value);
