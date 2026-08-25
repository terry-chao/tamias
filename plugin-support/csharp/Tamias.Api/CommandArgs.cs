using System.Globalization;
using System.Text;

namespace Tamias.Api;

public sealed class CommandArgs
{
    readonly List<string> parts_ = [];

    public CommandArgs SetInt(string key, long value)
    {
        parts_.Add("i:" + key + "=" + value.ToString(CultureInfo.InvariantCulture));
        return this;
    }

    public CommandArgs SetDouble(string key, double value)
    {
        parts_.Add("d:" + key + "=" + value.ToString("G17", CultureInfo.InvariantCulture));
        return this;
    }

    public CommandArgs SetString(string key, string value)
    {
        parts_.Add("s:" + key + "=" + value);
        return this;
    }

    public CommandArgs SetVec3(string key, float x, float y, float z)
    {
        parts_.Add(
            "v:" + key + "=" +
            x.ToString("G9", CultureInfo.InvariantCulture) + "," +
            y.ToString("G9", CultureInfo.InvariantCulture) + "," +
            z.ToString("G9", CultureInfo.InvariantCulture));
        return this;
    }

    public override string ToString()
    {
        var sb = new StringBuilder();
        for (var i = 0; i < parts_.Count; ++i)
        {
            if (i > 0)
            {
                sb.Append(';');
            }
            sb.Append(parts_[i]);
        }
        return sb.ToString();
    }
}
