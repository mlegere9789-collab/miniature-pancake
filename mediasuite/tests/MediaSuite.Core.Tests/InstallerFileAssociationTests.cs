using System.Text.RegularExpressions;
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
    /// The other direction of the check above: a format removed from the catalogue (or
    /// never added correctly in the first place) must not leave a stale SupportedTypes
    /// entry behind. Only checking "every catalogue extension is registered" would let
    /// that drift silently forever, since nothing generates this list from the catalogue.
    /// A stale entry is not a crash today -- <c>App.ResolveOpenWithFiles</c> already
    /// filters "Open with" launches through <see cref="FormatCatalog.FromPath"/> and
    /// drops anything the catalogue no longer recognises -- but it is still a real,
    /// user-visible "Open with MediaSuite" choice for a format the app can no longer
    /// actually do anything with.
    /// </summary>
    [Fact]
    public void Every_registered_SupportedType_still_exists_in_the_catalogue()
    {
        var script = File.ReadAllText(FindInstallerScript());

        var catalogueExtensions = new HashSet<string>(
            FormatCatalog.All.SelectMany(format => format.AllExtensions),
            StringComparer.OrdinalIgnoreCase);

        var registeredExtensions = Regex.Matches(script, @"SupportedTypes""; ValueType: string; ValueName: ""\.([A-Za-z0-9]+)"";")
            .Select(match => match.Groups[1].Value);

        var stale = registeredExtensions
            .Where(extension => !catalogueExtensions.Contains(extension))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(extension => extension, StringComparer.OrdinalIgnoreCase)
            .ToList();

        Assert.True(stale.Count == 0,
            "installer/MediaSuite.iss's [Registry] SupportedTypes list registers extensions "
            + "no longer in FormatCatalog: " + string.Join(", ", stale)
            + ". Remove the matching \"Root: HKA; Subkey: ...SupportedTypes...\" line(s).");
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
