using MediaSuite.App.ViewModels;
using MediaSuite.Core.Features;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// Three small view models with real logic but no WPF surface, so unlike
/// <see cref="Services.ThemeService"/> they need no <c>Window</c> to exercise directly:
/// <see cref="FeatureViewModel"/>'s status text, <see cref="FeatureGroupViewModel"/>'s
/// ready-count aggregation, and <see cref="ToolStatusViewModel"/>'s constructor, which is
/// where <see cref="ToolSource"/> actually turns into what Settings → Bundled tools shows.
/// </summary>
public class FeatureAndToolStatusViewModelTests
{
    private static FeatureDescriptor MakeDescriptor(string operationId = "video.convert", int buildStep = 5) =>
        new(operationId, "Video Converter", FeatureSection.Convert, "Video & Audio", "Converts video.", buildStep);

    [Fact]
    public void FeatureViewModel_exposes_the_descriptor_fields_verbatim()
    {
        var descriptor = MakeDescriptor();
        var feature = new FeatureViewModel(descriptor, isAvailable: true);

        Assert.Equal(descriptor.Name, feature.Name);
        Assert.Equal(descriptor.Description, feature.Description);
        Assert.Equal(descriptor.OperationId, feature.OperationId);
        Assert.Equal(descriptor.Name, feature.ToString());
    }

    [Fact]
    public void FeatureViewModel_status_label_is_Ready_when_available()
    {
        var feature = new FeatureViewModel(MakeDescriptor(), isAvailable: true);

        Assert.Equal("Ready", feature.StatusLabel);
    }

    [Fact]
    public void FeatureViewModel_status_label_names_the_build_step_when_not_available()
    {
        var feature = new FeatureViewModel(MakeDescriptor(buildStep: 9), isAvailable: false);

        Assert.Equal("Build step 9", feature.StatusLabel);
    }

    [Fact]
    public void FeatureGroupViewModel_count_label_omits_the_ready_count_when_everything_is_ready()
    {
        var features = new[]
        {
            new FeatureViewModel(MakeDescriptor("a"), isAvailable: true),
            new FeatureViewModel(MakeDescriptor("b"), isAvailable: true),
        };
        var group = new FeatureGroupViewModel("Video & Audio", features);

        Assert.Equal("2 tools", group.CountLabel);
    }

    [Fact]
    public void FeatureGroupViewModel_count_label_uses_singular_tool_for_exactly_one()
    {
        var features = new[] { new FeatureViewModel(MakeDescriptor(), isAvailable: true) };
        var group = new FeatureGroupViewModel("Video & Audio", features);

        Assert.Equal("1 tool", group.CountLabel);
    }

    [Fact]
    public void FeatureGroupViewModel_count_label_shows_the_ready_count_when_some_are_not()
    {
        var features = new[]
        {
            new FeatureViewModel(MakeDescriptor("a"), isAvailable: true),
            new FeatureViewModel(MakeDescriptor("b"), isAvailable: false),
            new FeatureViewModel(MakeDescriptor("c"), isAvailable: false),
        };
        var group = new FeatureGroupViewModel("Video & Audio", features);

        Assert.Equal("3 tools · 1 ready", group.CountLabel);
    }

    [Fact]
    public void FeatureGroupViewModel_count_label_handles_zero_ready_out_of_one()
    {
        var features = new[] { new FeatureViewModel(MakeDescriptor(), isAvailable: false) };
        var group = new FeatureGroupViewModel("Video & Audio", features);

        Assert.Equal("1 tool · 0 ready", group.CountLabel);
    }

    private static ToolDescriptor MakeToolDescriptor(bool isRequired = false) =>
        new(
            ExternalToolId.FFmpeg,
            "FFmpeg",
            "ffmpeg",
            new[] { "ffmpeg.exe" },
            "Video and audio conversion.",
            "LGPL-2.1",
            "https://www.gyan.dev/ffmpeg/builds/",
            isRequired);

    [Theory]
    [InlineData(ToolSource.Override, "Found (custom path)")]
    [InlineData(ToolSource.Bundled, "Found (bundled)")]
    [InlineData(ToolSource.SystemPath, "Found (system PATH)")]
    public void ToolStatusViewModel_status_text_names_where_a_found_tool_came_from(ToolSource source, string expectedStatus)
    {
        var location = new ToolLocation(ExternalToolId.FFmpeg, @"C:\tools\ffmpeg\ffmpeg.exe", source);
        var status = new ToolStatusViewModel(MakeToolDescriptor(), location);

        Assert.Equal(expectedStatus, status.Status);
        Assert.True(status.IsFound);
        Assert.Equal(location.Path, status.Path);
    }

    [Fact]
    public void ToolStatusViewModel_reports_missing_required_distinctly_from_missing_optional()
    {
        var missing = ToolLocation.Missing(ExternalToolId.FFmpeg);

        var required = new ToolStatusViewModel(MakeToolDescriptor(isRequired: true), missing);
        var optional = new ToolStatusViewModel(MakeToolDescriptor(isRequired: false), missing);

        Assert.Equal("Missing — required", required.Status);
        Assert.Equal("Missing — optional", optional.Status);
        Assert.False(required.IsFound);
        Assert.False(optional.IsFound);
    }

    [Fact]
    public void ToolStatusViewModel_falls_back_to_the_expected_folder_when_no_path_was_found()
    {
        var status = new ToolStatusViewModel(MakeToolDescriptor(), ToolLocation.Missing(ExternalToolId.FFmpeg));

        Assert.Equal(@"Expected in tools\ffmpeg\", status.Path);
    }

    [Fact]
    public void ToolStatusViewModel_copies_the_descriptor_fields_verbatim()
    {
        var descriptor = MakeToolDescriptor(isRequired: true);
        var status = new ToolStatusViewModel(descriptor, ToolLocation.Missing(ExternalToolId.FFmpeg));

        Assert.Equal(descriptor.DisplayName, status.Name);
        Assert.Equal(descriptor.Purpose, status.Purpose);
        Assert.Equal(descriptor.License, status.License);
        Assert.Equal(descriptor.SourceUrl, status.SourceUrl);
        Assert.True(status.IsRequired);
    }
}
