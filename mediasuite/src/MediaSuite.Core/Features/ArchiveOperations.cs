namespace MediaSuite.Core.Features;

/// <summary>The archive operation, and what each format the picker offers implies.</summary>
public static class ArchiveOperations
{
    public static IReadOnlySet<string> All { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "archive.convert",
    };
}
