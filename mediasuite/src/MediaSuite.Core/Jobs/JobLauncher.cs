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
    /// Tools that merge their inputs (see <see cref="OperationInputRules.CombinesInputs"/>)
    /// are the exception and get a single job over the whole selection.
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

        var resolvedOptions = options ?? new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        JobSpec SpecFor(IReadOnlyList<string> paths) => new()
        {
            OperationId = feature.OperationId,
            InputPaths = paths,
            Output = target,
            Preset = preset,
            Options = resolvedOptions,
        };

        if (OperationInputRules.CombinesInputs(feature.OperationId))
        {
            if (inputPaths.Count == 0)
            {
                return Array.Empty<QueuedJob>();
            }

            // The order the user added the files in is the order the frames play in, so it
            // is passed through untouched.
            return _queue.EnqueueRange(new[] { SpecFor(inputPaths.ToArray()) });
        }

        return _queue.EnqueueRange(inputPaths.Select(path => SpecFor(new[] { path })));
    }
}
