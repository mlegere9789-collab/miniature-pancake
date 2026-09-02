using System.Diagnostics.CodeAnalysis;

namespace MediaSuite.Core.Tooling;

/// <summary>How a binary was found.</summary>
public enum ToolSource
{
    /// <summary>Not on disk anywhere we looked.</summary>
    NotFound,

    /// <summary>An explicit path the user set in Settings.</summary>
    Override,

    /// <summary>Inside one of the app's tools folders.</summary>
    Bundled,

    /// <summary>On the system PATH — a machine-wide install we happened to find.</summary>
    SystemPath,
}

/// <summary>Result of looking for one binary.</summary>
public sealed record ToolLocation(ExternalToolId Id, string? Path, ToolSource Source)
{
    [MemberNotNullWhen(true, nameof(Path))]
    public bool Found => Path is not null;

    public static ToolLocation Missing(ExternalToolId id) => new(id, null, ToolSource.NotFound);
}
