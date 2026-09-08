namespace MediaSuite.Core.Jobs;

/// <summary>
/// Puts scratch folders on disk under a root the user controls — the default local app
/// data folder, or a RAM disk if they point Settings at one.
/// </summary>
/// <remarks>
/// Disk rather than memory is the default deliberately: a 10,000-file batch would
/// otherwise be bounded by RAM instead of by free space.
/// </remarks>
public sealed class DiskTempWorkspaceFactory : ITempWorkspaceFactory
{
    private readonly string _root;

    public DiskTempWorkspaceFactory(string root)
    {
        if (string.IsNullOrWhiteSpace(root))
        {
            throw new ArgumentException("A temp root is required.", nameof(root));
        }

        _root = root;
    }

    public string Root => _root;

    public TempWorkspace Create(Guid jobId)
    {
        var path = Path.Combine(_root, jobId.ToString("N"));
        Directory.CreateDirectory(path);
        return new TempWorkspace(path);
    }

    /// <summary>
    /// Deletes workspaces left behind by a previous session — a crash or a kill during a
    /// job skips the normal cleanup, and those folders can be large.
    /// </summary>
    /// <param name="olderThan">Only remove folders last written to before this age.</param>
    /// <returns>How many folders were removed.</returns>
    public int PurgeStaleWorkspaces(TimeSpan olderThan)
    {
        if (!Directory.Exists(_root))
        {
            return 0;
        }

        var cutoff = DateTime.UtcNow - olderThan;
        var removed = 0;

        foreach (var directory in EnumerateWorkspaceFolders())
        {
            try
            {
                if (Directory.GetLastWriteTimeUtc(directory) > cutoff)
                {
                    continue;
                }

                Directory.Delete(directory, recursive: true);
                removed++;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Still in use by another instance of the app; leave it alone.
            }
        }

        return removed;
    }

    private IReadOnlyList<string> EnumerateWorkspaceFolders()
    {
        try
        {
            // Only a folder this factory itself created -- named by Create() as a job's
            // own Guid in "N" format -- is ours to delete. _root is user-configurable
            // (Settings can point it at any folder, including one already used for
            // something else), so without this filter a stale, unrelated subfolder the
            // user happens to keep there would get silently wiped just for being older
            // than the cutoff.
            return Directory.GetDirectories(_root)
                .Where(directory => Guid.TryParseExact(Path.GetFileName(directory), "N", out _))
                .ToArray();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return Array.Empty<string>();
        }
    }
}
