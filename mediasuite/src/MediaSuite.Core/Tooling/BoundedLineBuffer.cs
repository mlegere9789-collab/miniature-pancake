using System.Text;

namespace MediaSuite.Core.Tooling;

/// <summary>
/// Keeps only the last N lines written to it. Tool output is only ever used for
/// diagnostics, and the useful part is the end, so there is no reason to hold a
/// gigabyte of FFmpeg progress lines to show the user one error.
/// </summary>
internal sealed class BoundedLineBuffer
{
    private readonly Queue<string> _lines = new();
    private readonly int _limit;

    public BoundedLineBuffer(int limit) => _limit = Math.Max(1, limit);

    public void Add(string line)
    {
        _lines.Enqueue(line);

        while (_lines.Count > _limit)
        {
            _lines.Dequeue();
        }
    }

    public override string ToString()
    {
        var builder = new StringBuilder();

        foreach (var line in _lines)
        {
            builder.AppendLine(line);
        }

        return builder.ToString().TrimEnd();
    }
}
