namespace MediaSuite.Core.Tooling;

/// <summary>
/// Third-party binaries MediaSuite shells out to. The app ships them under its own
/// <c>tools\</c> folder; nothing here is expected on the user's PATH.
/// </summary>
public enum ExternalToolId
{
    FFmpeg,
    FFprobe,
    ImageMagick,
    VipsThumbnail,
    LibRaw,
    MuPdf,
    QPdf,
    Ghostscript,
    Pandoc,
    LibreOffice,
    Calibre,
    SevenZip,
    RealEsrgan,
    Rsvg,
    Potrace,
    GfpganFaceEnhance,
}
