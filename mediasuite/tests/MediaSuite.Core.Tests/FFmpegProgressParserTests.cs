using MediaSuite.Core.Engines;
using Xunit;

namespace MediaSuite.Core.Tests;

public class FFmpegProgressParserTests
{
    [Fact]
    public void The_formatted_time_field_is_read()
    {
        Assert.Equal(TimeSpan.FromSeconds(12.34), FFmpegProgressParser.TryParseTime("out_time=00:00:12.340000"));
    }

    [Fact]
    public void The_microsecond_field_is_read()
    {
        Assert.Equal(TimeSpan.FromSeconds(12.5), FFmpegProgressParser.TryParseTime("out_time_us=12500000"));
    }

    [Fact]
    public void The_misnamed_millisecond_field_is_ignored()
    {
        // FFmpeg writes microseconds into out_time_ms despite the name. Reading it would
        // put progress out by a factor of a thousand, so it is skipped entirely.
        Assert.Null(FFmpegProgressParser.TryParseTime("out_time_ms=12500000"));
    }

    [Theory]
    [InlineData("out_time=N/A")]
    [InlineData("out_time_us=N/A")]
    [InlineData("frame=120")]
    [InlineData("bitrate= 1500.2kbits/s")]
    [InlineData("speed=1.02x")]
    [InlineData("")]
    [InlineData(null)]
    [InlineData("no-equals-sign")]
    [InlineData("=leading")]
    public void Lines_without_a_usable_timestamp_report_nothing(string? line)
    {
        Assert.Null(FFmpegProgressParser.TryParseTime(line));
    }

    [Fact]
    public void A_negative_microsecond_value_is_refused()
    {
        // FFmpeg can emit a negative position while priming the encoder.
        Assert.Null(FFmpegProgressParser.TryParseTime("out_time_us=-42"));
    }

    [Fact]
    public void The_completion_line_is_recognised()
    {
        Assert.True(FFmpegProgressParser.IsCompletion("progress=end"));
        Assert.True(FFmpegProgressParser.IsCompletion("  progress=end  "));
        Assert.False(FFmpegProgressParser.IsCompletion("progress=continue"));
        Assert.False(FFmpegProgressParser.IsCompletion(null));
    }
}
