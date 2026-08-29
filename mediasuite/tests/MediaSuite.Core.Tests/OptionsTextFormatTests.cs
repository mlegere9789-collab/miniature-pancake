using MediaSuite.Core.Settings;
using Xunit;

namespace MediaSuite.Core.Tests;

public class OptionsTextFormatTests
{
    [Fact]
    public void Parse_reads_one_key_value_pair_per_line()
    {
        var options = OptionsTextFormat.Parse("crf=20\nbitrate=4000k");

        Assert.Equal("20", options["crf"]);
        Assert.Equal("4000k", options["bitrate"]);
        Assert.Equal(2, options.Count);
    }

    [Fact]
    public void Parse_trims_whitespace_around_keys_and_values()
    {
        var options = OptionsTextFormat.Parse("  crf  =  20  ");

        Assert.Equal("20", options["crf"]);
    }

    [Fact]
    public void Parse_ignores_blank_lines()
    {
        var options = OptionsTextFormat.Parse("crf=20\n\n   \nbitrate=4000k");

        Assert.Equal(2, options.Count);
    }

    [Fact]
    public void Parse_ignores_lines_with_no_equals_sign()
    {
        var options = OptionsTextFormat.Parse("crf=20\nthis is just a note");

        Assert.Single(options);
        Assert.Equal("20", options["crf"]);
    }

    [Fact]
    public void Parse_ignores_lines_with_a_blank_key()
    {
        var options = OptionsTextFormat.Parse("=20\ncrf=18");

        Assert.Single(options);
        Assert.Equal("18", options["crf"]);
    }

    [Fact]
    public void Parse_keeps_everything_after_the_first_equals_sign_as_the_value()
    {
        var options = OptionsTextFormat.Parse(@"path=C:\a=b\c");

        Assert.Equal(@"C:\a=b\c", options["path"]);
    }

    [Fact]
    public void Parse_allows_an_empty_value()
    {
        var options = OptionsTextFormat.Parse("flag=");

        Assert.Equal(string.Empty, options["flag"]);
    }

    [Fact]
    public void Parse_handles_windows_line_endings()
    {
        var options = OptionsTextFormat.Parse("crf=20\r\nbitrate=4000k\r\n");

        Assert.Equal(2, options.Count);
        Assert.Equal("20", options["crf"]);
        Assert.Equal("4000k", options["bitrate"]);
    }

    [Fact]
    public void Parse_last_duplicate_key_wins()
    {
        var options = OptionsTextFormat.Parse("crf=20\ncrf=18");

        Assert.Equal("18", options["crf"]);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    public void Parse_returns_an_empty_map_for_blank_input(string? text)
    {
        var options = OptionsTextFormat.Parse(text);

        Assert.Empty(options);
    }

    [Fact]
    public void Format_writes_one_key_value_pair_per_line()
    {
        var options = new Dictionary<string, string> { ["crf"] = "20", ["bitrate"] = "4000k" };

        var text = OptionsTextFormat.Format(options);

        Assert.Equal("crf=20\nbitrate=4000k", text);
    }

    [Fact]
    public void Format_of_an_empty_map_is_an_empty_string()
    {
        Assert.Equal(string.Empty, OptionsTextFormat.Format(new Dictionary<string, string>()));
    }

    [Fact]
    public void Format_and_parse_round_trip()
    {
        var options = new Dictionary<string, string> { ["crf"] = "20", ["denoise"] = "true" };

        var roundTripped = OptionsTextFormat.Parse(OptionsTextFormat.Format(options));

        Assert.Equal(options, roundTripped);
    }
}
