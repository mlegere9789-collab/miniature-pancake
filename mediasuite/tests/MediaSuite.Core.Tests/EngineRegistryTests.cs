using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class EngineRegistryTests
{
    private sealed class FakeEngine : IConversionEngine
    {
        private readonly string _handles;

        public FakeEngine(string id, string handles)
        {
            Id = id;
            _handles = handles;
        }

        public string Id { get; }

        public string DisplayName => Id;

        public IReadOnlyList<ExternalToolId> RequiredTools => Array.Empty<ExternalToolId>();

        public bool CanHandle(JobSpec spec) =>
            string.Equals(spec.OperationId, _handles, StringComparison.OrdinalIgnoreCase);

        public Task<JobResult> RunAsync(JobSpec spec, IProgress<JobProgress> progress, CancellationToken cancellationToken) =>
            Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero));
    }

    private static JobSpec SpecFor(string operationId) => new()
    {
        OperationId = operationId,
        InputPaths = new[] { "input.png" },
        Output = new OutputTarget { Directory = "out" },
    };

    [Fact]
    public void Resolves_the_engine_that_claims_the_operation()
    {
        var registry = new EngineRegistry()
            .Register(new FakeEngine("ffmpeg", "video.convert"))
            .Register(new FakeEngine("magick", "image.convert"));

        Assert.Equal("magick", registry.Resolve(SpecFor("image.convert"))?.Id);
    }

    [Fact]
    public void A_later_registration_shadows_an_earlier_one()
    {
        var registry = new EngineRegistry()
            .Register(new FakeEngine("magick", "image.convert"))
            .Register(new FakeEngine("libvips", "image.convert"));

        Assert.Equal("libvips", registry.Resolve(SpecFor("image.convert"))?.Id);
    }

    [Fact]
    public void Resolve_returns_null_when_nothing_can_handle_the_job()
    {
        var registry = new EngineRegistry().Register(new FakeEngine("ffmpeg", "video.convert"));

        Assert.Null(registry.Resolve(SpecFor("pdf.merge")));
    }

    [Fact]
    public void ResolveRequired_explains_which_operation_had_no_engine()
    {
        var registry = new EngineRegistry();

        var error = Assert.Throws<InvalidOperationException>(() => registry.ResolveRequired(SpecFor("pdf.merge")));

        Assert.Contains("pdf.merge", error.Message, StringComparison.Ordinal);
    }
}
