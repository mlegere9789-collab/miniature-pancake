using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Shared plumbing for engines that drive a command-line tool: locating the binary,
/// running it, and turning a non-zero exit into a message the user can act on.
/// </summary>
public abstract class ExternalProcessEngine : IConversionEngine
{
    protected ExternalProcessEngine(IProcessRunner processRunner, ToolLocator toolLocator)
    {
        ProcessRunner = processRunner ?? throw new ArgumentNullException(nameof(processRunner));
        ToolLocator = toolLocator ?? throw new ArgumentNullException(nameof(toolLocator));
    }

    protected IProcessRunner ProcessRunner { get; }

    protected ToolLocator ToolLocator { get; }

    public abstract string Id { get; }

    public abstract string DisplayName { get; }

    public abstract IReadOnlyList<ExternalToolId> RequiredTools { get; }

    public abstract bool CanHandle(JobSpec spec);

    public abstract Task<JobResult> RunAsync(
        JobSpec spec,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken);

    /// <summary>Full path to a tool, or a failure explaining which one is missing.</summary>
    protected string RequireTool(ExternalToolId id)
    {
        var location = ToolLocator.Locate(id);

        if (location.Found)
        {
            return location.Path;
        }

        var descriptor = ToolManifest.Get(id);
        throw new ToolExecutionException(
            $"{descriptor.DisplayName} is not installed. Put it in tools\\{descriptor.FolderName}\\ "
            + $"or set its path in Settings — it is needed for: {descriptor.Purpose}");
    }

    /// <summary>Runs a tool and throws a readable error if it fails.</summary>
    /// <param name="outputPathToDeleteOnCancel">
    /// The real, final destination this specific call writes to directly — never a scratch
    /// file later moved/relocated into place, and never passed just because a path happens
    /// to be in scope. <see cref="ProcessRunner"/> already kills the tool on cancellation,
    /// but the tool can have written a truncated file up to that instant; without this, that
    /// truncated file is left sitting at the user's chosen output path with no indication
    /// it is incomplete, and a later run under <see cref="OverwritePolicy.Rename"/> would
    /// count it as "already there" and rename around it instead of ever cleaning it up.
    /// Left null for calls that only ever write to a workspace scratch path (cleaned up
    /// wholesale when the job's workspace is disposed) — passing the real output path from
    /// a call that does not actually target it would risk deleting a pre-existing file at
    /// that path under <see cref="OverwritePolicy.Overwrite"/> that this call never touched.
    /// </param>
    protected async Task RunToolAsync(
        string executable,
        IReadOnlyList<string> arguments,
        string toolName,
        CancellationToken cancellationToken,
        string? workingDirectory = null,
        string? outputPathToDeleteOnCancel = null)
    {
        ProcessResult result;

        try
        {
            result = await ProcessRunner.RunAsync(
                new ProcessRequest
                {
                    FileName = executable,
                    Arguments = arguments,
                    WorkingDirectory = workingDirectory,
                },
                cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            DeletePartialOutput(outputPathToDeleteOnCancel);
            throw;
        }

        if (!result.IsSuccess)
        {
            throw new ToolExecutionException($"{toolName} failed: {result.DescribeFailure()}", result.FullOutput());
        }
    }

    /// <summary>
    /// Best-effort delete of a file a killed tool was mid-write on. <see cref="File.Delete"/>
    /// is already a silent no-op when nothing is there, so callers that pass a path the
    /// cancelled step never actually reached (an earlier pass in a multi-step pipeline) need
    /// no guard of their own.
    /// </summary>
    protected static void DeletePartialOutput(string? outputPath)
    {
        if (string.IsNullOrEmpty(outputPath))
        {
            return;
        }

        try
        {
            File.Delete(outputPath);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Locked by another process, or permissions -- the job is already ending as
            // Canceled either way; failing cleanup is not worth turning that into a Failure.
        }
    }

    /// <summary>
    /// Folder for intermediates. The queue assigns one per job; the fallback only matters
    /// when an engine is driven directly, such as from a test.
    /// </summary>
    protected static string ResolveWorkingDirectory(JobSpec spec)
    {
        if (!string.IsNullOrWhiteSpace(spec.WorkingDirectory))
        {
            Directory.CreateDirectory(spec.WorkingDirectory);
            return spec.WorkingDirectory;
        }

        var fallback = Path.Combine(Path.GetTempPath(), "MediaSuite", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(fallback);
        return fallback;
    }
}
