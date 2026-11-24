using System.Diagnostics;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ProcessRunnerTests : IDisposable
{
    private readonly TempDirectory _temp = new();
    private readonly ProcessRunner _runner = new();

    public void Dispose() => _temp.Dispose();

    private Task<ProcessResult> RunScriptAsync(
        string[] lines,
        Action<string>? onOutput = null,
        Action<string>? onError = null,
        int capturedLines = 60,
        CancellationToken cancellationToken = default)
    {
        var script = ScriptRunner.WriteScript(_temp, lines);
        var (fileName, arguments) = ScriptRunner.CommandFor(script);

        return _runner.RunAsync(
            new ProcessRequest
            {
                FileName = fileName,
                Arguments = arguments,
                OnStandardOutputLine = onOutput,
                OnStandardErrorLine = onError,
                CapturedLineLimit = capturedLines,
            },
            cancellationToken);
    }

    [Fact]
    public async Task Captures_standard_output_and_a_successful_exit_code()
    {
        var result = await RunScriptAsync(new[] { "echo hello from the tool" });

        Assert.True(result.IsSuccess);
        Assert.Equal(0, result.ExitCode);
        Assert.Contains("hello from the tool", result.StandardOutput, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Reports_a_non_zero_exit_code_as_a_failure()
    {
        var result = await RunScriptAsync(new[] { "exit 3" });

        Assert.False(result.IsSuccess);
        Assert.Equal(3, result.ExitCode);
    }

    [Fact]
    public async Task Captures_standard_error_separately()
    {
        var result = await RunScriptAsync(new[] { "echo something went wrong 1>&2" });

        Assert.Contains("something went wrong", result.StandardError, StringComparison.Ordinal);
        Assert.DoesNotContain("something went wrong", result.StandardOutput, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Streams_output_lines_as_they_arrive()
    {
        var seen = new List<string>();

        await RunScriptAsync(
            new[] { "echo first", "echo second" },
            onOutput: line =>
            {
                lock (seen)
                {
                    seen.Add(line);
                }
            });

        Assert.Contains(seen, line => line.Contains("first", StringComparison.Ordinal));
        Assert.Contains(seen, line => line.Contains("second", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Only_the_tail_of_a_chatty_tool_is_kept()
    {
        var lines = Enumerable.Range(1, 40).Select(i => $"echo line{i}").ToArray();

        var result = await RunScriptAsync(lines, capturedLines: 5);

        var captured = result.StandardOutput.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        Assert.Equal(5, captured.Length);
        Assert.Contains("line40", result.StandardOutput, StringComparison.Ordinal);
        Assert.DoesNotContain("line35", result.StandardOutput, StringComparison.Ordinal);
    }

    [Fact]
    public async Task DescribeFailure_surfaces_the_last_error_line()
    {
        var result = await RunScriptAsync(new[] { "echo unsupported codec 1>&2", "exit 1" });

        Assert.Contains("unsupported codec", result.DescribeFailure(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task DescribeFailure_falls_back_to_the_exit_code_when_the_tool_said_nothing()
    {
        var result = await RunScriptAsync(new[] { "exit 9" });

        Assert.Contains("9", result.DescribeFailure(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task Non_ascii_standard_output_is_decoded_as_UTF8_not_the_legacy_console_code_page()
    {
        // Left unset, .NET decodes a redirected stream using the console's legacy code
        // page, not UTF-8 -- FFmpeg and the other bundled tools write UTF-8 by default on
        // modern Windows builds, so an accented character (in an echoed input file's own
        // name, for instance) would otherwise come back as mojibake. "chcp 65001" switches
        // the console -- and, since cmd re-reads the rest of this very script line by
        // line, this script's own remaining commands too -- to UTF-8 first, so what cmd
        // actually writes to stdout here is genuine UTF-8 bytes, the same as a real tool
        // like FFmpeg already produces regardless of the console's own code page.
        var lines = OperatingSystem.IsWindows()
            ? new[] { "chcp 65001 > nul", "echo café" }
            : new[] { "echo café" };

        var result = await RunScriptAsync(lines);

        Assert.Contains("café", result.StandardOutput, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Non_ascii_failure_text_surfaces_correctly_through_DescribeFailure()
    {
        // The practically important path: ExternalProcessEngine.RunToolAsync puts exactly
        // this text -- the tool's own last stderr line -- into the exception message a
        // user actually sees for a failed job, e.g. a real FFmpeg error naming an accented
        // input file it could not open.
        var lines = OperatingSystem.IsWindows()
            ? new[] { "chcp 65001 > nul", "echo error opening café.mp4 1>&2", "exit 1" }
            : new[] { "echo error opening café.mp4 1>&2", "exit 1" };

        var result = await RunScriptAsync(lines);

        Assert.Contains("café.mp4", result.DescribeFailure(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task Cancelling_stops_waiting_on_a_long_running_tool()
    {
        using var cancellation = new CancellationTokenSource();
        cancellation.CancelAfter(TimeSpan.FromMilliseconds(250));

        var started = Stopwatch.StartNew();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => RunScriptAsync(new[] { ScriptRunner.SleepCommand(30) }, cancellationToken: cancellation.Token));

        started.Stop();

        // The point is that we neither wait out the 30 seconds nor leak the wait.
        Assert.True(
            started.Elapsed < TimeSpan.FromSeconds(15),
            $"cancellation took {started.Elapsed}, which suggests it waited for the tool");
    }

    [Fact]
    public async Task An_already_cancelled_token_never_starts_the_tool()
    {
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => RunScriptAsync(new[] { "echo should not run" }, cancellationToken: cancellation.Token));
    }

    [Fact]
    public async Task A_missing_executable_fails_with_the_path_that_could_not_be_started()
    {
        var missing = _temp.Combine("not-installed", "ffmpeg.exe");

        var error = await Assert.ThrowsAsync<ToolExecutionException>(() => _runner.RunAsync(
            new ProcessRequest { FileName = missing, Arguments = Array.Empty<string>() },
            CancellationToken.None));

        Assert.Contains(missing, error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Arguments_with_spaces_reach_the_tool_intact()
    {
        // Paths with spaces are the norm on Windows; ArgumentList has to handle the
        // quoting so engines never build a command string by hand.
        var script = ScriptRunner.WriteScript(_temp, OperatingSystem.IsWindows() ? "echo %1" : "echo \"$1\"");
        var (fileName, arguments) = ScriptRunner.CommandFor(script);

        var result = await _runner.RunAsync(
            new ProcessRequest
            {
                FileName = fileName,
                Arguments = arguments.Append(@"C:\My Photos\holiday shot.cr2").ToList(),
            },
            CancellationToken.None);

        Assert.Contains("holiday shot.cr2", result.StandardOutput, StringComparison.Ordinal);
    }
}
