using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Features;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class EngineSetupTests : IDisposable
{
    private readonly TempDirectory _temp = new();

    public void Dispose() => _temp.Dispose();

    private EngineRegistry Build() =>
        EngineSetup.CreateDefaultRegistry(
            new FakeProcessRunner(),
            new ToolLocator(new[] { _temp.Combine("tools") }, pathVariable: string.Empty));

    [Fact]
    public void Every_image_operation_the_builder_knows_has_an_engine_behind_it()
    {
        var registry = Build();

        foreach (var operation in ImageOperations.All)
        {
            Assert.True(registry.SupportsOperation(operation), $"no engine claims {operation}");
        }

        Assert.True(registry.SupportsOperation(PngToSvgEngine.OperationId));
    }

    [Fact]
    public void Operations_from_later_build_steps_are_still_unclaimed()
    {
        var registry = Build();

        Assert.False(registry.SupportsOperation("video.convert"));
        Assert.False(registry.SupportsOperation("pdf.merge"));
        Assert.False(registry.SupportsOperation("upscale.photo"));
    }

    [Fact]
    public void Every_image_feature_in_the_catalogue_is_now_runnable()
    {
        // The brief's image list is the contract; this is what "the image module is done"
        // actually means.
        var registry = Build();

        var imageFeatures = FeatureCatalog.All
            .Where(feature => feature.BuildStep == 4)
            .ToList();

        Assert.NotEmpty(imageFeatures);

        var unclaimed = imageFeatures
            .Where(feature => !registry.SupportsOperation(feature.OperationId))
            .Select(feature => feature.OperationId)
            .ToList();

        // The colour picker is a UI tool: it reads pixels in the app and never runs a job.
        Assert.Equal(new[] { "image.color-picker" }, unclaimed);
    }
}
