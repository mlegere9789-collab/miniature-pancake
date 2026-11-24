namespace MediaSuite.Core.Jobs;

/// <summary>
/// Works out where a converted file goes: applies the name template, the target
/// extension, the folder-structure option and the overwrite policy.
/// </summary>
public static class OutputPathResolver
{
    // JobLauncher turns a multi-file batch into one JobSpec per input file, specifically so
    // JobQueueManager can run several of them at once (MaxConcurrentJobs defaults to
    // Environment.ProcessorCount) -- and every engine calls Resolve() from inside its own
    // job execution, at the moment that job starts, not once up front for the whole batch.
    // A plain File.Exists check is a real TOCTOU race there: two jobs whose inputs would
    // produce the same output name (same filename from different subfolders with folder
    // structure off, or two different extensions converting to the same target extension)
    // can both see "nothing there yet" before either has actually written its file, both
    // get handed the identical path, and one process's output silently clobbers or
    // corrupts the other's -- exactly what OverwritePolicy.Rename exists to prevent, just
    // not fast enough against real concurrency. This reservation set closes that window by
    // making the naming decision itself atomic, in-process, with no per-call-site changes
    // needed anywhere. It is deliberately never pruned: the one cost is that a name whose
    // job later failed or was cancelled before writing anything stays "reserved" for the
    // rest of the app's run, so a subsequent unrelated conversion to that exact same path
    // gets a needlessly incremented "(1)" instead of reusing the genuinely free slot --
    // strictly more conservative than today's behavior, never a data-loss risk, and the
    // same "would rather rename than gamble" philosophy Rename already embodies as the
    // default policy.
    private static readonly object ReservationLock = new();
    private static readonly HashSet<string> ReservedPaths = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// Resolves the output path for one input file and creates the folder it lives in.
    /// </summary>
    /// <param name="inputPath">File being converted.</param>
    /// <param name="target">Where the job writes and under what name.</param>
    /// <param name="index">1-based position in the batch, for the <c>{index}</c> token.</param>
    /// <param name="batchRoot">
    /// Common folder the batch was taken from. Only used when
    /// <see cref="OutputTarget.PreserveFolderStructure"/> is set, to rebuild the tree.
    /// </param>
    public static string Resolve(string inputPath, OutputTarget target, int index = 1, string? batchRoot = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(inputPath);
        ArgumentNullException.ThrowIfNull(target);

        var directory = ResolveDirectory(inputPath, target, batchRoot);
        Directory.CreateDirectory(directory);

        var fileName = BuildFileName(inputPath, target, index);
        var candidate = Path.Combine(directory, fileName);

        return ApplyOverwritePolicy(candidate, target.OverwritePolicy);
    }

    /// <summary>
    /// Deepest folder that contains every input, used as the base when recreating the
    /// source tree. Null when the inputs span drives or share nothing.
    /// </summary>
    public static string? FindCommonRoot(IReadOnlyList<string> inputPaths)
    {
        ArgumentNullException.ThrowIfNull(inputPaths);

        if (inputPaths.Count == 0)
        {
            return null;
        }

        string? common = null;

        foreach (var path in inputPaths)
        {
            var directory = Path.GetDirectoryName(Path.GetFullPath(path));
            if (string.IsNullOrEmpty(directory))
            {
                return null;
            }

            common = common is null ? directory : CommonPrefix(common, directory);

            if (string.IsNullOrEmpty(common))
            {
                return null;
            }
        }

        return common;
    }

    /// <summary>Fills the <c>{name}</c>, <c>{ext}</c> and <c>{index}</c> tokens.</summary>
    public static string ApplyTemplate(string template, string name, string extension, int index)
    {
        var result = string.IsNullOrWhiteSpace(template) ? "{name}.{ext}" : template;

        return result
            .Replace("{name}", name, StringComparison.OrdinalIgnoreCase)
            .Replace("{ext}", extension, StringComparison.OrdinalIgnoreCase)
            .Replace("{index}", index.ToString(System.Globalization.CultureInfo.InvariantCulture), StringComparison.OrdinalIgnoreCase);
    }

    private static string ResolveDirectory(string inputPath, OutputTarget target, string? batchRoot)
    {
        if (!target.PreserveFolderStructure || string.IsNullOrWhiteSpace(batchRoot))
        {
            return target.Directory;
        }

        var inputDirectory = Path.GetDirectoryName(Path.GetFullPath(inputPath));
        if (string.IsNullOrEmpty(inputDirectory))
        {
            return target.Directory;
        }

        var root = Path.GetFullPath(batchRoot);
        if (!inputDirectory.StartsWith(root, StringComparison.OrdinalIgnoreCase))
        {
            // Outside the batch root — flatten rather than climbing out of the output folder.
            return target.Directory;
        }

        var relative = Path.GetRelativePath(root, inputDirectory);

        return relative is "." or ""
            ? target.Directory
            : Path.Combine(target.Directory, relative);
    }

    private static string BuildFileName(string inputPath, OutputTarget target, int index)
    {
        var name = Path.GetFileNameWithoutExtension(inputPath);
        var extension = (target.Format ?? Path.GetExtension(inputPath).TrimStart('.')).TrimStart('.');

        var fileName = ApplyTemplate(target.FileNameTemplate, name, extension, index);
        return Sanitize(fileName);
    }

    private static string ApplyOverwritePolicy(string candidate, OverwritePolicy policy)
    {
        // Overwrite is a deliberate "yes, last write wins" choice, so two colliding jobs
        // both legitimately targeting the same path under this policy is the user's own
        // call, not something to guard against here.
        if (policy == OverwritePolicy.Overwrite)
        {
            return candidate;
        }

        lock (ReservationLock)
        {
            if (!File.Exists(candidate) && ReservedPaths.Add(candidate))
            {
                return candidate;
            }

            if (policy == OverwritePolicy.Fail)
            {
                throw new IOException($"'{Path.GetFileName(candidate)}' already exists in the output folder.");
            }

            return NextFreeName(candidate);
        }
    }

    /// <summary>Must be called while holding <see cref="ReservationLock"/>.</summary>
    private static string NextFreeName(string candidate)
    {
        var directory = Path.GetDirectoryName(candidate) ?? string.Empty;
        var name = Path.GetFileNameWithoutExtension(candidate);
        var extension = Path.GetExtension(candidate);

        for (var suffix = 1; suffix < int.MaxValue; suffix++)
        {
            var attempt = Path.Combine(directory, $"{name} ({suffix}){extension}");

            if (!File.Exists(attempt) && ReservedPaths.Add(attempt))
            {
                return attempt;
            }
        }

        throw new IOException($"Could not find a free name for '{Path.GetFileName(candidate)}'.");
    }

    // Reserved for legacy DOS devices on Windows regardless of any extension -- "con.png" is
    // exactly as reserved as "con" itself. A source file innocently named "com1.jpg" or
    // "nul.png" (an old scan, a placeholder someone named literally) is plausible on a real
    // machine, and without this check the resolver would hand the tool an output path that
    // either fails outright to create or gets silently redirected to the actual device
    // instead of a real file.
    private static readonly HashSet<string> ReservedDeviceNames = new(StringComparer.OrdinalIgnoreCase)
    {
        "CON", "PRN", "AUX", "NUL",
        "COM0", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT0", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };

    private static string Sanitize(string fileName)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var cleaned = new string(fileName.Select(c => invalid.Contains(c) ? '_' : c).ToArray()).Trim();

        // Windows strips trailing dots and spaces from a filename at the point it is
        // actually created (CreateFileW's own path normalization, outside .NET's control) --
        // an extension-less "same as input" conversion (target.Format left null for a source
        // file that itself has no extension) leaves BuildFileName's "{name}.{ext}" template
        // with nothing after that final dot, e.g. "Report." rather than "Report". Stripping
        // it here too means every decision this resolver makes from here on (the File.Exists
        // collision check, the in-process ReservedPaths set) is made against the exact string
        // Windows will actually use on disk, rather than risking the two disagreeing.
        cleaned = cleaned.TrimEnd('.', ' ');

        if (cleaned.Length == 0)
        {
            return "output";
        }

        var baseName = Path.GetFileNameWithoutExtension(cleaned);

        if (ReservedDeviceNames.Contains(baseName))
        {
            cleaned = $"{baseName}_{Path.GetExtension(cleaned)}";
        }

        return cleaned;
    }

    private static string CommonPrefix(string first, string second)
    {
        var separators = new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar };
        var firstParts = first.Split(separators, StringSplitOptions.RemoveEmptyEntries);
        var secondParts = second.Split(separators, StringSplitOptions.RemoveEmptyEntries);

        var shared = new List<string>();

        for (var i = 0; i < Math.Min(firstParts.Length, secondParts.Length); i++)
        {
            if (!string.Equals(firstParts[i], secondParts[i], StringComparison.OrdinalIgnoreCase))
            {
                break;
            }

            shared.Add(firstParts[i]);
        }

        if (shared.Count == 0)
        {
            return string.Empty;
        }

        // Rebuild with the original root so rooted paths stay rooted.
        var rebuilt = string.Join(Path.DirectorySeparatorChar, shared);

        return Path.IsPathRooted(first) && !Path.IsPathRooted(rebuilt)
            ? Path.DirectorySeparatorChar + rebuilt
            : rebuilt;
    }
}
