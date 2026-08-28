using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>Builds the engine registry the app runs with.</summary>
public static class EngineSetup
{
    /// <summary>
    /// Registers every engine that exists today. Later build steps add PDF, document,
    /// archive and upscaling engines here; nothing else has to change.
    /// </summary>
    public static EngineRegistry CreateDefaultRegistry(IProcessRunner processRunner, ToolLocator toolLocator)
    {
        ArgumentNullException.ThrowIfNull(processRunner);
        ArgumentNullException.ThrowIfNull(toolLocator);

        return new EngineRegistry()
            .Register(new ImageMagickEngine(processRunner, toolLocator))
            .Register(new PngToSvgEngine(processRunner, toolLocator))
            .Register(new FFmpegEngine(processRunner, toolLocator))
            .Register(new GifEngine(processRunner, toolLocator));
    }
}
