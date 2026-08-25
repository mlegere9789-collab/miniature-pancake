using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class JobSpecTests
{
    private static JobSpec Spec(params (string Key, string Value)[] options) => new()
    {
        OperationId = "video.compress",
        InputPaths = new[] { @"C:\clips\holiday.mp4" },
        Output = new OutputTarget { Directory = @"C:\out", Format = "mp4" },
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    [Fact]
    public void Options_are_read_case_insensitively_with_typed_fallbacks()
    {
        var spec = Spec(("CRF", "18"), ("scale", "0.5"), ("twoPass", "true"));

        Assert.Equal(18, spec.GetInt("crf", 23));
        Assert.Equal(0.5, spec.GetDouble("SCALE", 1.0));
        Assert.True(spec.GetBool("twopass", false));
        Assert.Equal("veryfast", spec.GetOption("preset", "veryfast"));
    }

    [Fact]
    public void Unparseable_options_fall_back_instead_of_throwing()
    {
        var spec = Spec(("crf", "not-a-number"), ("twoPass", "yes-please"));

        Assert.Equal(23, spec.GetInt("crf", 23));
        Assert.False(spec.GetBool("twoPass", false));
    }

    [Fact]
    public void WithOption_returns_a_copy_and_leaves_the_original_alone()
    {
        var original = Spec(("crf", "23"));

        var updated = original.WithOption("crf", "18").WithOption("preset", "slow");

        Assert.Equal("23", original.GetOption("crf"));
        Assert.Null(original.GetOption("preset"));
        Assert.Equal("18", updated.GetOption("crf"));
        Assert.Equal("slow", updated.GetOption("preset"));
    }

    [Fact]
    public void DisplayName_summarises_a_batch()
    {
        var single = Spec();
        Assert.Equal("holiday.mp4", single.DisplayName);

        var batch = single with
        {
            InputPaths = new[] { @"C:\a.mp4", @"C:\b.mp4", @"C:\c.mp4" },
        };
        Assert.Equal("a.mp4 + 2 more", batch.DisplayName);
    }

    [Fact]
    public void Defaults_are_the_agreed_ones()
    {
        var spec = Spec();

        Assert.Equal(QualityPreset.Balanced, spec.Preset);
        Assert.Equal(OverwritePolicy.Rename, spec.Output.OverwritePolicy);
        Assert.False(spec.Output.UploadToGoogleDrive);
        Assert.False(spec.Output.PreserveFolderStructure);
    }
}
