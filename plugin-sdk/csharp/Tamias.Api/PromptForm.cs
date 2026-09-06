using System.Globalization;
using System.Text;

namespace Tamias.Api;

public sealed class PromptForm
{
    readonly List<PromptField> fields_ = [];

    public string Title { get; set; } = "";
    public IReadOnlyList<PromptField> Fields => fields_;

    public PromptForm AddString(string id, string label, string value = "")
    {
        fields_.Add(new PromptField
        {
            Kind = PromptFieldKind.String,
            Id = RequireId(id),
            Label = label,
            Text = value,
        });
        return this;
    }

    public PromptForm AddNumber(string id, string label, double value, double? min = null, double? max = null)
    {
        fields_.Add(new PromptField
        {
            Kind = PromptFieldKind.Number,
            Id = RequireId(id),
            Label = label,
            Number = value,
            Min = min ?? 0,
            Max = max ?? 0,
            HasRange = min.HasValue && max.HasValue,
        });
        return this;
    }

    public PromptForm AddBool(string id, string label, bool value = false)
    {
        fields_.Add(new PromptField
        {
            Kind = PromptFieldKind.Bool,
            Id = RequireId(id),
            Label = label,
            Flag = value,
        });
        return this;
    }

    public string String(string id) => Field(id).Text;

    public double Number(string id) => Field(id).Number;

    public bool Bool(string id) => Field(id).Flag;

    internal string ToSpec()
    {
        var sb = new StringBuilder();
        sb.Append("T|").Append(Escape(Title)).Append('\n');
        foreach (var field in fields_)
        {
            sb.Append("F|");
            switch (field.Kind)
            {
                case PromptFieldKind.Number:
                    sb.Append("n|").Append(Escape(field.Id)).Append('|').Append(Escape(field.Label)).Append('|')
                        .Append(field.Number.ToString("G17", CultureInfo.InvariantCulture));
                    if (field.HasRange)
                    {
                        sb.Append('|')
                            .Append(field.Min.ToString("G17", CultureInfo.InvariantCulture)).Append('|')
                            .Append(field.Max.ToString("G17", CultureInfo.InvariantCulture));
                    }
                    break;
                case PromptFieldKind.Bool:
                    sb.Append("b|").Append(Escape(field.Id)).Append('|').Append(Escape(field.Label)).Append('|')
                        .Append(field.Flag ? "1" : "0");
                    break;
                default:
                    sb.Append("s|").Append(Escape(field.Id)).Append('|').Append(Escape(field.Label)).Append('|')
                        .Append(Escape(field.Text));
                    break;
            }
            sb.Append('\n');
        }
        return sb.ToString();
    }

    internal void ApplyResult(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return;
        }
        foreach (var token in text.Split(';', StringSplitOptions.RemoveEmptyEntries))
        {
            var colon = token.IndexOf(':');
            var eq = token.IndexOf('=');
            if (colon < 0 || eq <= colon + 1)
            {
                continue;
            }
            var id = token[(colon + 1)..eq];
            var value = token[(eq + 1)..];
            var field = fields_.Find(item => item.Id == id);
            if (field == null)
            {
                continue;
            }
            switch (field.Kind)
            {
                case PromptFieldKind.Number:
                    field.Number = double.Parse(value, CultureInfo.InvariantCulture);
                    break;
                case PromptFieldKind.Bool:
                    field.Flag = value is "1" or "true" or "True";
                    break;
                default:
                    field.Text = value;
                    break;
            }
        }
    }

    PromptField Field(string id)
    {
        var field = fields_.Find(item => item.Id == id);
        if (field == null)
        {
            throw new KeyNotFoundException("Unknown prompt field '" + id + "'");
        }
        return field;
    }

    static string RequireId(string id)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        return id;
    }

    static string Escape(string text) => (text ?? "").Replace('|', ' ').Replace('\n', '\u001e');
}
