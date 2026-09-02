namespace MediaSuite.Core.Tests;

/// <summary>Scratch folder that deletes itself at the end of a test.</summary>
public sealed class TempDirectory : IDisposable
{
    public TempDirectory()
    {
        Path = System.IO.Path.Combine(
            System.IO.Path.GetTempPath(),
            "mediasuite-tests",
            Guid.NewGuid().ToString("N"));

        Directory.CreateDirectory(Path);
    }

    public string Path { get; }

    /// <summary>Creates an empty file (and any folders above it) and returns its full path.</summary>
    public string CreateFile(params string[] segments)
    {
        var full = System.IO.Path.Combine(new[] { Path }.Concat(segments).ToArray());
        var directory = System.IO.Path.GetDirectoryName(full);

        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        File.WriteAllText(full, string.Empty);
        return full;
    }

    public string Combine(params string[] segments) =>
        System.IO.Path.Combine(new[] { Path }.Concat(segments).ToArray());

    public void Dispose()
    {
        try
        {
            if (Directory.Exists(Path))
            {
                Directory.Delete(Path, recursive: true);
            }
        }
        catch (IOException)
        {
            // A locked file must not fail the test that already passed.
        }
    }
}
