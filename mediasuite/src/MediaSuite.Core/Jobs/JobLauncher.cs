using MediaSuite.Core.Features;
using MediaSuite.Core.Settings;

namespace MediaSuite.Core.Jobs;

/// <summary>
/// Turns "these files, this tool, these settings" into queued jobs.
/// </summary>
public sealed class JobLauncher
{
    private readonly JobQueueManager _queue;
    private readonly AppSettings _settings;

    public JobLauncher(JobQueueManager queue, AppSettings settings)
    {
        _queue = queue ?? throw new ArgumentNullException(nameof(queue));
        _settings = settings ?? throw new ArgumentNullException(nameof(settings));
    }

    /// <summary>
    /// Queues the work. One job per file, rather than a single job over the batch, so the
    /// queue can run them in parallel and the user can cancel one without losing the rest.
    /// </summary>
    public IReadOnlyList<QueuedJob> Launch(
        FeatureDescriptor feature,
        IReadOnlyList<string> inputPaths,
        string? outputFormat,
        QualityPreset preset,
        string? outputDirectory = null,
        IReadOnlyDictionary<string, string>? options = null)
    {
        ArgumentNullException.ThrowIfNull(feature);
        ArgumentNullException.ThrowIfNull(inputPaths);

        var target = new OutputTarget
        {
            Directory = string.IsNullOrWhiteSpace(outputDirectory)
                ? _settings.ResolveOutputDirectory()
                : outputDirectory,
            Format = string.IsNullOrWhiteSpace(outputFormat) ? null : outputFormat,
            PreserveFolderStructure = _settings.PreserveFolderStructure,
        };

        var specs = inputPaths.Select(path => new JobSpec
        {
            OperationId = feature.OperationId,
            InputPaths = new[] { path },
            Output = target,
            Preset = preset,
            Options = options ?? new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase),
        });

        return _queue.EnqueueRange(specs);
    }
}
