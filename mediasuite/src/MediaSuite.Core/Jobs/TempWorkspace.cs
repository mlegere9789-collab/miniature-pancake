namespace MediaSuite.Core.Jobs;

/// <summary>
/// A scratch folder owned by one job. Disposing it removes the folder and everything
/// left in it.
/// </summary>
public sealed class TempWorkspace : IDisposable
{
    private bool _disposed;

    internal TempWorkspace(string path) => Path = path;

    /// <summary>Full path to the folder. It exists from construction until disposal.</summary>
    public string Path { get; }

    /// <summary>Full path for a file inside the workspace.</summary>
    public string File(string fileName) => System.IO.Path.Combine(Path, fileName);

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;

        try
        {
            if (Directory.Exists(Path))
            {
                Directory.Delete(Path, recursive: true);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // A file still held open by a tool that has not fully exited must not fail the
            // job that already produced its output. PurgeStaleWorkspaces sweeps it later.
        }
    }
}
