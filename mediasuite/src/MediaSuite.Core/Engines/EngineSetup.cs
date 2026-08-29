using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>Builds the engine registry the app runs with.</summary>
public static class EngineSetup
{
    /// <summary>
    /// Registers every engine that exists today. Only the upscaling engine remains
    /// for a later build step; nothing else has to change to add it.
    /// </summary>
    public static EngineRegistry CreateDefaultRegistry(IProcessRunner processRunner, ToolLocator toolLocator)
    {
        ArgumentNullException.ThrowIfNull(processRunner);
        ArgumentNullException.ThrowIfNull(toolLocator);

        return new EngineRegistry()
            .Register(new ImageMagickEngine(processRunner, toolLocator))
            .Register(new PngToSvgEngine(processRunner, toolLocator))
            .Register(new FFmpegEngine(processRunner, toolLocator))
            .Register(new GifEngine(processRunner, toolLocator))
            .Register(new PdfEngine(processRunner, toolLocator))
            .Register(new DocumentEngine(processRunner, toolLocator))
            .Register(new ArchiveEngine(processRunner, toolLocator));
    }
}
