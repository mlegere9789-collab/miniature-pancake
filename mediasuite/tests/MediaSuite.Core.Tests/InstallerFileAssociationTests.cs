using MediaSuite.Core.Formats;
using Xunit;

namespace MediaSuite.Core.Tests;

/// <summary>
/// <c>installer/MediaSuite.iss</c> registers MediaSuite as an "Open with" choice in
/// Explorer for every extension in <see cref="FormatCatalog"/> — see its
/// <c>[Registry]</c> section. That list is hand-written, not generated from the
/// catalogue, so nothing stops the two from drifting apart the next time a format is
/// added or removed. This reads the real .iss file off disk and fails loudly the moment
/// they disagree, rather than leaving a newly-supported format quietly missing from the
/// installer forever.
/// </summary>
public class InstallerFileAssociationTests
{
    [Fact]
    public void Every_catalogue_extension_is_registered_as_a_SupportedType()
    {
        var script = File.ReadAllText(FindInstallerScript());

        var missing = FormatCatalog.All
            .SelectMany(format => format.AllExtensions)
            .Where(extension => !script.Contains($"ValueName: \".{extension}\";", StringComparison.Ordinal))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(extension => extension, StringComparer.OrdinalIgnoreCase)
            .ToList();

        Assert.True(missing.Count == 0,
            "installer/MediaSuite.iss's [Registry] SupportedTypes list is missing: "
            + string.Join(", ", missing)
            + ". Add a matching \"Root: HKA; Subkey: ...SupportedTypes...\" line for each.");
    }

    /// <summary>
    /// Walks up from the test's own output directory to the repo checkout that contains
    /// <c>MediaSuite.sln</c>, then down into <c>installer/</c> — avoids hard-coding a
    /// relative path that would break the moment the test project's own output layout
    /// changes.
    /// </summary>
    private static string FindInstallerScript()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null && !File.Exists(Path.Combine(directory.FullName, "MediaSuite.sln")))
        {
            directory = directory.Parent;
        }

        if (directory is null)
        {
            throw new FileNotFoundException(
                "Could not locate MediaSuite.sln by walking up from the test output directory.");
        }

        return Path.Combine(directory.FullName, "installer", "MediaSuite.iss");
    }
}
