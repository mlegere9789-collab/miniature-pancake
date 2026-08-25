namespace MediaSuite.Core.Tooling;

/// <summary>
/// Finds the bundled binaries on disk.
/// </summary>
/// <remarks>
/// Resolution order, first hit wins:
/// <list type="number">
///   <item>an explicit override path from Settings;</item>
///   <item><c>&lt;root&gt;\&lt;folder&gt;\&lt;exe&gt;</c> and <c>&lt;root&gt;\&lt;folder&gt;\bin\&lt;exe&gt;</c>
///   for each search root, so a vendor archive can be unpacked as-is;</item>
///   <item><c>&lt;root&gt;\&lt;exe&gt;</c>, for tools dropped straight into the folder;</item>
///   <item>the system PATH, as a convenience during development.</item>
/// </list>
/// Results are cached until <see cref="Refresh"/> is called, because probing a dozen
/// binaries on every job would hit the disk far more than it needs to.
/// </remarks>
public sealed class ToolLocator
{
    private readonly IReadOnlyList<string> _searchRoots;
    private readonly IReadOnlyDictionary<ExternalToolId, string> _overrides;
    private readonly IReadOnlyList<string> _pathDirectories;
    private readonly Dictionary<ExternalToolId, ToolLocation> _cache = new();
    private readonly object _gate = new();

    public ToolLocator(
        IEnumerable<string> searchRoots,
        IReadOnlyDictionary<ExternalToolId, string>? overrides = null,
        string? pathVariable = null)
    {
        ArgumentNullException.ThrowIfNull(searchRoots);

        _searchRoots = searchRoots
            .Where(root => !string.IsNullOrWhiteSpace(root))
            .Select(root => root.Trim())
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

        _overrides = overrides ?? new Dictionary<ExternalToolId, string>();

        _pathDirectories = (pathVariable ?? Environment.GetEnvironmentVariable("PATH") ?? string.Empty)
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToList();
    }

    /// <summary>
    /// Builds a locator for a running app: the user's own tools folder (if set) wins,
    /// then <c>MEDIASUITE_TOOLS_DIR</c>, then <c>tools\</c> next to the executable.
    /// </summary>
    public static ToolLocator ForApplication(
        string applicationDirectory,
        string? userToolsDirectory = null,
        IReadOnlyDictionary<ExternalToolId, string>? overrides = null)
    {
        var roots = new List<string>();

        if (!string.IsNullOrWhiteSpace(userToolsDirectory))
        {
            roots.Add(userToolsDirectory);
        }

        var fromEnvironment = Environment.GetEnvironmentVariable("MEDIASUITE_TOOLS_DIR");
        if (!string.IsNullOrWhiteSpace(fromEnvironment))
        {
            roots.Add(fromEnvironment);
        }

        roots.Add(Path.Combine(applicationDirectory, "tools"));

        return new ToolLocator(roots, overrides);
    }

    /// <summary>Locates one binary, using the cached answer when there is one.</summary>
    public ToolLocation Locate(ExternalToolId id)
    {
        lock (_gate)
        {
            if (_cache.TryGetValue(id, out var cached))
            {
                return cached;
            }

            var located = Probe(id);
            _cache[id] = located;
            return located;
        }
    }

    /// <summary>Locates every tool in the manifest.</summary>
    public IReadOnlyList<ToolLocation> LocateAll() =>
        ToolManifest.All.Select(tool => Locate(tool.Id)).ToList();

    /// <summary>Tools the manifest marks required that are not installed.</summary>
    public IReadOnlyList<ToolDescriptor> MissingRequiredTools() =>
        ToolManifest.Required.Where(tool => !Locate(tool.Id).Found).ToList();

    /// <summary>Drops the cache, so the next lookup hits the disk again.</summary>
    public void Refresh()
    {
        lock (_gate)
        {
            _cache.Clear();
        }
    }

    private ToolLocation Probe(ExternalToolId id)
    {
        var descriptor = ToolManifest.Get(id);

        if (_overrides.TryGetValue(id, out var overridePath)
            && !string.IsNullOrWhiteSpace(overridePath)
            && File.Exists(overridePath))
        {
            return new ToolLocation(id, Path.GetFullPath(overridePath), ToolSource.Override);
        }

        foreach (var root in _searchRoots)
        {
            foreach (var candidate in CandidatePaths(root, descriptor))
            {
                if (File.Exists(candidate))
                {
                    return new ToolLocation(id, Path.GetFullPath(candidate), ToolSource.Bundled);
                }
            }
        }

        foreach (var directory in _pathDirectories)
        {
            foreach (var executable in descriptor.ExecutableNames)
            {
                string candidate;
                try
                {
                    candidate = Path.Combine(directory, executable);
                }
                catch (ArgumentException)
                {
                    // A malformed PATH entry should not take the whole probe down.
                    continue;
                }

                if (File.Exists(candidate))
                {
                    return new ToolLocation(id, Path.GetFullPath(candidate), ToolSource.SystemPath);
                }
            }
        }

        return ToolLocation.Missing(id);
    }

    private static IEnumerable<string> CandidatePaths(string root, ToolDescriptor descriptor)
    {
        foreach (var executable in descriptor.ExecutableNames)
        {
            yield return Path.Combine(root, descriptor.FolderName, executable);
            yield return Path.Combine(root, descriptor.FolderName, "bin", executable);
            yield return Path.Combine(root, executable);
        }
    }
}
