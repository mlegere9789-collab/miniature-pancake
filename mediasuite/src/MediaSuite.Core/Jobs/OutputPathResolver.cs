namespace MediaSuite.Core.Jobs;

/// <summary>
/// Works out where a converted file goes: applies the name template, the target
/// extension, the folder-structure option and the overwrite policy.
/// </summary>
public static class OutputPathResolver
{
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
        if (!File.Exists(candidate))
        {
            return candidate;
        }

        switch (policy)
        {
            case OverwritePolicy.Overwrite:
                return candidate;

            case OverwritePolicy.Fail:
                throw new IOException($"'{Path.GetFileName(candidate)}' already exists in the output folder.");

            default:
                return NextFreeName(candidate);
        }
    }

    private static string NextFreeName(string candidate)
    {
        var directory = Path.GetDirectoryName(candidate) ?? string.Empty;
        var name = Path.GetFileNameWithoutExtension(candidate);
        var extension = Path.GetExtension(candidate);

        for (var suffix = 1; suffix < int.MaxValue; suffix++)
        {
            var attempt = Path.Combine(directory, $"{name} ({suffix}){extension}");

            if (!File.Exists(attempt))
            {
                return attempt;
            }
        }

        throw new IOException($"Could not find a free name for '{Path.GetFileName(candidate)}'.");
    }

    private static string Sanitize(string fileName)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var cleaned = new string(fileName.Select(c => invalid.Contains(c) ? '_' : c).ToArray()).Trim();

        return cleaned.Length == 0 ? "output" : cleaned;
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
