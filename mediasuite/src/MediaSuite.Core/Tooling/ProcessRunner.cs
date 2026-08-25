using System.ComponentModel;
using System.Diagnostics;

namespace MediaSuite.Core.Tooling;

/// <summary>
/// Starts a tool, streams its output, and kills it if the job is canceled.
/// </summary>
public sealed class ProcessRunner : IProcessRunner
{
    public async Task<ProcessResult> RunAsync(ProcessRequest request, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();

        var startInfo = new ProcessStartInfo(request.FileName)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            RedirectStandardInput = false,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        foreach (var argument in request.Arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        if (!string.IsNullOrWhiteSpace(request.WorkingDirectory))
        {
            startInfo.WorkingDirectory = request.WorkingDirectory;
        }

        var standardOutput = new BoundedLineBuffer(request.CapturedLineLimit);
        var standardError = new BoundedLineBuffer(request.CapturedLineLimit);

        using var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };

        process.OutputDataReceived += (_, e) =>
        {
            if (e.Data is null)
            {
                return;
            }

            standardOutput.Add(e.Data);
            request.OnStandardOutputLine?.Invoke(e.Data);
        };

        process.ErrorDataReceived += (_, e) =>
        {
            if (e.Data is null)
            {
                return;
            }

            standardError.Add(e.Data);
            request.OnStandardErrorLine?.Invoke(e.Data);
        };

        var started = Stopwatch.GetTimestamp();

        try
        {
            process.Start();
        }
        catch (Win32Exception ex)
        {
            // Missing binary, wrong architecture, no execute permission.
            throw new ToolExecutionException(
                $"Could not start '{request.FileName}': {ex.Message}", ex);
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        try
        {
            await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // WaitForExitAsync only stops waiting; the tool would carry on writing the
            // output file, so the whole tree has to go.
            TryKill(process);
            throw;
        }

        // The async wait can return before the output callbacks have drained. The
        // parameterless overload returns immediately for an exited process but does wait
        // for the redirected streams to finish, which is what we need before reading them.
        process.WaitForExit();

        return new ProcessResult(
            process.ExitCode,
            standardOutput.ToString(),
            standardError.ToString(),
            Stopwatch.GetElapsedTime(started));
    }

    private static void TryKill(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch (Exception ex) when (ex is InvalidOperationException or NotSupportedException or Win32Exception)
        {
            // Already gone, or the OS will not let us; either way there is nothing else to do.
        }
    }
}
