using System.Globalization;

namespace MediaSuite.Core.Jobs;

/// <summary>
/// Everything an engine needs to run one unit of work: the inputs, where the output
/// goes, the quality preset, and a bag of engine-specific advanced options.
/// </summary>
/// <remarks>
/// Advanced options are stored as strings rather than a typed object per operation so
/// the queue, the settings store and the presets can all round-trip a job without
/// knowing which engine will pick it up. Engines read them through the typed
/// <c>GetOption</c> helpers below.
/// </remarks>
public sealed record JobSpec
{
    /// <summary>
    /// Operation identifier in <c>family.operation</c> form — "image.convert",
    /// "video.compress", "pdf.merge", "upscale.photo". Engines advertise the ids they handle.
    /// </summary>
    public required string OperationId { get; init; }

    /// <summary>Input files, in the order the user added them (matters for pdf.merge).</summary>
    public required IReadOnlyList<string> InputPaths { get; init; }

    /// <summary>Where results are written.</summary>
    public required OutputTarget Output { get; init; }

    /// <summary>Quality preset; engines fall back to their own defaults for anything the preset doesn't cover.</summary>
    public QualityPreset Preset { get; init; } = QualityPreset.Balanced;

    /// <summary>Advanced options, keyed case-insensitively (e.g. "crf" =&gt; "18").</summary>
    public IReadOnlyDictionary<string, string> Options { get; init; } =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

    /// <summary>Human readable label for the queue row.</summary>
    public string DisplayName =>
        InputPaths.Count switch
        {
            0 => OperationId,
            1 => Path.GetFileName(InputPaths[0]),
            _ => $"{Path.GetFileName(InputPaths[0])} + {InputPaths.Count - 1} more",
        };

    public string? GetOption(string key) =>
        Options.TryGetValue(key, out var value) ? value : null;

    public string GetOption(string key, string fallback) =>
        GetOption(key) is { Length: > 0 } value ? value : fallback;

    public int GetInt(string key, int fallback) =>
        int.TryParse(GetOption(key), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value)
            ? value
            : fallback;

    public double GetDouble(string key, double fallback) =>
        double.TryParse(GetOption(key), NumberStyles.Float, CultureInfo.InvariantCulture, out var value)
            ? value
            : fallback;

    public bool GetBool(string key, bool fallback) =>
        bool.TryParse(GetOption(key), out var value) ? value : fallback;

    /// <summary>Returns a copy of this spec with one advanced option set or replaced.</summary>
    public JobSpec WithOption(string key, string value)
    {
        var options = new Dictionary<string, string>(Options, StringComparer.OrdinalIgnoreCase)
        {
            [key] = value,
        };
        return this with { Options = options };
    }
}
