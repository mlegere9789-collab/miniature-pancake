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
        IReadOnlyDictionary<string, string>? options = null,
        bool uploadToGoogleDrive = false,
        string? googleDriveFolderId = null)
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
            UploadToGoogleDrive = uploadToGoogleDrive,
            GoogleDriveFolderId = string.IsNullOrWhiteSpace(googleDriveFolderId) ? null : googleDriveFolderId,
        };

        var resolvedOptions = options ?? new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        // Computed once, from the whole original selection, before it gets split into one
        // spec per file below — see JobSpec.BatchRoot's own doc comment for why this can't
        // just be recomputed later from a single-file spec's own InputPaths.
        var batchRoot = target.PreserveFolderStructure ? OutputPathResolver.FindCommonRoot(inputPaths) : null;

        JobSpec SpecFor(IReadOnlyList<string> paths, int? batchIndex) => new()
        {
            OperationId = feature.OperationId,
            InputPaths = paths,
            Output = target,
            Preset = preset,
            Options = resolvedOptions,
            BatchRoot = batchRoot,
            BatchIndex = batchIndex,
        };

        if (OperationInputRules.CombinesInputs(feature.OperationId))
        {
            if (inputPaths.Count == 0)
            {
                return Array.Empty<QueuedJob>();
            }

            // The order the user added the files in is the order the frames play in, so it
            // is passed through untouched. This spec's own InputPaths already is the whole
            // batch, so its engine's own loop position is already a correct batch index --
            // BatchIndex stays null.
            return _queue.EnqueueRange(new[] { SpecFor(inputPaths.ToArray(), batchIndex: null) });
        }

        return _queue.EnqueueRange(
            inputPaths.Select((path, i) => SpecFor(new[] { path }, batchIndex: i + 1)));
    }
}
