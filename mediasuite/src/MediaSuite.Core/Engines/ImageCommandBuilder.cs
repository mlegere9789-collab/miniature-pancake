using System.Globalization;
using MediaSuite.Core.Jobs;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Turns a job into an ImageMagick command line. Pure: no disk, no process, so every
/// operation and preset can be checked without ImageMagick installed.
/// </summary>
public static class ImageCommandBuilder
{
    /// <summary>
    /// Builds the argument list for <c>magick</c>.
    /// </summary>
    /// <remarks>
    /// Argument order matters to ImageMagick and is not cosmetic: settings that affect how
    /// the input is *read* (density, background for vectors) must come before the file
    /// name, and operations apply in sequence to the image already loaded. The output path
    /// is always last.
    /// </remarks>
    public static IReadOnlyList<string> Build(JobSpec spec, string inputPath, string outputPath)
    {
        ArgumentNullException.ThrowIfNull(spec);

        var operation = spec.OperationId.ToLowerInvariant();
        var targetExtension = Path.GetExtension(outputPath).TrimStart('.').ToLowerInvariant();

        var arguments = new List<string>();

        AppendReadSettings(arguments, spec, inputPath);
        arguments.Add(inputPath);

        // EXIF orientation applied up front, so a phone photo is never silently rotated
        // by a later crop or resize.
        arguments.Add("-auto-orient");

        AppendOperation(arguments, spec, operation);
        AppendEncoding(arguments, spec, operation, targetExtension);

        if (spec.GetBool("strip", false))
        {
            arguments.Add("-strip");
        }

        arguments.Add(outputPath);
        return arguments;
    }

    /// <summary>Quality number a preset means for a given output format.</summary>
    public static int QualityFor(QualityPreset preset, string extension) => extension switch
    {
        // WebP holds up better at lower numbers than JPEG does.
        "webp" or "avif" => preset switch
        {
            QualityPreset.Quick => 65,
            QualityPreset.Best => 92,
            _ => 80,
        },
        _ => preset switch
        {
            QualityPreset.Quick => 70,
            QualityPreset.Best => 95,
            _ => 85,
        },
    };

    private static void AppendReadSettings(List<string> arguments, JobSpec spec, string inputPath)
    {
        if (!ImageOperations.IsVectorSource(inputPath))
        {
            return;
        }

        // A vector has no inherent pixel size: without a density it rasterises at a
        // default 72 dpi and looks soft.
        arguments.Add("-density");
        arguments.Add(spec.GetInt("dpi", 300).ToString(CultureInfo.InvariantCulture));
        arguments.Add("-background");
        arguments.Add(spec.GetOption("background", "none"));
    }

    private static void AppendOperation(List<string> arguments, JobSpec spec, string operation)
    {
        switch (operation)
        {
            case "image.resize":
                AppendResize(arguments, spec);
                break;

            case "image.enlarge":
                AppendEnlarge(arguments, spec);
                break;

            case "image.crop":
                AppendCrop(arguments, spec);
                break;

            case "image.rotate":
                AppendRotate(arguments, spec);
                break;

            case "image.flip":
                AppendFlip(arguments, spec);
                break;

            default:
                // Plain conversion or compression: no geometry change.
                break;
        }
    }

    private static void AppendResize(List<string> arguments, JobSpec spec)
    {
        var geometry = ResizeGeometry(spec);

        if (geometry is null)
        {
            return;
        }

        if (spec.GetOption("filter") is { Length: > 0 } filter)
        {
            arguments.Add("-filter");
            arguments.Add(filter);
        }

        arguments.Add("-resize");
        arguments.Add(geometry);
    }

    private static void AppendEnlarge(List<string> arguments, JobSpec spec)
    {
        // Lanczos is the sharpest of the general-purpose filters for upscaling; the AI
        // upscaler is a separate feature entirely.
        arguments.Add("-filter");
        arguments.Add(spec.GetOption("filter", "Lanczos"));

        var scale = spec.GetDouble("scale", 2);
        var percent = (scale * 100).ToString("0.##", CultureInfo.InvariantCulture);

        arguments.Add("-resize");
        arguments.Add($"{percent}%");
    }

    private static void AppendCrop(List<string> arguments, JobSpec spec)
    {
        var width = spec.GetInt("width", 0);
        var height = spec.GetInt("height", 0);
        var x = spec.GetInt("x", 0);
        var y = spec.GetInt("y", 0);

        if (width <= 0 || height <= 0)
        {
            throw new ArgumentException("Cropping needs a width and a height greater than zero.");
        }

        arguments.Add("-crop");
        arguments.Add($"{width}x{height}+{x}+{y}");

        // Without +repage the file keeps the original canvas size in its metadata and
        // some viewers show the uncropped image.
        arguments.Add("+repage");
    }

    private static void AppendRotate(List<string> arguments, JobSpec spec)
    {
        var angle = spec.GetDouble("angle", 90);

        if (Math.Abs(angle % 90) > double.Epsilon)
        {
            // An off-square rotation exposes corners that need filling.
            arguments.Add("-background");
            arguments.Add(spec.GetOption("background", "white"));
        }

        arguments.Add("-rotate");
        arguments.Add(angle.ToString("0.##", CultureInfo.InvariantCulture));
    }

    private static void AppendFlip(List<string> arguments, JobSpec spec)
    {
        switch (spec.GetOption("direction", "horizontal").ToLowerInvariant())
        {
            case "vertical":
                arguments.Add("-flip");
                break;

            case "both":
                arguments.Add("-flop");
                arguments.Add("-flip");
                break;

            default:
                arguments.Add("-flop");
                break;
        }
    }

    private static void AppendEncoding(List<string> arguments, JobSpec spec, string operation, string extension)
    {
        var isCompression = operation.StartsWith("image.compress", StringComparison.OrdinalIgnoreCase);

        switch (extension)
        {
            case "jpg" or "jpeg" or "jfif":
                arguments.Add("-quality");
                arguments.Add(ResolveQuality(spec, extension).ToString(CultureInfo.InvariantCulture));

                if (spec.GetBool("progressive", isCompression))
                {
                    arguments.Add("-interlace");
                    arguments.Add("Plane");
                }

                if (spec.GetOption("subsampling") is { Length: > 0 } subsampling)
                {
                    arguments.Add("-sampling-factor");
                    arguments.Add(subsampling);
                }

                break;

            case "png":
                arguments.Add("-define");
                arguments.Add($"png:compression-level={spec.GetInt("pngCompression", 9)}");

                // Palette quantisation is the only big win available for PNG, and it is
                // lossy, so it stays opt-in.
                var colors = spec.GetInt("colors", 0);
                if (colors is > 1 and <= 256)
                {
                    arguments.Add("-colors");
                    arguments.Add(colors.ToString(CultureInfo.InvariantCulture));
                }

                break;

            case "webp" or "avif" or "heic" or "heif":
                arguments.Add("-quality");
                arguments.Add(ResolveQuality(spec, extension).ToString(CultureInfo.InvariantCulture));

                if (spec.GetBool("lossless", false))
                {
                    arguments.Add("-define");
                    arguments.Add($"{extension}:lossless=true");
                }

                break;

            case "tiff" or "tif":
                arguments.Add("-compress");
                arguments.Add(spec.GetOption("tiffCompression", "LZW"));
                break;

            default:
                // Formats with nothing worth tuning (BMP, PPM, ICO, TGA…).
                break;
        }
    }

    private static int ResolveQuality(JobSpec spec, string extension)
    {
        var quality = spec.GetInt("quality", 0);

        return quality is > 0 and <= 100
            ? quality
            : QualityFor(spec.Preset, extension);
    }

    /// <summary>
    /// Builds the ImageMagick geometry string for a resize, or null when the job asked
    /// for no change.
    /// </summary>
    public static string? ResizeGeometry(JobSpec spec)
    {
        var percent = spec.GetDouble("percent", 0);
        if (percent > 0)
        {
            return $"{percent.ToString("0.##", CultureInfo.InvariantCulture)}%";
        }

        var width = spec.GetInt("width", 0);
        var height = spec.GetInt("height", 0);
        var longestEdge = spec.GetInt("longestEdge", 0);

        if (longestEdge > 0)
        {
            // WxH with both set to the same value fits inside that square, which is
            // exactly "longest edge" behaviour once the aspect ratio is kept.
            return $"{longestEdge}x{longestEdge}{Modifiers(spec)}";
        }

        if (width <= 0 && height <= 0)
        {
            return null;
        }

        var geometry = (width > 0, height > 0) switch
        {
            (true, true) => $"{width}x{height}",
            (true, false) => $"{width}",
            _ => $"x{height}",
        };

        return geometry + Modifiers(spec);
    }

    private static string Modifiers(JobSpec spec)
    {
        // "!" ignores the aspect ratio, ">" only ever shrinks. They can be combined but
        // rarely usefully, so aspect wins.
        if (!spec.GetBool("keepAspect", true))
        {
            return "!";
        }

        return spec.GetBool("onlyShrink", false) ? ">" : string.Empty;
    }
}
