using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class FFmpegCommandBuilderTests
{
    private static readonly MediaProbe H264Mp4 = new(TimeSpan.FromMinutes(2), "h264", "aac", 1920, 1080);

    private static JobSpec Spec(
        string operationId,
        QualityPreset preset = QualityPreset.Balanced,
        params (string Key, string Value)[] options) => new()
    {
        OperationId = operationId,
        InputPaths = new[] { "in.mp4" },
        Output = new OutputTarget { Directory = "out" },
        Preset = preset,
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    private static IReadOnlyList<string> Build(
        JobSpec spec,
        string input = "in.mp4",
        string output = "out.mp4",
        MediaProbe? probe = null) =>
        FFmpegCommandBuilder.Build(spec, input, output, probe ?? MediaProbe.Unknown);

    private static string Command(
        JobSpec spec,
        string input = "in.mp4",
        string output = "out.mp4",
        MediaProbe? probe = null) =>
        string.Join(' ', Build(spec, input, output, probe));

    [Fact]
    public void Every_run_asks_for_machine_readable_progress_and_never_touches_stdin()
    {
        var command = Command(Spec("video.convert"), probe: H264Mp4);

        Assert.Contains("-progress pipe:1", command, StringComparison.Ordinal);
        Assert.Contains("-nostats", command, StringComparison.Ordinal);

        // Without -nostdin FFmpeg competes with the app for the console and can hang.
        Assert.Contains("-nostdin", command, StringComparison.Ordinal);
        Assert.Contains("-y", command, StringComparison.Ordinal);
    }

    [Fact]
    public void The_input_follows_minus_i_and_the_output_is_last()
    {
        var arguments = Build(Spec("video.convert"), "clip.mov", "clip.mp4", H264Mp4);
        var list = arguments.ToList();

        Assert.Equal("clip.mov", list[list.IndexOf("-i") + 1]);
        Assert.Equal("clip.mp4", list[^1]);
    }

    // --- Trimming ---------------------------------------------------------

    [Fact]
    public void Trimming_seeks_before_the_input_so_it_does_not_decode_what_it_discards()
    {
        var arguments = Build(Spec("video.trim", options: new[] { ("start", "00:00:30"), ("end", "00:01:00") }));
        var list = arguments.ToList();

        var seek = list.IndexOf("-ss");
        var input = list.IndexOf("-i");

        Assert.True(seek >= 0, "a trim has to seek");
        Assert.True(seek < input, "seeking after -i decodes and throws away everything up to the start point");
    }

    [Fact]
    public void A_trim_is_expressed_as_a_duration_from_the_seek_point()
    {
        var command = Command(Spec("video.trim", options: new[] { ("start", "30"), ("end", "90") }));

        Assert.Contains("-ss 00:00:30.000", command, StringComparison.Ordinal);
        Assert.Contains("-t 00:01:00.000", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_explicit_duration_beats_an_end_time()
    {
        var command = Command(Spec("video.trim", options: new[]
        {
            ("start", "10"), ("end", "90"), ("duration", "5"),
        }));

        Assert.Contains("-t 00:00:05.000", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_end_before_the_start_is_treated_as_no_duration_at_all()
    {
        var command = Command(Spec("video.trim", options: new[] { ("start", "60"), ("end", "30") }));

        Assert.DoesNotContain("-t ", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Trimming_copies_the_streams_by_default()
    {
        // Lossless and near-instant; the cost is starting at the nearest keyframe.
        var command = Command(Spec("video.trim", options: new[] { ("start", "5"), ("end", "10") }));

        Assert.Contains("-c copy", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-crf", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Trimming_can_re_encode_for_a_frame_accurate_cut()
    {
        var command = Command(Spec("video.trim", options: new[]
        {
            ("start", "5"), ("end", "10"), ("reencode", "true"),
        }));

        Assert.DoesNotContain("-c copy", command, StringComparison.Ordinal);
        Assert.Contains("-c:v libx264", command, StringComparison.Ordinal);
    }

    // --- Cropping ---------------------------------------------------------

    [Fact]
    public void Cropping_builds_the_filter_and_leaves_the_audio_alone()
    {
        var command = Command(Spec("video.crop", options: new[]
        {
            ("width", "640"), ("height", "480"), ("x", "10"), ("y", "20"),
        }));

        Assert.Contains("-vf crop=640:480:10:20", command, StringComparison.Ordinal);

        // The audio is untouched by a crop, so copying it saves time and a generation.
        Assert.Contains("-c:a copy", command, StringComparison.Ordinal);
        Assert.Contains("-c:v libx264", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Cropping_without_a_size_is_rejected_rather_than_producing_junk()
    {
        Assert.Throws<ArgumentException>(() => Command(Spec("video.crop")));
        Assert.Throws<ArgumentException>(() => Command(Spec("video.crop", options: new[] { ("width", "640") })));
    }

    // --- Remuxing ---------------------------------------------------------

    [Fact]
    public void Compatible_streams_are_copied_into_the_new_container()
    {
        var command = Command(Spec("video.mov-to-mp4"), "clip.mov", "clip.mp4", H264Mp4);

        Assert.Contains("-c copy", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-crf", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Streams_the_container_cannot_hold_are_re_encoded()
    {
        var vp9 = new MediaProbe(TimeSpan.FromMinutes(1), "vp9", "opus");

        var command = Command(Spec("video.convert.mp4"), "clip.webm", "clip.mp4", vp9);

        Assert.DoesNotContain("-c copy", command, StringComparison.Ordinal);
        Assert.Contains("-c:v libx264", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Asking_for_a_codec_means_an_encode_even_when_a_copy_would_fit()
    {
        var command = Command(
            Spec("video.convert", options: new[] { ("videoCodec", "libx265") }), "a.mp4", "b.mp4", H264Mp4);

        Assert.DoesNotContain("-c copy", command, StringComparison.Ordinal);
        Assert.Contains("-c:v libx265", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Remuxing_can_be_turned_off()
    {
        var command = Command(
            Spec("video.convert", options: new[] { ("remux", "false") }), "a.mov", "b.mp4", H264Mp4);

        Assert.DoesNotContain("-c copy", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_unprobed_file_is_re_encoded_rather_than_assumed_compatible()
    {
        var command = Command(Spec("video.convert"), "a.mov", "b.mp4");

        Assert.DoesNotContain("-c copy", command, StringComparison.Ordinal);
    }

    // --- Encoding settings ------------------------------------------------

    [Theory]
    [InlineData(QualityPreset.Quick, 28)]
    [InlineData(QualityPreset.Balanced, 23)]
    [InlineData(QualityPreset.Best, 18)]
    public void H264_quality_follows_the_preset(QualityPreset preset, int expected)
    {
        var command = Command(Spec("video.compress", preset), output: "out.mp4");

        Assert.Contains($"-crf {expected}", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Vp9_uses_its_own_quality_scale()
    {
        // CRF numbers are not comparable between codecs; 23 in VP9 is not 23 in H.264.
        var command = Command(Spec("video.convert", QualityPreset.Balanced), "a.mp4", "b.webm");

        Assert.Contains("-c:v libvpx-vp9", command, StringComparison.Ordinal);
        Assert.Contains("-crf 32", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_explicit_crf_overrides_the_preset()
    {
        var command = Command(Spec("video.compress", QualityPreset.Quick, ("crf", "15")), output: "out.mp4");

        Assert.Contains("-crf 15", command, StringComparison.Ordinal);
    }

    [Fact]
    public void A_speed_preset_is_only_passed_to_codecs_that_have_one()
    {
        Assert.Contains("-preset medium", Command(Spec("video.compress"), output: "out.mp4"), StringComparison.Ordinal);
        Assert.DoesNotContain("-preset ", Command(Spec("video.convert"), "a.mp4", "b.webm"), StringComparison.Ordinal);
    }

    [Fact]
    public void Mp4_output_moves_its_index_to_the_front_for_streaming()
    {
        Assert.Contains("-movflags +faststart", Command(Spec("video.compress"), output: "out.mp4"), StringComparison.Ordinal);
        Assert.DoesNotContain("-movflags", Command(Spec("video.compress"), output: "out.mkv"), StringComparison.Ordinal);
    }

    // --- Size targeting ---------------------------------------------------

    [Fact]
    public void A_size_target_becomes_a_bitrate_worked_back_from_the_duration()
    {
        // 10 MB over 100 s is 819.2 kbps all in; the audio's 128 leaves 691 for video.
        var command = Command(
            Spec("video.compress", options: new[] { ("targetSizeMb", "10"), ("durationSeconds", "100") }),
            output: "out.mp4");

        Assert.Contains("-b:v 691k", command, StringComparison.Ordinal);
        Assert.Contains("-maxrate 691k", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-crf", command, StringComparison.Ordinal);
    }

    [Fact]
    public void A_size_target_without_a_duration_falls_back_to_quality()
    {
        // Nothing to divide by, so guessing a bitrate would be worse than a good CRF.
        var command = Command(Spec("video.compress", options: new[] { ("targetSizeMb", "10") }), output: "out.mp4");

        Assert.Contains("-crf 23", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-b:v", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_impossibly_small_target_still_produces_a_usable_bitrate()
    {
        var command = Command(
            Spec("video.compress", options: new[] { ("targetSizeMb", "0.01"), ("durationSeconds", "600") }),
            output: "out.mp4");

        Assert.Contains("-b:v 64k", command, StringComparison.Ordinal);
    }

    // --- Audio ------------------------------------------------------------

    [Fact]
    public void Extracting_audio_drops_the_video_stream()
    {
        var command = Command(Spec("video.mp4-to-mp3"), "clip.mp4", "clip.mp3", H264Mp4);

        Assert.Contains("-vn", command, StringComparison.Ordinal);
        Assert.Contains("-c:a libmp3lame", command, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("mp3", "libmp3lame")]
    [InlineData("ogg", "libvorbis")]
    [InlineData("opus", "libopus")]
    [InlineData("flac", "flac")]
    [InlineData("wav", "pcm_s16le")]
    [InlineData("m4a", "aac")]
    [InlineData("aiff", "pcm_s16be")]
    public void Each_audio_container_gets_its_matching_encoder(string extension, string codec)
    {
        var command = Command(Spec("audio.convert"), "a.wav", $"out.{extension}");

        Assert.Contains($"-c:a {codec}", command, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(QualityPreset.Quick, 5)]
    [InlineData(QualityPreset.Balanced, 2)]
    [InlineData(QualityPreset.Best, 0)]
    public void Mp3_uses_variable_quality_where_lower_is_better(QualityPreset preset, int expected)
    {
        var command = Command(Spec("audio.convert.mp3", preset), "a.wav", "out.mp3");

        Assert.Contains($"-q:a {expected}", command, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(QualityPreset.Quick, 3)]
    [InlineData(QualityPreset.Best, 8)]
    public void Vorbis_quality_runs_the_other_way_and_the_mapping_reflects_that(QualityPreset preset, int expected)
    {
        var command = Command(Spec("audio.mp3-to-ogg", preset), "a.mp3", "out.ogg");

        Assert.Contains($"-q:a {expected}", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_explicit_bitrate_replaces_the_quality_scale()
    {
        var command = Command(
            Spec("audio.convert.mp3", options: new[] { ("audioBitrateKbps", "320") }), "a.wav", "out.mp3");

        Assert.Contains("-b:a 320k", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-q:a", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Wav_compression_works_by_sample_rate_and_channels_rather_than_bitrate()
    {
        var command = Command(
            Spec("audio.compress.wav", options: new[] { ("sampleRate", "22050"), ("channels", "1") }),
            "a.wav",
            "out.wav");

        Assert.Contains("-ar 22050", command, StringComparison.Ordinal);
        Assert.Contains("-ac 1", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-b:a", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Webm_pairs_vp9_with_opus()
    {
        var command = Command(Spec("video.convert"), "a.mp4", "b.webm");

        Assert.Contains("-c:v libvpx-vp9", command, StringComparison.Ordinal);
        Assert.Contains("-c:a libopus", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Numbers_are_written_the_same_way_regardless_of_the_machine_locale()
    {
        var previous = Thread.CurrentThread.CurrentCulture;
        Thread.CurrentThread.CurrentCulture = new System.Globalization.CultureInfo("de-DE");

        try
        {
            var command = Command(Spec("video.trim", options: new[] { ("start", "1.5"), ("end", "3.25") }));

            Assert.Contains("-ss 00:00:01.500", command, StringComparison.Ordinal);
            Assert.Contains("-t 00:00:01.750", command, StringComparison.Ordinal);
        }
        finally
        {
            Thread.CurrentThread.CurrentCulture = previous;
        }
    }
}
