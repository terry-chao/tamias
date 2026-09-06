namespace Tamias.Api;

public sealed class PromptField
{
    public PromptFieldKind Kind { get; init; } = PromptFieldKind.String;
    public string Id { get; init; } = "";
    public string Label { get; init; } = "";
    public string Text { get; set; } = "";
    public double Number { get; set; }
    public double Min { get; init; }
    public double Max { get; init; }
    public bool HasRange { get; init; }
    public bool Flag { get; set; }
}
