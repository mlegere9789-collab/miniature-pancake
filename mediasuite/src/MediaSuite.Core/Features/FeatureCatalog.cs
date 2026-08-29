using System.Collections.Immutable;

namespace MediaSuite.Core.Features;

/// <summary>
/// Which screen a feature lives on.
/// </summary>
public enum FeatureSection
{
    Convert,
    Compress,
    Tools,
    Upscale,
}

/// <summary>
/// One user-facing tool.
/// </summary>
/// <param name="OperationId">Id passed to the engine as <c>JobSpec.OperationId</c>.</param>
/// <param name="Name">Name shown on the card.</param>
/// <param name="Section">Screen the feature appears on.</param>
/// <param name="Group">Sub-heading within that screen ("Image", "PDF Tools", …).</param>
/// <param name="Description">One line explaining what it does.</param>
/// <param name="BuildStep">
/// Build-order step from the project plan that delivers this feature. The shell uses it
/// to show what is wired up and what is still coming.
/// </param>
public sealed record FeatureDescriptor(
    string OperationId,
    string Name,
    FeatureSection Section,
    string Group,
    string Description,
    int BuildStep);

/// <summary>
/// The complete tool list the app is committed to shipping, taken from the project
/// brief. It is the single source of truth for the shell's navigation content and for
/// the operation ids engines advertise, so a feature cannot silently go missing.
/// </summary>
public static class FeatureCatalog
{
    private static readonly ImmutableArray<FeatureDescriptor> Features = ImmutableArray.Create(
        new FeatureDescriptor("video.convert", "Video Converter", FeatureSection.Convert, "Video & Audio", "Any container or codec FFmpeg can read, out to any it can write.", 5),
        new FeatureDescriptor("audio.convert", "Audio Converter", FeatureSection.Convert, "Video & Audio", "MP3, WAV, AAC, FLAC, OGG, M4A, WMA, AIFF, Opus and AC-3, in any direction.", 5),
        new FeatureDescriptor("audio.convert.mp3", "MP3 Converter", FeatureSection.Convert, "Video & Audio", "Anything with an audio track, out as MP3 at your chosen bitrate.", 5),
        new FeatureDescriptor("video.mp4-to-mp3", "MP4 to MP3", FeatureSection.Convert, "Video & Audio", "Pull the audio track out of an MP4 without re-encoding where possible.", 5),
        new FeatureDescriptor("video.to-mp3", "Video to MP3", FeatureSection.Convert, "Video & Audio", "Extract audio from any video format straight to MP3.", 5),
        new FeatureDescriptor("video.convert.mp4", "MP4 Converter", FeatureSection.Convert, "Video & Audio", "Convert into MP4 with H.264, H.265 or AV1.", 5),
        new FeatureDescriptor("video.mov-to-mp4", "MOV to MP4", FeatureSection.Convert, "Video & Audio", "QuickTime to MP4, remuxing instead of re-encoding when the codecs already fit.", 5),
        new FeatureDescriptor("audio.mp3-to-ogg", "MP3 to OGG", FeatureSection.Convert, "Video & Audio", "MP3 out to Ogg Vorbis at a chosen quality level.", 5),
        new FeatureDescriptor("image.convert", "Image Converter", FeatureSection.Convert, "Image", "500+ formats including camera RAW, driven by ImageMagick, libvips and LibRaw.", 4),
        new FeatureDescriptor("image.webp-to-png", "WEBP to PNG", FeatureSection.Convert, "Image", "WebP to lossless PNG, alpha preserved.", 4),
        new FeatureDescriptor("image.jfif-to-png", "JFIF to PNG", FeatureSection.Convert, "Image", "JFIF to PNG in one click.", 4),
        new FeatureDescriptor("image.png-to-svg", "PNG to SVG", FeatureSection.Convert, "Image", "Raster to vector tracing via Potrace, with threshold and curve controls.", 4),
        new FeatureDescriptor("image.heic-to-jpg", "HEIC to JPG", FeatureSection.Convert, "Image", "iPhone HEIC photos out as JPEG, EXIF kept.", 4),
        new FeatureDescriptor("image.heic-to-png", "HEIC to PNG", FeatureSection.Convert, "Image", "HEIC to lossless PNG.", 4),
        new FeatureDescriptor("image.webp-to-jpg", "WEBP to JPG", FeatureSection.Convert, "Image", "WebP to JPEG with a quality slider.", 4),
        new FeatureDescriptor("image.svg-convert", "SVG Converter", FeatureSection.Convert, "Image", "Rasterise SVG at any resolution, or convert between vector formats.", 4),
        new FeatureDescriptor("pdf.convert", "PDF Converter", FeatureSection.Convert, "PDF & Documents", "PDF in or out, to and from images, Office documents and ebooks.", 7),
        new FeatureDescriptor("document.convert", "Document Converter", FeatureSection.Convert, "PDF & Documents", "DOCX, DOC, ODT, RTF, TXT, HTML and Markdown, in any direction.", 8),
        new FeatureDescriptor("ebook.convert", "Ebook Converter", FeatureSection.Convert, "PDF & Documents", "EPUB, MOBI and AZW3 conversion through Calibre.", 8),
        new FeatureDescriptor("pdf.to-word", "PDF to Word", FeatureSection.Convert, "PDF & Documents", "PDF out as an editable DOCX, layout preserved as far as possible.", 7),
        new FeatureDescriptor("pdf.to-jpg", "PDF to JPG", FeatureSection.Convert, "PDF & Documents", "Every page rendered to JPEG at a DPI you choose.", 7),
        new FeatureDescriptor("pdf.to-epub", "PDF to EPUB", FeatureSection.Convert, "PDF & Documents", "Reflowable ebook from a PDF.", 8),
        new FeatureDescriptor("ebook.epub-to-pdf", "EPUB to PDF", FeatureSection.Convert, "PDF & Documents", "EPUB out as a paginated PDF.", 8),
        new FeatureDescriptor("image.heic-to-pdf", "HEIC to PDF", FeatureSection.Convert, "PDF & Documents", "One or many HEIC photos into a single PDF.", 7),
        new FeatureDescriptor("document.docx-to-pdf", "DOCX to PDF", FeatureSection.Convert, "PDF & Documents", "High-fidelity Word to PDF via headless LibreOffice.", 8),
        new FeatureDescriptor("image.jpg-to-pdf", "JPG to PDF", FeatureSection.Convert, "PDF & Documents", "Images into a PDF, one page each, with page-size options.", 7),
        new FeatureDescriptor("gif.from-video", "Video to GIF", FeatureSection.Convert, "GIF", "Any video to GIF with frame rate, width and palette control.", 6),
        new FeatureDescriptor("gif.mp4-to-gif", "MP4 to GIF", FeatureSection.Convert, "GIF", "MP4 to GIF with an optimised palette.", 6),
        new FeatureDescriptor("gif.webm-to-gif", "WEBM to GIF", FeatureSection.Convert, "GIF", "WebM to GIF.", 6),
        new FeatureDescriptor("gif.apng-to-gif", "APNG to GIF", FeatureSection.Convert, "GIF", "Animated PNG to GIF.", 6),
        new FeatureDescriptor("gif.to-mp4", "GIF to MP4", FeatureSection.Convert, "GIF", "GIF to MP4 — usually a fraction of the size.", 6),
        new FeatureDescriptor("gif.to-apng", "GIF to APNG", FeatureSection.Convert, "GIF", "GIF to animated PNG, for true colour and alpha.", 6),
        new FeatureDescriptor("gif.from-images", "Image to GIF", FeatureSection.Convert, "GIF", "Build an animation from a sequence of stills.", 6),
        new FeatureDescriptor("gif.mov-to-gif", "MOV to GIF", FeatureSection.Convert, "GIF", "QuickTime to GIF.", 6),
        new FeatureDescriptor("gif.avi-to-gif", "AVI to GIF", FeatureSection.Convert, "GIF", "AVI to GIF.", 6),
        new FeatureDescriptor("util.unit-convert", "Unit Converter", FeatureSection.Convert, "Others", "Length, mass, area, volume, temperature, data and speed.", 9),
        new FeatureDescriptor("util.time-convert", "Time Converter", FeatureSection.Convert, "Others", "Time zones, timestamps, durations and frame counts.", 9),
        new FeatureDescriptor("archive.convert", "Archive Converter", FeatureSection.Convert, "Others", "ZIP, 7Z, TAR and GZIP conversion, plus RAR extraction.", 9),
        new FeatureDescriptor("video.compress", "Video Compressor", FeatureSection.Compress, "Video & Audio", "Target a file size or a quality level; CRF, bitrate and codec exposed.", 5),
        new FeatureDescriptor("audio.compress.mp3", "MP3 Compressor", FeatureSection.Compress, "Video & Audio", "Re-encode MP3 at a lower bitrate, CBR or VBR.", 5),
        new FeatureDescriptor("audio.compress.wav", "WAV Compressor", FeatureSection.Compress, "Video & Audio", "Shrink WAV by sample rate, bit depth or a lossless codec.", 5),
        new FeatureDescriptor("image.compress", "Image Compressor", FeatureSection.Compress, "Image", "Quality-target or size-target compression across every raster format.", 4),
        new FeatureDescriptor("image.compress.jpeg", "JPEG Compressor", FeatureSection.Compress, "Image", "JPEG quality, chroma subsampling and progressive encoding.", 4),
        new FeatureDescriptor("image.compress.png", "PNG Compressor", FeatureSection.Compress, "Image", "Lossless and palette-quantised PNG compression.", 4),
        new FeatureDescriptor("pdf.compress", "PDF Compressor", FeatureSection.Compress, "PDF & Documents", "Ghostscript downsampling presets from screen to prepress.", 7),
        new FeatureDescriptor("gif.compress", "GIF Compressor", FeatureSection.Compress, "GIF", "Fewer colours, dropped frames and lossy LZW, with a live size estimate.", 6),
        new FeatureDescriptor("video.crop", "Crop Video", FeatureSection.Tools, "Video Tools", "Crop to a region or an aspect ratio, with a preview.", 5),
        new FeatureDescriptor("video.trim", "Trim Video", FeatureSection.Tools, "Video Tools", "Cut by timecode, losslessly where the keyframes allow it.", 5),
        new FeatureDescriptor("gif.maker", "GIF Maker", FeatureSection.Tools, "Image Tools", "Assemble a GIF from images or a video clip, with timing per frame.", 6),
        new FeatureDescriptor("image.resize", "Resize Image", FeatureSection.Tools, "Image Tools", "Resize by pixels, percentage or longest edge, aspect ratio locked.", 4),
        new FeatureDescriptor("image.crop", "Crop Image", FeatureSection.Tools, "Image Tools", "Crop to a region, an aspect ratio or a fixed output size.", 4),
        new FeatureDescriptor("image.color-picker", "Color Picker", FeatureSection.Tools, "Image Tools", "Pick colours from an image and copy HEX, RGB or HSL.", 4),
        new FeatureDescriptor("image.rotate", "Rotate Image", FeatureSection.Tools, "Image Tools", "Rotate by 90° steps or an arbitrary angle.", 4),
        new FeatureDescriptor("image.flip", "Flip Image", FeatureSection.Tools, "Image Tools", "Flip horizontally or vertically.", 4),
        new FeatureDescriptor("image.enlarge", "Image Enlarger", FeatureSection.Tools, "Image Tools", "Classic interpolated enlargement — see Upscale for the AI version.", 4),
        new FeatureDescriptor("pdf.merge", "PDF Merge", FeatureSection.Tools, "PDF Tools", "Combine PDFs in any order, drag to reorder.", 7),
        new FeatureDescriptor("pdf.split", "PDF Split", FeatureSection.Tools, "PDF Tools", "Split by page ranges, every N pages or into single pages.", 7),
        new FeatureDescriptor("pdf.flatten", "Flatten PDF", FeatureSection.Tools, "PDF Tools", "Flatten forms and annotations into the page content.", 7),
        new FeatureDescriptor("pdf.resize", "Resize PDF", FeatureSection.Tools, "PDF Tools", "Scale pages to A4, Letter or a custom size.", 7),
        new FeatureDescriptor("pdf.unlock", "Unlock PDF", FeatureSection.Tools, "PDF Tools", "Remove a password you know, and lift printing restrictions.", 7),
        new FeatureDescriptor("pdf.rotate", "Rotate PDF", FeatureSection.Tools, "PDF Tools", "Rotate selected pages or the whole document.", 7),
        new FeatureDescriptor("pdf.protect", "Protect PDF", FeatureSection.Tools, "PDF Tools", "Add a password and set permissions.", 7),
        new FeatureDescriptor("pdf.crop", "Crop PDF", FeatureSection.Tools, "PDF Tools", "Trim margins across pages, uniformly or per page.", 7),
        new FeatureDescriptor("pdf.organize", "Organize PDF", FeatureSection.Tools, "PDF Tools", "Reorder, duplicate and delete pages in a visual grid.", 7),
        new FeatureDescriptor("pdf.extract-images", "Extract Image from PDF", FeatureSection.Tools, "PDF Tools", "Pull embedded images out at original resolution.", 7),
        new FeatureDescriptor("pdf.remove-pages", "PDF Page Remover", FeatureSection.Tools, "PDF Tools", "Delete pages by number or range.", 7),
        new FeatureDescriptor("pdf.extract-pages", "Extract Pages from PDF", FeatureSection.Tools, "PDF Tools", "Save selected pages as a new PDF.", 7),
        new FeatureDescriptor("upscale.photo", "AI Photo Upscaler", FeatureSection.Upscale, "AI Upscale", "Real-ESRGAN at 2x, 4x or 8x with general, anime and face-enhance models, denoise and sharpen, CUDA-accelerated.", 10));

    /// <summary>Every feature, in brief order.</summary>
    public static IReadOnlyList<FeatureDescriptor> All => Features;

    /// <summary>Features on one screen, still in brief order.</summary>
    public static IEnumerable<FeatureDescriptor> InSection(FeatureSection section) =>
        Features.Where(f => f.Section == section);

    /// <summary>Features on one screen, grouped by their sub-heading.</summary>
    public static IEnumerable<IGrouping<string, FeatureDescriptor>> GroupedBySection(FeatureSection section) =>
        InSection(section).GroupBy(f => f.Group);

    public static FeatureDescriptor? FromOperationId(string operationId) =>
        Features.FirstOrDefault(f => string.Equals(f.OperationId, operationId, StringComparison.OrdinalIgnoreCase));
}
