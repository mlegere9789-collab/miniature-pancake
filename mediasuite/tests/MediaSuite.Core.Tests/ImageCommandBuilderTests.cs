using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ImageCommandBuilderTests
{
    private static JobSpec Spec(
        string operationId = "image.convert",
        QualityPreset preset = QualityPreset.Balanced,
        params (string Key, string Value)[] options) => new()
    {
        OperationId = operationId,
        InputPaths = new[] { "in.jpg" },
        Output = new OutputTarget { Directory = "out", Format = "png" },
        Preset = preset,
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    private static string Command(JobSpec spec, string input = "in.jpg", string output = "out.png") =>
        string.Join(' ', ImageCommandBuilder.Build(spec, input, output));

    [Fact]
    public void The_input_comes_first_and_the_output_last()
    {
        var arguments = ImageCommandBuilder.Build(Spec(), "photo.jpg", "photo.png");

        Assert.Equal("photo.jpg", arguments[0]);
        Assert.Equal("photo.png", arguments[^1]);
    }

    [Fact]
    public void Orientation_is_applied_before_anything_else_touches_the_pixels()
    {
        var arguments = ImageCommandBuilder.Build(Spec("image.crop", options: new[]
        {
            ("width", "100"), ("height", "100"),
        }), "photo.jpg", "photo.png");

        var autoOrient = arguments.ToList().IndexOf("-auto-orient");
        var crop = arguments.ToList().IndexOf("-crop");

        Assert.True(autoOrient >= 0, "EXIF orientation should always be applied");
        Assert.True(autoOrient < crop, "orientation has to happen before the crop, or the crop is wrong");
    }

    [Theory]
    [InlineData(QualityPreset.Quick, 70)]
    [InlineData(QualityPreset.Balanced, 85)]
    [InlineData(QualityPreset.Best, 95)]
    public void Jpeg_quality_follows_the_preset(QualityPreset preset, int expected)
    {
        var command = Command(Spec(preset: preset), output: "out.jpg");

        Assert.Contains($"-quality {expected}", command, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(QualityPreset.Quick, 65)]
    [InlineData(QualityPreset.Balanced, 80)]
    [InlineData(QualityPreset.Best, 92)]
    public void Webp_uses_its_own_scale_because_it_holds_up_better_than_jpeg(QualityPreset preset, int expected)
    {
        var command = Command(Spec(preset: preset), output: "out.webp");

        Assert.Contains($"-quality {expected}", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_explicit_quality_overrides_the_preset()
    {
        var command = Command(Spec(preset: QualityPreset.Quick, options: new[] { ("quality", "97") }), output: "out.jpg");

        Assert.Contains("-quality 97", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-quality 70", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_out_of_range_quality_falls_back_to_the_preset()
    {
        var command = Command(Spec(preset: QualityPreset.Best, options: new[] { ("quality", "500") }), output: "out.jpg");

        Assert.Contains("-quality 95", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Png_gets_a_compression_level_rather_than_a_quality()
    {
        var command = Command(Spec(), output: "out.png");

        Assert.Contains("png:compression-level=9", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-quality", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Png_palette_quantisation_stays_opt_in_because_it_is_lossy()
    {
        Assert.DoesNotContain("-colors", Command(Spec("image.compress.png"), output: "out.png"), StringComparison.Ordinal);
        Assert.Contains(
            "-colors 64",
            Command(Spec("image.compress.png", options: new[] { ("colors", "64") }), output: "out.png"),
            StringComparison.Ordinal);
    }

    [Fact]
    public void Compressing_a_jpeg_makes_it_progressive_by_default()
    {
        Assert.Contains("-interlace Plane", Command(Spec("image.compress.jpeg"), output: "out.jpg"), StringComparison.Ordinal);
        Assert.DoesNotContain("-interlace", Command(Spec("image.convert"), output: "out.jpg"), StringComparison.Ordinal);
    }

    [Fact]
    public void Tiff_picks_a_compression_scheme()
    {
        Assert.Contains("-compress LZW", Command(Spec(), output: "out.tiff"), StringComparison.Ordinal);
    }

    [Fact]
    public void Metadata_is_kept_unless_the_job_asks_to_strip_it()
    {
        Assert.DoesNotContain("-strip", Command(Spec()), StringComparison.Ordinal);
        Assert.Contains("-strip", Command(Spec(options: new[] { ("strip", "true") })), StringComparison.Ordinal);
    }

    [Fact]
    public void A_vector_source_sets_its_render_density_before_the_file_is_read()
    {
        var arguments = ImageCommandBuilder.Build(
            Spec("image.svg-convert", options: new[] { ("dpi", "600") }), "logo.svg", "logo.png");

        var density = arguments.ToList().IndexOf("-density");
        var input = arguments.ToList().IndexOf("logo.svg");

        Assert.True(density >= 0, "an SVG needs a density or it rasterises soft");
        Assert.True(density < input, "density only counts if it is set before the file is read");
        Assert.Contains("600", arguments);
    }

    [Fact]
    public void A_raster_source_gets_no_density()
    {
        Assert.DoesNotContain("-density", Command(Spec()), StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("width", "1920", "-resize 1920")]
    [InlineData("height", "1080", "-resize x1080")]
    [InlineData("percent", "50", "-resize 50%")]
    [InlineData("longestEdge", "2048", "-resize 2048x2048")]
    public void Resize_understands_each_way_of_asking(string key, string value, string expected)
    {
        var command = Command(Spec("image.resize", options: new[] { (key, value) }));

        Assert.Contains(expected, command, StringComparison.Ordinal);
    }

    [Fact]
    public void Resize_with_both_dimensions_keeps_the_aspect_ratio_by_default()
    {
        var command = Command(Spec("image.resize", options: new[] { ("width", "800"), ("height", "600") }));

        Assert.Contains("-resize 800x600", command, StringComparison.Ordinal);
        Assert.DoesNotContain("800x600!", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Turning_off_the_aspect_lock_forces_the_exact_size()
    {
        var command = Command(Spec("image.resize", options: new[]
        {
            ("width", "800"), ("height", "600"), ("keepAspect", "false"),
        }));

        Assert.Contains("-resize 800x600!", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Only_shrink_never_enlarges_a_smaller_image()
    {
        var command = Command(Spec("image.resize", options: new[]
        {
            ("longestEdge", "1000"), ("onlyShrink", "true"),
        }));

        Assert.Contains("-resize 1000x1000>", command, StringComparison.Ordinal);
    }

    [Fact]
    public void A_resize_with_no_dimensions_asks_for_no_resize_at_all()
    {
        Assert.Null(ImageCommandBuilder.ResizeGeometry(Spec("image.resize")));
        Assert.DoesNotContain("-resize", Command(Spec("image.resize")), StringComparison.Ordinal);
    }

    [Fact]
    public void Enlarging_scales_by_a_factor_with_a_sharp_filter()
    {
        var command = Command(Spec("image.enlarge", options: new[] { ("scale", "4") }));

        Assert.Contains("-filter Lanczos", command, StringComparison.Ordinal);
        Assert.Contains("-resize 400%", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Cropping_repages_so_viewers_do_not_show_the_original_canvas()
    {
        var command = Command(Spec("image.crop", options: new[]
        {
            ("width", "640"), ("height", "480"), ("x", "10"), ("y", "20"),
        }));

        Assert.Contains("-crop 640x480+10+20", command, StringComparison.Ordinal);
        Assert.Contains("+repage", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Cropping_without_a_size_is_rejected_rather_than_producing_junk()
    {
        Assert.Throws<ArgumentException>(() => Command(Spec("image.crop")));
        Assert.Throws<ArgumentException>(() => Command(Spec("image.crop", options: new[] { ("width", "100") })));
    }

    [Fact]
    public void Square_rotations_need_no_background_fill()
    {
        var command = Command(Spec("image.rotate", options: new[] { ("angle", "90") }));

        Assert.Contains("-rotate 90", command, StringComparison.Ordinal);
        Assert.DoesNotContain("-background", command, StringComparison.Ordinal);
    }

    [Fact]
    public void An_angled_rotation_fills_the_exposed_corners()
    {
        var command = Command(Spec("image.rotate", options: new[] { ("angle", "12.5"), ("background", "black") }));

        Assert.Contains("-background black", command, StringComparison.Ordinal);
        Assert.Contains("-rotate 12.5", command, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("horizontal", "-flop")]
    [InlineData("vertical", "-flip")]
    public void Flip_maps_the_direction_to_the_right_operator(string direction, string expected)
    {
        var command = Command(Spec("image.flip", options: new[] { ("direction", direction) }));

        Assert.Contains(expected, command, StringComparison.Ordinal);
    }

    [Fact]
    public void Flipping_both_ways_applies_both_operators()
    {
        var command = Command(Spec("image.flip", options: new[] { ("direction", "both") }));

        Assert.Contains("-flop", command, StringComparison.Ordinal);
        Assert.Contains("-flip", command, StringComparison.Ordinal);
    }

    [Fact]
    public void Numbers_are_written_the_same_way_regardless_of_the_machine_locale()
    {
        // A comma decimal separator would produce "-rotate 12,5", which ImageMagick reads
        // as a different value entirely.
        var previous = Thread.CurrentThread.CurrentCulture;
        Thread.CurrentThread.CurrentCulture = new System.Globalization.CultureInfo("de-DE");

        try
        {
            var command = Command(Spec("image.rotate", options: new[] { ("angle", "12.5") }));
            Assert.Contains("-rotate 12.5", command, StringComparison.Ordinal);
        }
        finally
        {
            Thread.CurrentThread.CurrentCulture = previous;
        }
    }
}
