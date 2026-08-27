using MediaSuite.Core.Engines;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class FFprobeReaderTests
{
    private const string SampleJson = """
    {
      "streams": [
        { "index": 0, "codec_name": "h264", "codec_type": "video", "width": 1920, "height": 1080 },
        { "index": 1, "codec_name": "aac", "codec_type": "audio", "channels": 2 }
      ],
      "format": { "format_name": "mov,mp4,m4a", "duration": "128.457000", "size": "20971520" }
    }
    """;

    [Fact]
    public void A_normal_probe_yields_duration_codecs_and_size()
    {
        var probe = FFprobeReader.Parse(SampleJson);

        Assert.Equal(TimeSpan.FromSeconds(128.457), probe.Duration);
        Assert.Equal("h264", probe.VideoCodec);
        Assert.Equal("aac", probe.AudioCodec);
        Assert.Equal(1920, probe.Width);
        Assert.Equal(1080, probe.Height);
        Assert.True(probe.HasVideo);
        Assert.True(probe.HasAudio);
    }

    [Fact]
    public void An_audio_only_file_reports_no_video()
    {
        var probe = FFprobeReader.Parse("""
        {
          "streams": [ { "codec_name": "mp3", "codec_type": "audio" } ],
          "format": { "duration": "61.2" }
        }
        """);

        Assert.False(probe.HasVideo);
        Assert.True(probe.HasAudio);
        Assert.Equal("mp3", probe.AudioCodec);
        Assert.Null(probe.Width);
    }

    [Fact]
    public void A_duration_the_container_does_not_carry_is_left_unknown()
    {
        var probe = FFprobeReader.Parse("""
        { "streams": [ { "codec_name": "h264", "codec_type": "video" } ], "format": { "duration": "N/A" } }
        """);

        Assert.Null(probe.Duration);
        Assert.Equal("h264", probe.VideoCodec);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData(null)]
    [InlineData("not json at all")]
    [InlineData("{ \"streams\": ")]
    [InlineData("{}")]
    public void Unusable_output_degrades_to_unknown_rather_than_throwing(string? json)
    {
        var probe = FFprobeReader.Parse(json);

        Assert.Null(probe.Duration);
        Assert.False(probe.HasVideo);
        Assert.False(probe.HasAudio);
    }

    [Fact]
    public void The_probe_asks_for_json_and_names_the_file_last()
    {
        var arguments = FFprobeReader.BuildArguments(@"C:\clips\holiday.mp4");

        Assert.Contains("json", arguments);
        Assert.Contains("-show_format", arguments);
        Assert.Contains("-show_streams", arguments);
        Assert.Equal(@"C:\clips\holiday.mp4", arguments[^1]);
    }

    [Fact]
    public async Task A_probe_that_fails_does_not_fail_the_job()
    {
        // A file FFprobe dislikes still gets handed to FFmpeg, which gives a better error.
        var runner = new FakeProcessRunner(_ => new ProcessResult(1, string.Empty, "moov atom not found", TimeSpan.Zero));

        var probe = await new FFprobeReader(runner).ReadAsync("ffprobe", "broken.mp4", CancellationToken.None);

        Assert.Equal(MediaProbe.Unknown, probe);
    }

    [Fact]
    public async Task A_successful_probe_is_parsed_from_the_tool_output()
    {
        var runner = new FakeProcessRunner(_ => new ProcessResult(0, SampleJson, string.Empty, TimeSpan.Zero));

        var probe = await new FFprobeReader(runner).ReadAsync("ffprobe", "holiday.mp4", CancellationToken.None);

        Assert.Equal("h264", probe.VideoCodec);
        Assert.Equal(TimeSpan.FromSeconds(128.457), probe.Duration);
    }
}
