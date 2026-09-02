using MediaSuite.Core.Engines;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ArchiveCommandBuilderTests
{
    [Theory]
    [InlineData("zip", "zip")]
    [InlineData("7z", "7z")]
    [InlineData("tar", "tar")]
    [InlineData("gz", "gzip")]
    [InlineData("gzip", "gzip")]
    [InlineData(".ZIP", "zip")]
    public void Archive_types_follow_7zips_own_vocabulary(string extension, string expected)
    {
        Assert.Equal(expected, ArchiveCommandBuilder.ArchiveTypeFor(extension));
    }

    [Fact]
    public void Rar_is_refused_since_7zip_cannot_write_it()
    {
        Assert.Throws<ArgumentException>(() => ArchiveCommandBuilder.ArchiveTypeFor("rar"));
    }

    [Fact]
    public void Extracting_answers_every_overwrite_prompt_automatically()
    {
        var arguments = ArchiveCommandBuilder.Extract("archive.zip", @"C:\work\out").ToList();

        Assert.Equal("x", arguments[0]);
        Assert.Equal("archive.zip", arguments[1]);
        Assert.Contains(@"-oC:\work\out", arguments);

        // No space between -o and the path — that space would make 7-Zip see a
        // different, unrelated switch instead of a destination folder.
        Assert.DoesNotContain(@"C:\work\out", arguments);
        Assert.Contains("-y", arguments);
    }

    [Fact]
    public void Creating_an_archive_names_the_type_explicitly_and_lists_every_source()
    {
        var arguments = ArchiveCommandBuilder.CreateArchive("out.zip", new[] { "a.txt", "b.txt" }, "zip").ToList();

        Assert.Equal("a", arguments[0]);
        Assert.Equal("-tzip", arguments[1]);
        Assert.Equal("out.zip", arguments[2]);
        Assert.Contains("a.txt", arguments);
        Assert.Contains("b.txt", arguments);
        Assert.Equal("-y", arguments[^1]);
    }

    [Fact]
    public void An_archive_needs_at_least_one_file()
    {
        Assert.Throws<ArgumentException>(() => ArchiveCommandBuilder.CreateArchive("out.zip", Array.Empty<string>(), "zip"));
    }
}
