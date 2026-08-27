using System.Globalization;
using MediaSuite.Core.Jobs;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Builds FFmpeg command lines. Pure, so every codec choice, preset mapping and trim
/// calculation can be checked without FFmpeg installed.
/// </summary>
public static class FFmpegCommandBuilder
{
    /// <summary>
    /// Builds the argument list.
    /// </summary>
    /// <remarks>
    /// FFmpeg's argument order is positional in a way that changes meaning: options before
    /// <c>-i</c> apply to reading the input, options after it apply to writing the output.
    /// Seeking is the clearest case — <c>-ss</c> before the input jumps straight to the
    /// keyframe, while after the input it decodes and discards everything up to that point.
    /// </remarks>
    public static IReadOnlyList<string> Build(JobSpec spec, string inputPath, string outputPath, MediaProbe probe)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ArgumentNullException.ThrowIfNull(probe);

        var operation = spec.OperationId.ToLowerInvariant();
        var extension = Path.GetExtension(outputPath).TrimStart('.').ToLowerInvariant();

        var arguments = new List<string>
        {
            "-hide_banner",

            // Without this FFmpeg competes with the app for stdin and can hang a batch.
            "-nostdin",
            "-loglevel", "error",

            // Machine-readable progress on stdout instead of the human status line.
            "-progress", "pipe:1",
            "-nostats",

            // The output path has already been resolved against the overwrite policy, so
            // whatever it points at is meant to be written.
            "-y",
        };

        AppendInputOptions(arguments, spec, operation);

        arguments.Add("-i");
        arguments.Add(inputPath);

        AppendOutputOptions(arguments, spec, operation, extension, probe);

        arguments.Add(outputPath);
        return arguments;
    }

    private static void AppendInputOptions(List<string> arguments, JobSpec spec, string operation)
    {
        if (operation != "video.trim")
        {
            return;
        }

        if (TryReadStart(spec, out var start) && start > TimeSpan.Zero)
        {
            arguments.Add("-ss");
            arguments.Add(MediaTime.Format(start));
        }
    }

    private static void AppendOutputOptions(
        List<string> arguments,
        JobSpec spec,
        string operation,
        string extension,
        MediaProbe probe)
    {
        switch (operation)
        {
            case "video.trim":
                AppendTrim(arguments, spec, extension);
                return;

            case "video.crop":
                AppendCrop(arguments, spec, extension);
                return;

            case "video.compress":
                AppendVideoEncode(arguments, spec, extension);
                AppendAudioEncode(arguments, spec, extension, forVideoContainer: true);
                AppendContainerExtras(arguments, extension);
                return;
        }

        if (FFmpegOperations.ProducesAudioOnly(operation))
        {
            arguments.Add("-vn");
            AppendAudioEncode(arguments, spec, extension, forVideoContainer: false);
            return;
        }

        // Whole-file video conversion.
        if (CanRemux(spec, extension, probe))
        {
            arguments.Add("-c");
            arguments.Add("copy");
        }
        else
        {
            AppendVideoEncode(arguments, spec, extension);
            AppendAudioEncode(arguments, spec, extension, forVideoContainer: true);
        }

        AppendContainerExtras(arguments, extension);
    }

    private static void AppendTrim(List<string> arguments, JobSpec spec, string extension)
    {
        // Duration rather than an end time: as an output option -t is measured from the
        // seek point, which is what "from here, this long" actually means.
        if (TryReadDuration(spec, out var duration) && duration > TimeSpan.Zero)
        {
            arguments.Add("-t");
            arguments.Add(MediaTime.Format(duration));
        }

        if (spec.GetBool("reencode", false))
        {
            AppendVideoEncode(arguments, spec, extension);
            AppendAudioEncode(arguments, spec, extension, forVideoContainer: true);
        }
        else
        {
            // Copying streams is instant and lossless, at the cost of starting on the
            // nearest keyframe before the requested point.
            arguments.Add("-c");
            arguments.Add("copy");
        }

        AppendContainerExtras(arguments, extension);
    }

    private static void AppendCrop(List<string> arguments, JobSpec spec, string extension)
    {
        var width = spec.GetInt("width", 0);
        var height = spec.GetInt("height", 0);
        var x = spec.GetInt("x", 0);
        var y = spec.GetInt("y", 0);

        if (width <= 0 || height <= 0)
        {
            throw new ArgumentException("Cropping needs a width and a height greater than zero.");
        }

        arguments.Add("-vf");
        arguments.Add(
            $"crop={width.ToString(CultureInfo.InvariantCulture)}:{height.ToString(CultureInfo.InvariantCulture)}:"
            + $"{x.ToString(CultureInfo.InvariantCulture)}:{y.ToString(CultureInfo.InvariantCulture)}");

        // Cropping rewrites every frame, so the video has to be encoded — but the audio is
        // untouched and copying it saves time and a generation of quality.
        AppendVideoEncode(arguments, spec, extension);
        arguments.Add("-c:a");
        arguments.Add("copy");

        AppendContainerExtras(arguments, extension);
    }

    private static void AppendVideoEncode(List<string> arguments, JobSpec spec, string extension)
    {
        var codec = spec.GetOption("videoCodec", DefaultVideoCodec(extension));

        arguments.Add("-c:v");
        arguments.Add(codec);

        var targetSize = spec.GetDouble("targetSizeMb", 0);
        var durationSeconds = spec.GetDouble("durationSeconds", 0);

        if (targetSize > 0 && durationSeconds > 0)
        {
            // Size targeting is a bitrate problem, not a quality one: work back from the
            // bytes the user asked for, leaving room for the audio track.
            var audioKbps = spec.GetInt("audioBitrateKbps", 128);
            var totalKbps = targetSize * 8 * 1024 / durationSeconds;
            var videoKbps = Math.Max(64, totalKbps - audioKbps);
            var bitrate = ((int)videoKbps).ToString(CultureInfo.InvariantCulture) + "k";

            arguments.Add("-b:v");
            arguments.Add(bitrate);
            arguments.Add("-maxrate");
            arguments.Add(bitrate);
            arguments.Add("-bufsize");
            arguments.Add(((int)videoKbps * 2).ToString(CultureInfo.InvariantCulture) + "k");
            return;
        }

        arguments.Add("-crf");
        arguments.Add(ResolveCrf(spec, codec).ToString(CultureInfo.InvariantCulture));

        if (SupportsSpeedPreset(codec))
        {
            arguments.Add("-preset");
            arguments.Add(spec.GetOption("speed", "medium"));
        }
    }

    private static void AppendAudioEncode(List<string> arguments, JobSpec spec, string extension, bool forVideoContainer)
    {
        var codec = spec.GetOption("audioCodec", DefaultAudioCodec(extension, forVideoContainer));

        arguments.Add("-c:a");
        arguments.Add(codec);

        if (codec is "copy" or "flac" or "pcm_s16le" or "pcm_s16be" or "pcm_s24le")
        {
            AppendPcmOptions(arguments, spec);
            return;
        }

        var bitrate = spec.GetInt("audioBitrateKbps", 0);
        if (bitrate > 0)
        {
            arguments.Add("-b:a");
            arguments.Add(bitrate.ToString(CultureInfo.InvariantCulture) + "k");
            return;
        }

        // MP3 and Vorbis do better on a variable-quality scale than a fixed bitrate.
        if (codec is "libmp3lame" or "libvorbis")
        {
            arguments.Add("-q:a");
            arguments.Add(VariableQualityFor(codec, spec.Preset).ToString(CultureInfo.InvariantCulture));
            return;
        }

        arguments.Add("-b:a");
        arguments.Add(DefaultBitrateKbps(spec.Preset).ToString(CultureInfo.InvariantCulture) + "k");
    }

    private static void AppendPcmOptions(List<string> arguments, JobSpec spec)
    {
        var sampleRate = spec.GetInt("sampleRate", 0);
        if (sampleRate > 0)
        {
            arguments.Add("-ar");
            arguments.Add(sampleRate.ToString(CultureInfo.InvariantCulture));
        }

        var channels = spec.GetInt("channels", 0);
        if (channels > 0)
        {
            arguments.Add("-ac");
            arguments.Add(channels.ToString(CultureInfo.InvariantCulture));
        }
    }

    private static void AppendContainerExtras(List<string> arguments, string extension)
    {
        if (extension is "mp4" or "m4v" or "mov" or "m4a")
        {
            // Moves the index to the front so the file can start playing before it has
            // finished downloading.
            arguments.Add("-movflags");
            arguments.Add("+faststart");
        }
    }

    /// <summary>
    /// True when the streams can simply be copied into the new container. Remuxing is
    /// near-instant and lossless, so it is worth checking for before re-encoding.
    /// </summary>
    internal static bool CanRemux(JobSpec spec, string extension, MediaProbe probe)
    {
        if (!spec.GetBool("remux", true) || !probe.HasVideo)
        {
            return false;
        }

        // An explicitly requested codec means the user wants an encode.
        if (spec.GetOption("videoCodec") is { Length: > 0 } || spec.GetOption("audioCodec") is { Length: > 0 })
        {
            return false;
        }

        var video = probe.VideoCodec?.ToLowerInvariant();
        var audio = probe.AudioCodec?.ToLowerInvariant();

        return extension switch
        {
            "mp4" or "m4v" or "mov" =>
                video is "h264" or "hevc" or "mpeg4" && audio is null or "aac" or "mp3",
            "mkv" => true,
            "webm" => video is "vp8" or "vp9" or "av1" && audio is null or "opus" or "vorbis",
            _ => false,
        };
    }

    internal static string DefaultVideoCodec(string extension) => extension switch
    {
        "webm" => "libvpx-vp9",
        _ => "libx264",
    };

    internal static string DefaultAudioCodec(string extension, bool forVideoContainer) => extension switch
    {
        "mp3" => "libmp3lame",
        "ogg" => "libvorbis",
        "opus" => "libopus",
        "flac" => "flac",
        "wav" => "pcm_s16le",
        "aiff" or "aif" => "pcm_s16be",
        "wma" => "wmav2",
        "aac" or "m4a" => "aac",
        "webm" => "libopus",
        _ => forVideoContainer ? "aac" : "aac",
    };

    /// <summary>CRF for a preset. The usable range differs per codec, so the table does too.</summary>
    internal static int ResolveCrf(JobSpec spec, string codec)
    {
        var explicitCrf = spec.GetInt("crf", -1);
        if (explicitCrf >= 0)
        {
            return explicitCrf;
        }

        return codec switch
        {
            "libvpx-vp9" => spec.Preset switch
            {
                QualityPreset.Quick => 36,
                QualityPreset.Best => 28,
                _ => 32,
            },
            "libx265" => spec.Preset switch
            {
                QualityPreset.Quick => 30,
                QualityPreset.Best => 22,
                _ => 26,
            },
            _ => spec.Preset switch
            {
                QualityPreset.Quick => 28,
                QualityPreset.Best => 18,
                _ => 23,
            },
        };
    }

    private static bool SupportsSpeedPreset(string codec) =>
        codec is "libx264" or "libx265";

    /// <summary>Codec-specific VBR quality scale, where lower is better for both.</summary>
    internal static int VariableQualityFor(string codec, QualityPreset preset) => codec switch
    {
        "libvorbis" => preset switch
        {
            // Vorbis runs the other way: -1 (worst) to 10 (best).
            QualityPreset.Quick => 3,
            QualityPreset.Best => 8,
            _ => 5,
        },
        _ => preset switch
        {
            QualityPreset.Quick => 5,
            QualityPreset.Best => 0,
            _ => 2,
        },
    };

    internal static int DefaultBitrateKbps(QualityPreset preset) => preset switch
    {
        QualityPreset.Quick => 128,
        QualityPreset.Best => 320,
        _ => 192,
    };

    private static bool TryReadStart(JobSpec spec, out TimeSpan start) =>
        MediaTime.TryParse(spec.GetOption("start"), out start);

    /// <summary>
    /// How much to keep, from an explicit duration or from the gap between start and end.
    /// </summary>
    internal static bool TryReadDuration(JobSpec spec, out TimeSpan duration)
    {
        if (MediaTime.TryParse(spec.GetOption("duration"), out duration))
        {
            return true;
        }

        if (MediaTime.TryParse(spec.GetOption("end"), out var end))
        {
            var start = MediaTime.TryParse(spec.GetOption("start"), out var parsedStart) ? parsedStart : TimeSpan.Zero;

            if (end > start)
            {
                duration = end - start;
                return true;
            }
        }

        duration = TimeSpan.Zero;
        return false;
    }
}
