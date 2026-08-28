using System.Globalization;
using MediaSuite.Core.Jobs;

namespace MediaSuite.Core.Engines;

/// <summary>Where FFmpeg reads the frames for a GIF job from.</summary>
/// <param name="Path">The input file, or the concat list when several images are joined.</param>
/// <param name="IsConcatList">
/// True when <paramref name="Path"/> is a concat-demuxer list rather than a media file.
/// </param>
public sealed record GifSource(string Path, bool IsConcatList = false);

/// <summary>One FFmpeg invocation within a GIF job.</summary>
/// <param name="Description">Stage text for the queue row, e.g. "Choosing colours".</param>
/// <param name="Arguments">The full argument list.</param>
public sealed record GifStep(string Description, IReadOnlyList<string> Arguments);

/// <summary>The invocations a GIF job runs, in order.</summary>
public sealed record GifPlan(IReadOnlyList<GifStep> Steps);

/// <summary>
/// Builds the FFmpeg commands behind the GIF tools. Pure, so the palette pipeline can be
/// checked argument by argument without FFmpeg installed.
/// </summary>
/// <remarks>
/// Writing a GIF well takes two passes. GIF holds at most 256 colours, and FFmpeg's
/// default is a fixed web-safe palette that turns most video into a dithered mess. So the
/// first pass reads the frames and writes a palette chosen for this clip, and the second
/// pass encodes against it. Both passes must apply exactly the same frame rate, scale and
/// trim, or the palette is built from frames the encode never sees.
/// </remarks>
public static class GifCommandBuilder
{
    /// <summary>Name of the palette written into the job's scratch folder.</summary>
    internal const string PaletteFileName = "palette.png";

    public static GifPlan Build(JobSpec spec, GifSource source, string outputPath, string workingDirectory)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ArgumentNullException.ThrowIfNull(source);

        var operation = spec.OperationId.ToLowerInvariant();

        return operation switch
        {
            "gif.to-mp4" => new GifPlan(new[]
            {
                new GifStep("Encoding", BuildToVideo(spec, source, outputPath)),
            }),
            "gif.to-apng" => new GifPlan(new[]
            {
                new GifStep("Encoding", BuildToApng(spec, source, outputPath)),
            }),
            _ when GifOperations.ProducesGif(operation) => BuildPalettePlan(spec, source, outputPath, workingDirectory),
            _ => throw new ArgumentException($"'{spec.OperationId}' is not a GIF operation.", nameof(spec)),
        };
    }

    private static GifPlan BuildPalettePlan(
        JobSpec spec,
        GifSource source,
        string outputPath,
        string workingDirectory)
    {
        var palettePath = System.IO.Path.Combine(workingDirectory, PaletteFileName);
        var frames = FrameFilters(spec);

        return new GifPlan(new[]
        {
            new GifStep("Choosing colours", BuildPaletteGen(spec, source, frames, palettePath)),
            new GifStep("Encoding", BuildPaletteUse(spec, source, frames, palettePath, outputPath)),
        });
    }

    private static IReadOnlyList<string> BuildPaletteGen(
        JobSpec spec,
        GifSource source,
        string frames,
        string palettePath)
    {
        var arguments = GlobalFlags();
        AppendTrimBeforeInput(arguments, spec);
        AppendInput(arguments, source);
        AppendTrimAfterInput(arguments, spec);

        arguments.Add("-vf");
        arguments.Add(
            $"{frames},palettegen=max_colors={ResolveColors(spec).ToString(CultureInfo.InvariantCulture)}"
            + ":stats_mode=full");

        // A palette is a single frame; without this FFmpeg would keep writing one per input frame.
        arguments.Add("-frames:v");
        arguments.Add("1");

        arguments.Add(palettePath);
        return arguments;
    }

    private static IReadOnlyList<string> BuildPaletteUse(
        JobSpec spec,
        GifSource source,
        string frames,
        string palettePath,
        string outputPath)
    {
        var arguments = GlobalFlags();
        AppendTrimBeforeInput(arguments, spec);
        AppendInput(arguments, source);

        // The palette is a second input, so this needs the filtergraph form rather than -vf.
        arguments.Add("-i");
        arguments.Add(palettePath);

        // After every input, not between them: -t sitting in front of an -i is an *input*
        // option, so it would cap how much of the palette is read rather than how long the
        // GIF runs for.
        AppendTrimAfterInput(arguments, spec);

        arguments.Add("-lavfi");
        arguments.Add($"{frames}[x];[x][1:v]paletteuse=dither={ResolveDither(spec)}");

        arguments.Add("-loop");
        arguments.Add(spec.GetBool("loopForever", true) ? "0" : "-1");

        arguments.Add(outputPath);
        return arguments;
    }

    private static IReadOnlyList<string> BuildToVideo(JobSpec spec, GifSource source, string outputPath)
    {
        var arguments = GlobalFlags();
        AppendInput(arguments, source);

        // H.264 cannot encode odd width or height, and GIFs very often have one. Rounding
        // down to the nearest even number loses at most a single row of pixels; without it
        // a perfectly ordinary GIF fails to convert at all.
        var filters = new List<string> { "scale=trunc(iw/2)*2:trunc(ih/2)*2:flags=lanczos" };
        var fps = spec.GetInt("fps", 0);

        if (fps > 0)
        {
            filters.Insert(0, $"fps={fps.ToString(CultureInfo.InvariantCulture)}");
        }

        arguments.Add("-vf");
        arguments.Add(string.Join(",", filters));

        var codec = FFmpegCommandBuilder.DefaultVideoCodec(
            System.IO.Path.GetExtension(outputPath).TrimStart('.').ToLowerInvariant());

        arguments.Add("-c:v");
        arguments.Add(codec);
        arguments.Add("-crf");
        arguments.Add(FFmpegCommandBuilder.ResolveCrf(spec, codec).ToString(CultureInfo.InvariantCulture));

        // Anything that is not a recent browser or editor needs 4:2:0 to play the result.
        arguments.Add("-pix_fmt");
        arguments.Add("yuv420p");

        // GIFs carry no sound, so say so rather than letting FFmpeg look for a stream.
        arguments.Add("-an");

        arguments.Add("-movflags");
        arguments.Add("+faststart");

        arguments.Add(outputPath);
        return arguments;
    }

    private static IReadOnlyList<string> BuildToApng(JobSpec spec, GifSource source, string outputPath)
    {
        var arguments = GlobalFlags();
        AppendInput(arguments, source);

        var fps = spec.GetInt("fps", 0);

        if (fps > 0)
        {
            arguments.Add("-vf");
            arguments.Add($"fps={fps.ToString(CultureInfo.InvariantCulture)}");
        }

        arguments.Add("-f");
        arguments.Add("apng");

        // APNG spells looping "plays", and 0 means forever.
        arguments.Add("-plays");
        arguments.Add(spec.GetBool("loopForever", true) ? "0" : "1");

        arguments.Add(outputPath);
        return arguments;
    }

    private static List<string> GlobalFlags() => new()
    {
        "-hide_banner",
        "-nostdin",
        "-loglevel", "error",
        "-progress", "pipe:1",
        "-nostats",
        "-y",
    };

    private static void AppendInput(List<string> arguments, GifSource source)
    {
        if (source.IsConcatList)
        {
            arguments.Add("-f");
            arguments.Add("concat");

            // The list holds absolute paths from wherever the user dragged the files in
            // from, which the demuxer refuses to read unless safety is turned off.
            arguments.Add("-safe");
            arguments.Add("0");
        }

        arguments.Add("-i");
        arguments.Add(source.Path);
    }

    private static void AppendTrimBeforeInput(List<string> arguments, JobSpec spec)
    {
        if (MediaTime.TryParse(spec.GetOption("start"), out var start) && start > TimeSpan.Zero)
        {
            arguments.Add("-ss");
            arguments.Add(MediaTime.Format(start));
        }
    }

    /// <summary>
    /// Appends the length to keep. Must come after every input: before an <c>-i</c> it
    /// would be read as an input option and limit that input instead of the output.
    /// </summary>
    private static void AppendTrimAfterInput(List<string> arguments, JobSpec spec)
    {
        if (FFmpegCommandBuilder.TryReadDuration(spec, out var duration) && duration > TimeSpan.Zero)
        {
            arguments.Add("-t");
            arguments.Add(MediaTime.Format(duration));
        }
    }

    /// <summary>
    /// The frame rate and size filters, shared verbatim by both passes so the palette
    /// describes the frames that actually get encoded.
    /// </summary>
    internal static string FrameFilters(JobSpec spec)
    {
        var filters = new List<string> { $"fps={ResolveFps(spec).ToString(CultureInfo.InvariantCulture)}" };

        var width = spec.GetInt("width", 0);

        if (width > 0)
        {
            // -1 keeps the aspect ratio; GIF has no even-dimension rule to satisfy.
            filters.Add($"scale={width.ToString(CultureInfo.InvariantCulture)}:-1:flags=lanczos");
        }

        return string.Join(",", filters);
    }

    /// <summary>
    /// Frame rate for the GIF. Compressing leans lower than converting, because dropping
    /// frames is the cheapest size saving a GIF has.
    /// </summary>
    internal static int ResolveFps(JobSpec spec)
    {
        var explicitFps = spec.GetInt("fps", 0);

        if (explicitFps > 0)
        {
            return Math.Clamp(explicitFps, 1, 50);
        }

        if (GifOperations.CombinesInputs(spec.OperationId))
        {
            // Frames hold for a fixed time in a slideshow, so the rate follows from that.
            // Encoding faster than the slideshow only duplicates frames and inflates the file.
            return Math.Clamp((int)Math.Round(1000d / ResolveFrameDuration(spec).TotalMilliseconds), 1, 50);
        }

        var compressing = string.Equals(spec.OperationId, "gif.compress", StringComparison.OrdinalIgnoreCase);

        return spec.Preset switch
        {
            QualityPreset.Quick => compressing ? 8 : 10,
            QualityPreset.Best => compressing ? 15 : 20,
            _ => compressing ? 12 : 15,
        };
    }

    /// <summary>Palette size. 256 is the most GIF can hold.</summary>
    internal static int ResolveColors(JobSpec spec)
    {
        var explicitColors = spec.GetInt("colors", 0);

        if (explicitColors > 0)
        {
            return Math.Clamp(explicitColors, 2, 256);
        }

        var compressing = string.Equals(spec.OperationId, "gif.compress", StringComparison.OrdinalIgnoreCase);

        return spec.Preset switch
        {
            QualityPreset.Quick => compressing ? 64 : 128,
            QualityPreset.Best => 256,
            _ => compressing ? 128 : 256,
        };
    }

    /// <summary>
    /// Dithering hides the banding a 256-colour palette causes, at the cost of file size —
    /// so the quick preset uses the ordered dither, which compresses far better than the
    /// error-diffusion ones.
    /// </summary>
    internal static string ResolveDither(JobSpec spec)
    {
        if (spec.GetOption("dither") is { Length: > 0 } chosen)
        {
            return chosen;
        }

        return spec.Preset switch
        {
            QualityPreset.Quick => "bayer:bayer_scale=3",
            _ => "sierra2_4a",
        };
    }

    /// <summary>How long each image is held for when a GIF is built from stills.</summary>
    internal static TimeSpan ResolveFrameDuration(JobSpec spec)
    {
        var milliseconds = spec.GetDouble("frameDurationMs", 0);

        return milliseconds > 0
            ? TimeSpan.FromMilliseconds(Math.Clamp(milliseconds, 20, 10_000))
            : TimeSpan.FromMilliseconds(100);
    }
}
