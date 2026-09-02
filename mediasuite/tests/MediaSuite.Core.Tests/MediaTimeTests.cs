using System.Globalization;
using MediaSuite.Core.Engines;
using Xunit;

namespace MediaSuite.Core.Tests;

public class MediaTimeTests
{
    [Theory]
    [InlineData("90", 0, 1, 30)]
    [InlineData("90.5", 0, 1, 30.5)]
    [InlineData("1:30", 0, 1, 30)]
    [InlineData("02:15", 0, 2, 15)]
    [InlineData("00:01:30", 0, 1, 30)]
    [InlineData("1:02:03", 1, 2, 3)]
    [InlineData("00:00:12.250", 0, 0, 12.25)]
    public void Every_way_a_user_might_type_a_timecode_parses(string text, int hours, int minutes, double seconds)
    {
        Assert.True(MediaTime.TryParse(text, out var value));
        Assert.Equal(
            TimeSpan.FromHours(hours) + TimeSpan.FromMinutes(minutes) + TimeSpan.FromSeconds(seconds),
            value);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData(null)]
    [InlineData("soon")]
    [InlineData("-5")]
    [InlineData("1:2:3:4")]
    [InlineData("1:-30")]
    public void Nonsense_is_rejected_rather_than_guessed_at(string? text)
    {
        Assert.False(MediaTime.TryParse(text, out _));
    }

    [Fact]
    public void Formatting_is_the_shape_FFmpeg_documents()
    {
        Assert.Equal("01:02:03.500", MediaTime.Format(new TimeSpan(0, 1, 2, 3, 500)));
        Assert.Equal("00:00:05.000", MediaTime.Format(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public void Parsing_and_formatting_ignore_the_machine_locale()
    {
        // In a comma-decimal locale, "90.5" must still mean ninety and a half seconds and
        // the output must still use a dot, or FFmpeg reads a different time entirely.
        var previous = Thread.CurrentThread.CurrentCulture;
        Thread.CurrentThread.CurrentCulture = new CultureInfo("de-DE");

        try
        {
            Assert.True(MediaTime.TryParse("90.5", out var value));
            Assert.Equal(TimeSpan.FromSeconds(90.5), value);
            Assert.Equal("00:01:30.500", MediaTime.Format(value));
        }
        finally
        {
            Thread.CurrentThread.CurrentCulture = previous;
        }
    }
}
