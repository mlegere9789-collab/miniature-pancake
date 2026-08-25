using System.Collections.Immutable;

namespace MediaSuite.Core.Tooling;

/// <summary>
/// The bundled-binary manifest. Adding an engine means adding a row here plus a
/// descriptor folder under <c>tools\</c>; nothing else in the app needs to change.
/// </summary>
/// <remarks>
/// Licence note for the record: this is a personal, non-distributed build, so the
/// GPL/AGPL tools (Ghostscript, Calibre, Pandoc) ship as-is. If the app is ever
/// distributed, Ghostscript and Calibre need a commercial licence or a replacement.
/// </remarks>
public static class ToolManifest
{
    private static readonly ImmutableArray<ToolDescriptor> Tools = ImmutableArray.Create(
        new ToolDescriptor(
            ExternalToolId.FFmpeg,
            "FFmpeg",
            "ffmpeg",
            new[] { "ffmpeg.exe", "ffmpeg" },
            "Video and audio conversion, compression, trimming, cropping and GIF encoding.",
            "LGPL-2.1 (shared build)",
            "https://www.gyan.dev/ffmpeg/builds/",
            IsRequired: true),
        new ToolDescriptor(
            ExternalToolId.FFprobe,
            "FFprobe",
            "ffmpeg",
            new[] { "ffprobe.exe", "ffprobe" },
            "Reads duration, streams and codecs so progress can be reported as a percentage.",
            "LGPL-2.1 (shared build)",
            "https://www.gyan.dev/ffmpeg/builds/",
            IsRequired: true),
        new ToolDescriptor(
            ExternalToolId.ImageMagick,
            "ImageMagick",
            "imagemagick",
            new[] { "magick.exe", "magick" },
            "The bulk of image conversion, resizing, rotation and compression.",
            "ImageMagick License (Apache-2.0 style)",
            "https://imagemagick.org/script/download.php",
            IsRequired: true),
        new ToolDescriptor(
            ExternalToolId.VipsThumbnail,
            "libvips",
            "libvips",
            new[] { "vipsthumbnail.exe", "vips.exe" },
            "Fast path for large images and big batches, where ImageMagick is slower.",
            "LGPL-2.1",
            "https://github.com/libvips/build-win64-mxe/releases"),
        new ToolDescriptor(
            ExternalToolId.LibRaw,
            "LibRaw (dcraw_emu)",
            "libraw",
            new[] { "dcraw_emu.exe", "unprocessed_raw.exe" },
            "Camera RAW decoding — CR2, CR3, NEF, ARW, DNG, ORF, RW2, RAF and friends.",
            "LGPL-2.1 / CDDL",
            "https://www.libraw.org/download",
            IsRequired: true),
        new ToolDescriptor(
            ExternalToolId.MuPdf,
            "MuPDF (mutool)",
            "mupdf",
            new[] { "mutool.exe", "mutool" },
            "PDF rendering to image, page extraction, text extraction and PDF tooling.",
            "AGPL-3.0",
            "https://mupdf.com/releases"),
        new ToolDescriptor(
            ExternalToolId.QPdf,
            "QPDF",
            "qpdf",
            new[] { "qpdf.exe", "qpdf" },
            "Merge, split, rotate, encrypt, decrypt and re-organise PDF pages losslessly.",
            "Apache-2.0",
            "https://github.com/qpdf/qpdf/releases"),
        new ToolDescriptor(
            ExternalToolId.Ghostscript,
            "Ghostscript",
            "ghostscript",
            new[] { "gswin64c.exe", "gs" },
            "PDF compression and flattening, plus PostScript/EPS handling.",
            "AGPL-3.0",
            "https://www.ghostscript.com/releases/gsdnld.html"),
        new ToolDescriptor(
            ExternalToolId.Pandoc,
            "Pandoc",
            "pandoc",
            new[] { "pandoc.exe", "pandoc" },
            "Text document conversion between DOCX, ODT, RTF, HTML, Markdown and EPUB.",
            "GPL-2.0-or-later",
            "https://github.com/jgm/pandoc/releases"),
        new ToolDescriptor(
            ExternalToolId.LibreOffice,
            "LibreOffice (headless)",
            "libreoffice",
            new[] { "soffice.exe", "soffice" },
            "High-fidelity DOCX/DOC/ODT to PDF conversion where Pandoc loses layout.",
            "MPL-2.0",
            "https://www.libreoffice.org/download/download-libreoffice/"),
        new ToolDescriptor(
            ExternalToolId.Calibre,
            "Calibre (ebook-convert)",
            "calibre",
            new[] { "ebook-convert.exe", "ebook-convert" },
            "Ebook conversion — EPUB, MOBI, AZW3 and PDF round trips.",
            "GPL-3.0",
            "https://calibre-ebook.com/download_windows"),
        new ToolDescriptor(
            ExternalToolId.SevenZip,
            "7-Zip",
            "7zip",
            new[] { "7z.exe", "7za.exe" },
            "Archive conversion: ZIP, 7Z, TAR, GZIP creation and RAR extraction.",
            "LGPL-2.1 (unRAR code under its own restricted licence)",
            "https://www.7-zip.org/download.html"),
        new ToolDescriptor(
            ExternalToolId.RealEsrgan,
            "Real-ESRGAN",
            "realesrgan",
            new[] { "realesrgan-ncnn-vulkan.exe", "realesrgan.exe" },
            "AI photo upscaling at 2x/4x/8x, CUDA-accelerated with a CPU fallback.",
            "BSD-3-Clause",
            "https://github.com/xinntao/Real-ESRGAN/releases"),
        new ToolDescriptor(
            ExternalToolId.Rsvg,
            "librsvg (rsvg-convert)",
            "rsvg",
            new[] { "rsvg-convert.exe", "rsvg-convert" },
            "SVG rasterising at arbitrary resolution.",
            "LGPL-2.1",
            "https://gitlab.gnome.org/GNOME/librsvg"),
        new ToolDescriptor(
            ExternalToolId.Potrace,
            "Potrace",
            "potrace",
            new[] { "potrace.exe", "potrace" },
            "Raster to vector tracing, used by PNG to SVG.",
            "GPL-2.0",
            "https://potrace.sourceforge.net/#downloading"));

    /// <summary>Every tool the app knows how to use.</summary>
    public static IReadOnlyList<ToolDescriptor> All => Tools;

    /// <summary>Tools without which the app cannot do its core work.</summary>
    public static IEnumerable<ToolDescriptor> Required => Tools.Where(t => t.IsRequired);

    public static ToolDescriptor Get(ExternalToolId id) =>
        Tools.FirstOrDefault(t => t.Id == id)
        ?? throw new ArgumentOutOfRangeException(nameof(id), id, "No manifest entry for this tool.");
}
