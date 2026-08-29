namespace MediaSuite.Core.Settings;

/// <summary>
/// A user-named set of advanced options for one operation, offered back to them when
/// they pick the Custom preset for that same operation again.
/// </summary>
public sealed record CustomPreset
{
    public required string Name { get; init; }

    public required IReadOnlyDictionary<string, string> Options { get; init; }
}
