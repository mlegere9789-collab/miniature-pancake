using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class TempWorkspaceTests : IDisposable
{
    private readonly TempDirectory _temp = new();

    public void Dispose() => _temp.Dispose();

    private DiskTempWorkspaceFactory CreateFactory() =>
        new(_temp.Combine("work"));

    [Fact]
    public void A_workspace_exists_on_creation_and_is_gone_after_disposal()
    {
        var factory = CreateFactory();
        string path;

        using (var workspace = factory.Create(Guid.NewGuid()))
        {
            path = workspace.Path;
            Assert.True(Directory.Exists(path));
        }

        Assert.False(Directory.Exists(path));
    }

    [Fact]
    public void Disposal_removes_the_files_left_inside()
    {
        var factory = CreateFactory();
        string path;

        using (var workspace = factory.Create(Guid.NewGuid()))
        {
            path = workspace.Path;
            File.WriteAllText(workspace.File("pass1.log"), "scratch");
            Directory.CreateDirectory(Path.Combine(path, "frames"));
            File.WriteAllText(Path.Combine(path, "frames", "0001.png"), "scratch");
        }

        Assert.False(Directory.Exists(path));
    }

    [Fact]
    public void Two_jobs_never_share_a_workspace()
    {
        var factory = CreateFactory();

        using var first = factory.Create(Guid.NewGuid());
        using var second = factory.Create(Guid.NewGuid());

        Assert.NotEqual(first.Path, second.Path);
    }

    [Fact]
    public void Disposing_twice_is_harmless()
    {
        var factory = CreateFactory();
        var workspace = factory.Create(Guid.NewGuid());

        workspace.Dispose();
        workspace.Dispose();
    }

    [Fact]
    public void Purge_removes_folders_left_behind_by_an_earlier_session()
    {
        var factory = CreateFactory();
        var stale = factory.Create(Guid.NewGuid()).Path;
        File.WriteAllText(Path.Combine(stale, "half-written.mp4"), "junk");

        // Pretend the crash happened a day ago.
        Directory.SetLastWriteTimeUtc(stale, DateTime.UtcNow.AddDays(-1));

        var removed = factory.PurgeStaleWorkspaces(TimeSpan.FromHours(1));

        Assert.Equal(1, removed);
        Assert.False(Directory.Exists(stale));
    }

    [Fact]
    public void Purge_leaves_workspaces_that_a_running_job_may_still_own()
    {
        var factory = CreateFactory();
        using var fresh = factory.Create(Guid.NewGuid());

        var removed = factory.PurgeStaleWorkspaces(TimeSpan.FromHours(1));

        Assert.Equal(0, removed);
        Assert.True(Directory.Exists(fresh.Path));
    }

    [Fact]
    public void Purge_on_a_root_that_does_not_exist_yet_is_a_no_op()
    {
        var factory = new DiskTempWorkspaceFactory(_temp.Combine("never-created"));

        Assert.Equal(0, factory.PurgeStaleWorkspaces(TimeSpan.Zero));
    }

    [Fact]
    public void A_blank_root_is_rejected()
    {
        Assert.Throws<ArgumentException>(() => new DiskTempWorkspaceFactory("  "));
    }
}
