using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Jobs;

/// <summary>
/// The single seam between the job queue and the outside tools. Every backend —
/// FFmpeg, ImageMagick, Ghostscript, Pandoc, Real-ESRGAN — is a thin adapter behind
/// this interface, so the queue never learns engine-specific details and new formats
/// only ever mean a new implementation.
/// </summary>
public interface IConversionEngine
{
    /// <summary>Stable id, used in diagnostics ("ffmpeg", "imagemagick").</summary>
    string Id { get; }

    /// <summary>Name shown to the user when reporting a missing dependency.</summary>
    string DisplayName { get; }

    /// <summary>External binaries this engine needs before it can run anything.</summary>
    IReadOnlyList<ExternalToolId> RequiredTools { get; }

    /// <summary>True when this engine is willing to run the given job.</summary>
    bool CanHandle(JobSpec spec);

    /// <summary>
    /// Runs the job. Implementations report progress through <paramref name="progress"/>,
    /// honour <paramref name="cancellationToken"/> promptly (killing the child process if
    /// needed), and return a failed <see cref="JobResult"/> rather than throwing for
    /// expected problems such as a corrupt input file.
    /// </summary>
    Task<JobResult> RunAsync(JobSpec spec, IProgress<JobProgress> progress, CancellationToken cancellationToken);
}
