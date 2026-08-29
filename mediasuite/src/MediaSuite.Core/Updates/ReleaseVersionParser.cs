namespace MediaSuite.Core.Updates;

/// <summary>
/// Parses and compares release tags. Pure, so the actual "is this newer" decision is
/// unit-tested directly rather than only exercised through a live network call.
/// </summary>
public static class ReleaseVersionParser
{
    /// <summary>
    /// Parses a GitHub release tag such as "v0.2.0" or "1.4.10" into a <see cref="Version"/>.
    /// Returns null for anything that isn't a plain dotted version — a hand-written tag
    /// like "nightly" or "v1.0-beta" is not something this app should ever compare
    /// against, since a bad parse offering a downgrade or a bogus "update" is worse than
    /// silently skipping the check.
    /// </summary>
    public static Version? Parse(string? tag)
    {
        if (string.IsNullOrWhiteSpace(tag))
        {
            return null;
        }

        var trimmed = tag.Trim().TrimStart('v', 'V');
        return Version.TryParse(trimmed, out var version) ? version : null;
    }

    /// <summary>True when <paramref name="candidate"/> is a real, strictly newer version than <paramref name="current"/>.</summary>
    public static bool IsNewerThan(Version? candidate, Version current) =>
        candidate is not null && candidate > current;

    /// <summary>"1.2.0" — major.minor.patch, dropping the fourth (revision) component .NET assembly versions always carry.</summary>
    public static string Format(Version version) => $"{version.Major}.{version.Minor}.{version.Build}";
}
