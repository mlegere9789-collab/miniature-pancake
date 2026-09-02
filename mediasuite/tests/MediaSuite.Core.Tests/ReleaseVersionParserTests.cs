using MediaSuite.Core.Updates;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ReleaseVersionParserTests
{
    [Theory]
    [InlineData("v0.2.0", 0, 2, 0)]
    [InlineData("V1.4.10", 1, 4, 10)]
    [InlineData("2.0.0", 2, 0, 0)]
    [InlineData("  v0.1.0  ", 0, 1, 0)]
    public void Parse_reads_a_plain_dotted_version_with_an_optional_v_prefix(
        string tag, int major, int minor, int build)
    {
        var version = ReleaseVersionParser.Parse(tag);

        Assert.NotNull(version);
        Assert.Equal(major, version!.Major);
        Assert.Equal(minor, version.Minor);
        Assert.Equal(build, version.Build);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("nightly")]
    [InlineData("v1.0-beta")]
    [InlineData("not-a-version")]
    public void Parse_returns_null_for_anything_that_is_not_a_plain_dotted_version(string? tag)
    {
        Assert.Null(ReleaseVersionParser.Parse(tag));
    }

    [Fact]
    public void IsNewerThan_is_true_for_a_genuinely_later_version()
    {
        var current = new Version(0, 1, 0);
        var candidate = new Version(0, 2, 0);

        Assert.True(ReleaseVersionParser.IsNewerThan(candidate, current));
    }

    [Fact]
    public void IsNewerThan_is_false_for_the_same_version()
    {
        var current = new Version(0, 1, 0);
        var candidate = new Version(0, 1, 0);

        Assert.False(ReleaseVersionParser.IsNewerThan(candidate, current));
    }

    [Fact]
    public void IsNewerThan_is_false_for_an_older_version()
    {
        var current = new Version(1, 0, 0);
        var candidate = new Version(0, 9, 0);

        Assert.False(ReleaseVersionParser.IsNewerThan(candidate, current));
    }

    [Fact]
    public void IsNewerThan_is_false_for_a_null_candidate()
    {
        Assert.False(ReleaseVersionParser.IsNewerThan(null, new Version(0, 1, 0)));
    }

    [Fact]
    public void IsNewerThan_treats_a_four_component_current_version_the_same_as_its_three_component_equivalent()
    {
        // The running app's own assembly version can carry a fourth (revision) component
        // that a hand-written release tag never does — this must not look like an update.
        var current = new Version(0, 1, 0, 0);
        var candidate = new Version(0, 1, 0);

        Assert.False(ReleaseVersionParser.IsNewerThan(candidate, current));
    }

    [Fact]
    public void Format_drops_the_fourth_assembly_version_component()
    {
        Assert.Equal("1.2.3", ReleaseVersionParser.Format(new Version(1, 2, 3, 0)));
    }
}
