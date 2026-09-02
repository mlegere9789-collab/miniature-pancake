using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ToolLocatorTests
{
    [Fact]
    public void Finds_a_tool_in_its_manifest_folder()
    {
        using var temp = new TempDirectory();
        var expected = temp.CreateFile("ffmpeg", "ffmpeg.exe");

        var locator = new ToolLocator(new[] { temp.Path }, pathVariable: string.Empty);
        var location = locator.Locate(ExternalToolId.FFmpeg);

        Assert.True(location.Found);
        Assert.Equal(expected, location.Path);
        Assert.Equal(ToolSource.Bundled, location.Source);
    }

    [Fact]
    public void Finds_a_tool_inside_a_bin_subfolder()
    {
        using var temp = new TempDirectory();
        var expected = temp.CreateFile("qpdf", "bin", "qpdf.exe");

        var locator = new ToolLocator(new[] { temp.Path }, pathVariable: string.Empty);

        Assert.Equal(expected, locator.Locate(ExternalToolId.QPdf).Path);
    }

    [Fact]
    public void Reports_missing_tools_rather_than_throwing()
    {
        using var temp = new TempDirectory();

        var locator = new ToolLocator(new[] { temp.Path }, pathVariable: string.Empty);
        var location = locator.Locate(ExternalToolId.Ghostscript);

        Assert.False(location.Found);
        Assert.Null(location.Path);
        Assert.Equal(ToolSource.NotFound, location.Source);
    }

    [Fact]
    public void An_explicit_override_beats_the_bundled_copy()
    {
        using var temp = new TempDirectory();
        temp.CreateFile("ffmpeg", "ffmpeg.exe");
        var overridePath = temp.CreateFile("elsewhere", "my-ffmpeg.exe");

        var locator = new ToolLocator(
            new[] { temp.Path },
            new Dictionary<ExternalToolId, string> { [ExternalToolId.FFmpeg] = overridePath },
            pathVariable: string.Empty);

        var location = locator.Locate(ExternalToolId.FFmpeg);

        Assert.Equal(overridePath, location.Path);
        Assert.Equal(ToolSource.Override, location.Source);
    }

    [Fact]
    public void An_override_pointing_at_nothing_falls_back_to_the_bundled_copy()
    {
        using var temp = new TempDirectory();
        var bundled = temp.CreateFile("ffmpeg", "ffmpeg.exe");

        var locator = new ToolLocator(
            new[] { temp.Path },
            new Dictionary<ExternalToolId, string> { [ExternalToolId.FFmpeg] = temp.Combine("gone.exe") },
            pathVariable: string.Empty);

        var location = locator.Locate(ExternalToolId.FFmpeg);

        Assert.Equal(bundled, location.Path);
        Assert.Equal(ToolSource.Bundled, location.Source);
    }

    [Fact]
    public void Earlier_search_roots_win()
    {
        using var first = new TempDirectory();
        using var second = new TempDirectory();

        var preferred = first.CreateFile("imagemagick", "magick.exe");
        second.CreateFile("imagemagick", "magick.exe");

        var locator = new ToolLocator(new[] { first.Path, second.Path }, pathVariable: string.Empty);

        Assert.Equal(preferred, locator.Locate(ExternalToolId.ImageMagick).Path);
    }

    [Fact]
    public void Falls_back_to_the_system_path()
    {
        using var temp = new TempDirectory();
        using var onPath = new TempDirectory();
        var expected = onPath.CreateFile("pandoc.exe");

        var locator = new ToolLocator(new[] { temp.Path }, pathVariable: onPath.Path);
        var location = locator.Locate(ExternalToolId.Pandoc);

        Assert.Equal(expected, location.Path);
        Assert.Equal(ToolSource.SystemPath, location.Source);
    }

    [Fact]
    public void Refresh_picks_up_a_tool_installed_after_the_first_lookup()
    {
        using var temp = new TempDirectory();
        var locator = new ToolLocator(new[] { temp.Path }, pathVariable: string.Empty);

        Assert.False(locator.Locate(ExternalToolId.SevenZip).Found);

        temp.CreateFile("7zip", "7z.exe");
        Assert.False(locator.Locate(ExternalToolId.SevenZip).Found);

        locator.Refresh();
        Assert.True(locator.Locate(ExternalToolId.SevenZip).Found);
    }

    [Fact]
    public void MissingRequiredTools_lists_only_the_required_ones()
    {
        using var temp = new TempDirectory();
        var locator = new ToolLocator(new[] { temp.Path }, pathVariable: string.Empty);

        var missing = locator.MissingRequiredTools();

        Assert.Equal(ToolManifest.Required.Count(), missing.Count);
        Assert.All(missing, tool => Assert.True(tool.IsRequired));
    }

    [Fact]
    public void Every_manifest_entry_is_usable()
    {
        foreach (var tool in ToolManifest.All)
        {
            Assert.NotEmpty(tool.ExecutableNames);
            Assert.False(string.IsNullOrWhiteSpace(tool.FolderName));
            Assert.False(string.IsNullOrWhiteSpace(tool.License));
            Assert.Equal(tool, ToolManifest.Get(tool.Id));
        }
    }
}
