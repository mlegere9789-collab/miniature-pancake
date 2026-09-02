namespace MediaSuite.Core.Tests;

/// <summary>
/// Builds a tiny shell script and the command line that runs it.
/// </summary>
/// <remarks>
/// Tests drive real processes through a script file rather than passing a command
/// inline: <c>cmd /c</c> has its own quote-stripping rules that inline commands with
/// redirects trip over, and a script sidesteps them entirely on both platforms.
/// </remarks>
public static class ScriptRunner
{
    public static string WriteScript(TempDirectory temp, params string[] lines)
    {
        var isWindows = OperatingSystem.IsWindows();
        var path = temp.Combine(isWindows ? "script.cmd" : "script.sh");

        var body = isWindows
            ? string.Join(Environment.NewLine, new[] { "@echo off" }.Concat(lines))
            : string.Join('\n', new[] { "#!/bin/sh" }.Concat(lines));

        File.WriteAllText(path, body + (isWindows ? Environment.NewLine : "\n"));

        if (!isWindows)
        {
            File.SetUnixFileMode(
                path,
                UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        }

        return path;
    }

    public static (string FileName, IReadOnlyList<string> Arguments) CommandFor(string scriptPath) =>
        OperatingSystem.IsWindows()
            ? (Environment.GetEnvironmentVariable("ComSpec") ?? @"C:\Windows\System32\cmd.exe",
                new[] { "/c", scriptPath })
            : ("/bin/sh", new[] { scriptPath });

    /// <summary>A command that keeps running for roughly the given number of seconds.</summary>
    public static string SleepCommand(int seconds) =>
        OperatingSystem.IsWindows()
            ? $"ping -n {seconds + 1} 127.0.0.1 > nul"
            : $"sleep {seconds}";
}
