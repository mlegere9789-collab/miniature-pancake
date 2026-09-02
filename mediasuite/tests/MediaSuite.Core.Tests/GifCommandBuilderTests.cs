using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class GifCommandBuilderTests
{
    private const string Work = "work";

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

    private static GifPlan Plan(
        JobSpec spec,
        string input = "in.mp4",
        string output = "out.gif",
        bool concat = false) =>
        GifCommandBuilder.Build(spec, new GifSource(input, concat), output, Work);

    private static string Command(GifStep step) => string.Join(' ', step.Arguments);

    /// <summary>The single filter string a pass hands to FFmpeg.</summary>
    private static string FilterArgument(GifStep step, string flag)
    {
        var index = step.Arguments.ToList().IndexOf(flag);
        Assert.True(index >= 0, $"expected {flag} in: {Command(step)}");
        return step.Arguments[index + 1];
    }

    // --- The palette pipeline ---------------------------------------------

    [Fact]
    public void Writing_a_gif_takes_a_palette_pass_and_then_an_encode_pass()
    {
        var plan = Plan(Spec("gif.from-video"));

        Assert.Equal(2, plan.Steps.Count);
        Assert.Contains("palettegen", Command(plan.Steps[0]), StringComparison.Ordinal);
        Assert.Contains("paletteuse", Command(plan.Steps[1]), StringComparison.Ordinal);
    }

    [Fact]
    public void Both_passes_see_exactly_the_same_frames()
    {
        // If the palette were built from a different frame rate or size than the encode
        // uses, it would describe colours that never reach the output. This is the whole
        // reason the two passes exist, so it is the invariant worth pinning.
        var spec = Spec("gif.from-video", QualityPreset.Best, ("fps", "12"), ("width", "480"));
        var plan = Plan(spec);

        var frames = GifCommandBuilder.FrameFilters(spec);

        Assert.StartsWith(frames + ",palettegen", FilterArgument(plan.Steps[0], "-vf"), StringComparison.Ordinal);
        Assert.StartsWith(frames + "[x];", FilterArgument(plan.Steps[1], "-lavfi"), StringComparison.Ordinal);
    }

    [Fact]
    public void The_palette_lands_in_the_job_scratch_folder_and_is_a_single_frame()
    {
        var plan = Plan(Spec("gif.mp4-to-gif"));
        var palettePass = plan.Steps[0];

        Assert.Equal(Path.Combine(Work, "palette.png"), palettePass.Arguments[^1]);

        // Without this FFmpeg writes one palette per input frame.
        Assert.Contains("-frames:v 1", Command(palettePass), StringComparison.Ordinal);
    }

    [Fact]
    public void The_encode_pass_reads_the_palette_as_a_second_input()
    {
        var plan = Plan(Spec("gif.mp4-to-gif"));
        var encode = plan.Steps[1].Arguments.ToList();

        var inputs = encode.Select((argument, index) => (argument, index))
            .Where(pair => pair.argument == "-i")
            .Select(pair => encode[pair.index + 1])
            .ToList();

        Assert.Equal(new[] { "in.mp4", Path.Combine(Work, "palette.png") }, inputs);

        // [1:v] is the palette; getting the index wrong silently uses the video as one.
        Assert.Contains("[x][1:v]paletteuse", Command(plan.Steps[1]), StringComparison.Ordinal);
    }

    [Fact]
    public void The_gif_is_the_last_argument_of_the_encode_pass()
    {
        var plan = Plan(Spec("gif.webm-to-gif"), output: "party.gif");

        Assert.Equal("party.gif", plan.Steps[1].Arguments[^1]);
    }

    [Fact]
    public void Every_pass_asks_for_machine_readable_progress_and_never_touches_stdin()
    {
        foreach (var step in Plan(Spec("gif.from-video")).Steps)
        {
            var command = Command(step);
            Assert.Contains("-progress pipe:1", command, StringComparison.Ordinal);
            Assert.Contains("-nostats", command, StringComparison.Ordinal);
            Assert.Contains("-nostdin", command, StringComparison.Ordinal);
        }
    }

    // --- Palette size, frame rate and dithering ---------------------------

    [Theory]
    [InlineData(QualityPreset.Quick, 128)]
    [InlineData(QualityPreset.Balanced, 256)]
    [InlineData(QualityPreset.Best, 256)]
    public void The_preset_decides_how_many_colours_a_conversion_keeps(QualityPreset preset, int expected)
    {
        Assert.Equal(expected, GifCommandBuilder.ResolveColors(Spec("gif.from-video", preset)));
    }

    [Fact]
    public void Compressing_starts_from_fewer_colours_and_frames_than_converting()
    {
        var compress = Spec("gif.compress", QualityPreset.Quick);
        var convert = Spec("gif.from-video", QualityPreset.Quick);

        Assert.True(GifCommandBuilder.ResolveColors(compress) < GifCommandBuilder.ResolveColors(convert));
        Assert.True(GifCommandBuilder.ResolveFps(compress) < GifCommandBuilder.ResolveFps(convert));
    }

    [Fact]
    public void A_chosen_colour_count_wins_over_the_preset_and_cannot_exceed_what_gif_holds()
    {
        Assert.Equal(64, GifCommandBuilder.ResolveColors(Spec("gif.from-video", QualityPreset.Best, ("colors", "64"))));

        // GIF is an 8-bit format; asking for more is a request that cannot be honoured.
        Assert.Equal(256, GifCommandBuilder.ResolveColors(Spec("gif.from-video", QualityPreset.Balanced, ("colors", "4096"))));
        Assert.Equal(2, GifCommandBuilder.ResolveColors(Spec("gif.from-video", QualityPreset.Balanced, ("colors", "1"))));
    }

    [Fact]
    public void A_chosen_frame_rate_wins_over_the_preset_and_is_kept_sane()
    {
        Assert.Equal(24, GifCommandBuilder.ResolveFps(Spec("gif.from-video", QualityPreset.Balanced, ("fps", "24"))));

        // 900 frames a second is not a GIF anyone can play.
        Assert.Equal(50, GifCommandBuilder.ResolveFps(Spec("gif.from-video", QualityPreset.Balanced, ("fps", "900"))));

        // Nonsense falls back to the preset rather than producing a one-frame animation.
        Assert.Equal(15, GifCommandBuilder.ResolveFps(Spec("gif.from-video", QualityPreset.Balanced, ("fps", "0"))));
        Assert.Equal(15, GifCommandBuilder.ResolveFps(Spec("gif.from-video", QualityPreset.Balanced, ("fps", "lots"))));
    }

    [Fact]
    public void The_quick_preset_dithers_with_the_pattern_that_compresses_best()
    {
        // Error-diffusion dithers add noise that GIF's run-length compression cannot pack,
        // so the smallest-file preset uses the ordered one instead.
        Assert.StartsWith("bayer", GifCommandBuilder.ResolveDither(Spec("gif.from-video", QualityPreset.Quick)), StringComparison.Ordinal);
        Assert.Equal("sierra2_4a", GifCommandBuilder.ResolveDither(Spec("gif.from-video", QualityPreset.Best)));
        Assert.Equal("none", GifCommandBuilder.ResolveDither(Spec("gif.from-video", QualityPreset.Balanced, ("dither", "none"))));
    }

    [Fact]
    public void Resizing_keeps_the_aspect_ratio_and_uses_a_good_scaler()
    {
        var filters = GifCommandBuilder.FrameFilters(Spec("gif.from-video", QualityPreset.Balanced, ("width", "320")));

        Assert.Contains("scale=320:-1:flags=lanczos", filters, StringComparison.Ordinal);
    }

    [Fact]
    public void No_width_means_no_scale_filter_at_all()
    {
        Assert.DoesNotContain("scale", GifCommandBuilder.FrameFilters(Spec("gif.from-video")), StringComparison.Ordinal);
    }

    [Fact]
    public void A_gif_loops_forever_unless_the_user_says_otherwise()
    {
        Assert.Contains("-loop 0", Command(Plan(Spec("gif.from-video")).Steps[1]), StringComparison.Ordinal);

        var once = Plan(Spec("gif.from-video", QualityPreset.Balanced, ("loopForever", "false"))).Steps[1];
        Assert.Contains("-loop -1", Command(once), StringComparison.Ordinal);
    }

    // --- Trimming a clip down to a GIF ------------------------------------

    [Fact]
    public void Trimming_seeks_before_the_input_on_both_passes()
    {
        var spec = Spec("gif.from-video", options: new[] { ("start", "00:00:10"), ("duration", "3") });

        foreach (var step in Plan(spec).Steps)
        {
            var arguments = step.Arguments.ToList();
            var seek = arguments.IndexOf("-ss");
            var input = arguments.IndexOf("-i");

            Assert.True(seek >= 0, $"no seek in: {Command(step)}");

            // After -i, FFmpeg decodes and throws away everything up to the start point.
            Assert.True(seek < input, $"seek must precede the input in: {Command(step)}");

            Assert.Equal("00:00:03.000", arguments[arguments.IndexOf("-t") + 1]);
        }
    }

    [Fact]
    public void The_length_to_keep_comes_after_every_input_not_between_them()
    {
        // -t in front of an -i is an *input* option. Between the clip and the palette it
        // would cap how much of the palette is read — a one-frame PNG — rather than how
        // long the GIF runs for, and the trim would quietly do nothing.
        var spec = Spec("gif.from-video", options: new[] { ("duration", "3") });
        var encode = Plan(spec).Steps[1].Arguments.ToList();

        Assert.True(encode.IndexOf("-t") > encode.LastIndexOf("-i"), string.Join(' ', encode));
    }

    [Fact]
    public void An_end_time_becomes_a_duration_measured_from_the_start()
    {
        var spec = Spec("gif.from-video", options: new[] { ("start", "5"), ("end", "9") });
        var arguments = Plan(spec).Steps[1].Arguments.ToList();

        Assert.Equal("00:00:04.000", arguments[arguments.IndexOf("-t") + 1]);
    }

    // --- GIF from separate images -----------------------------------------

    [Fact]
    public void A_slideshow_reads_a_concat_list_rather_than_a_media_file()
    {
        var plan = Plan(Spec("gif.from-images"), input: "frames.txt", concat: true);

        foreach (var step in plan.Steps)
        {
            var arguments = step.Arguments.ToList();

            Assert.Contains("-f concat", Command(step), StringComparison.Ordinal);

            // The list holds absolute paths, which the demuxer refuses without this.
            Assert.Contains("-safe 0", Command(step), StringComparison.Ordinal);
            Assert.True(arguments.IndexOf("-f") < arguments.IndexOf("-i"));
        }
    }

    [Fact]
    public void A_slideshow_encodes_at_the_rate_its_frames_are_held_for()
    {
        // Encoding faster than the slideshow plays only duplicates frames and inflates the
        // file; slower would drop images the user chose.
        Assert.Equal(4, GifCommandBuilder.ResolveFps(Spec("gif.maker", QualityPreset.Balanced, ("frameDurationMs", "250"))));
        Assert.Equal(10, GifCommandBuilder.ResolveFps(Spec("gif.from-images")));
    }

    [Fact]
    public void An_explicit_frame_rate_still_wins_for_a_slideshow()
    {
        Assert.Equal(20, GifCommandBuilder.ResolveFps(
            Spec("gif.maker", options: new[] { ("frameDurationMs", "250"), ("fps", "20") })));
    }

    [Fact]
    public void Frame_duration_defaults_to_a_tenth_of_a_second_and_is_kept_in_range()
    {
        Assert.Equal(TimeSpan.FromMilliseconds(100), GifCommandBuilder.ResolveFrameDuration(Spec("gif.maker")));
        Assert.Equal(
            TimeSpan.FromMilliseconds(20),
            GifCommandBuilder.ResolveFrameDuration(Spec("gif.maker", QualityPreset.Balanced, ("frameDurationMs", "0.5"))));
        Assert.Equal(
            TimeSpan.FromMilliseconds(10_000),
            GifCommandBuilder.ResolveFrameDuration(Spec("gif.maker", QualityPreset.Balanced, ("frameDurationMs", "60000"))));
    }

    // --- Leaving GIF behind ------------------------------------------------

    [Fact]
    public void Turning_a_gif_into_a_video_rounds_odd_dimensions_down_to_even()
    {
        // H.264 cannot encode an odd width or height, and GIFs very often have one, so
        // without this a perfectly ordinary GIF simply fails to convert.
        var plan = Plan(Spec("gif.to-mp4"), input: "loop.gif", output: "loop.mp4");
        var step = Assert.Single(plan.Steps);

        Assert.Contains("scale=trunc(iw/2)*2:trunc(ih/2)*2", Command(step), StringComparison.Ordinal);
        Assert.Contains("-pix_fmt yuv420p", Command(step), StringComparison.Ordinal);
    }

    [Fact]
    public void A_gif_has_no_sound_so_the_video_is_told_not_to_look_for_any()
    {
        var step = Assert.Single(Plan(Spec("gif.to-mp4"), "loop.gif", "loop.mp4").Steps);

        Assert.Contains("-an", step.Arguments);
        Assert.Contains("+faststart", Command(step), StringComparison.Ordinal);
        Assert.Equal("loop.mp4", step.Arguments[^1]);
    }

    [Theory]
    [InlineData(QualityPreset.Quick, "28")]
    [InlineData(QualityPreset.Balanced, "23")]
    [InlineData(QualityPreset.Best, "18")]
    public void The_video_quality_follows_the_same_preset_table_as_the_video_module(
        QualityPreset preset,
        string expectedCrf)
    {
        var step = Assert.Single(Plan(Spec("gif.to-mp4", preset), "loop.gif", "loop.mp4").Steps);
        var arguments = step.Arguments.ToList();

        Assert.Equal("libx264", arguments[arguments.IndexOf("-c:v") + 1]);
        Assert.Equal(expectedCrf, arguments[arguments.IndexOf("-crf") + 1]);
    }

    [Fact]
    public void Animated_png_spells_looping_plays()
    {
        var step = Assert.Single(Plan(Spec("gif.to-apng"), "loop.gif", "loop.apng").Steps);

        Assert.Contains("-f apng", Command(step), StringComparison.Ordinal);
        Assert.Contains("-plays 0", Command(step), StringComparison.Ordinal);
        Assert.Equal("loop.apng", step.Arguments[^1]);
    }

    [Fact]
    public void Apng_to_gif_goes_through_the_palette_pipeline_like_any_other_source()
    {
        var plan = Plan(Spec("gif.apng-to-gif"), "loop.apng", "loop.gif");

        Assert.Equal(2, plan.Steps.Count);
        Assert.Contains("palettegen", Command(plan.Steps[0]), StringComparison.Ordinal);
    }

    [Fact]
    public void An_operation_from_another_module_is_refused_rather_than_guessed_at()
    {
        Assert.Throws<ArgumentException>(() => Plan(Spec("video.convert")));
    }

    [Fact]
    public void Every_gif_operation_the_catalogue_lists_produces_a_plan()
    {
        foreach (var operation in GifOperations.All)
        {
            var plan = Plan(Spec(operation), "in.gif", "out." + GifOperations.FixedFormatFor(operation));

            Assert.NotEmpty(plan.Steps);
            Assert.All(plan.Steps, step => Assert.NotEmpty(step.Arguments));
        }
    }
}
