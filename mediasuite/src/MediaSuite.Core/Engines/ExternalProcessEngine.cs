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
    protected async Task RunToolAsync(
        string executable,
        IReadOnlyList<string> arguments,
        string toolName,
        CancellationToken cancellationToken,
        string? workingDirectory = null)
    {
        var result = await ProcessRunner.RunAsync(
            new ProcessRequest
            {
                FileName = executable,
                Arguments = arguments,
                WorkingDirectory = workingDirectory,
            },
            cancellationToken).ConfigureAwait(false);

        if (!result.IsSuccess)
        {
            throw new ToolExecutionException($"{toolName} failed: {result.DescribeFailure()}");
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
