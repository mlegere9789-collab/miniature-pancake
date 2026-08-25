namespace MediaSuite.Core.Tooling;

/// <summary>
/// Runs external tools. An interface so engines can be tested without the binaries
/// actually being installed.
/// </summary>
public interface IProcessRunner
{
    Task<ProcessResult> RunAsync(ProcessRequest request, CancellationToken cancellationToken);
}
