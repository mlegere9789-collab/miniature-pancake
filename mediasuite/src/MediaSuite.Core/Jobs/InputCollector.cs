namespace MediaSuite.Core.Jobs;

/// <summary>
/// Turns whatever the user dropped — files, folders, a mix of both — into a flat,
/// de-duplicated list of files, keeping the order they were dropped in.
/// </summary>
public static class InputCollector
{
    /// <summary>
    /// Expands <paramref name="paths"/> into files. Folders are walked (recursively by
    /// default); unreadable folders are skipped rather than failing the whole drop,
    /// because one locked system folder should not cost the user a 5,000-file batch.
    /// </summary>
    public static IReadOnlyList<string> Expand(IEnumerable<string?>? paths, bool recurse = true)
    {
        var files = new List<string>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        if (paths is null)
        {
            return files;
        }

        foreach (var path in paths)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                continue;
            }

            if (File.Exists(path))
            {
                Add(path);
            }
            else if (Directory.Exists(path))
            {
                foreach (var file in EnumerateFilesSafely(path, recurse))
                {
                    Add(file);
                }
            }
        }

        return files;

        void Add(string file)
        {
            var full = TryGetFullPath(file);
            if (full is not null && seen.Add(full))
            {
                files.Add(full);
            }
        }
    }

    private static IEnumerable<string> EnumerateFilesSafely(string directory, bool recurse)
    {
        var options = new EnumerationOptions
        {
            RecurseSubdirectories = recurse,
            IgnoreInaccessible = true,
            AttributesToSkip = FileAttributes.System,
        };

        // Enumeration is lazy, so a folder that becomes unreadable mid-walk throws from
        // MoveNext rather than from the call below — hence the manual enumerator loop.
        IEnumerator<string> enumerator;
        try
        {
            enumerator = Directory.EnumerateFiles(directory, "*", options).GetEnumerator();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            yield break;
        }

        using (enumerator)
        {
            while (true)
            {
                string current;
                try
                {
                    if (!enumerator.MoveNext())
                    {
                        break;
                    }

                    current = enumerator.Current;
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    break;
                }

                yield return current;
            }
        }
    }

    private static string? TryGetFullPath(string path)
    {
        try
        {
            return Path.GetFullPath(path);
        }
        catch (Exception ex) when (ex is ArgumentException or PathTooLongException or NotSupportedException)
        {
            return null;
        }
    }
}
