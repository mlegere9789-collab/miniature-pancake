using System.Collections.Immutable;

namespace MediaSuite.Core.Formats;

/// <summary>
/// Catalogue of supported formats, audited against FreeConvert's published converter
/// pages in build step 13.
/// </summary>
/// <remarks>
/// <para>
/// Every entry here has to clear two bars: FreeConvert has to actually offer it (checked
/// against freeconvert.com's own converter pages, not assumed), and the bundled tool that
/// would handle it has to genuinely support it — not "probably works", but a real,
/// unlicensed encoder always present in a standard build. Where a FreeConvert format
/// failed the second bar it was left out rather than added and hoped for; see the
/// comments beside the entries below for the specific reasoning.
/// </para>
/// <para>
/// A few whole categories FreeConvert supports are deliberately still missing:
/// spreadsheet and presentation formats (XLSX, XLS, CSV, PPTX, PPT, ODS, ODP) would need
/// a new operation category — Pandoc has no spreadsheet or presentation support at all,
/// so these could only go through LibreOffice, which is a real capability but a different
/// build step's worth of engine and catalogue work, not a format-list edit. PostScript
/// (.ps) has the same shape: Ghostscript could read it, but nothing in the PDF module
/// today accepts a non-PDF input. MIDI is left out entirely rather than narrowed to
/// read-only, since FFmpeg does not meaningfully decode or encode it in a standard build.
/// </para>
/// </remarks>
public static class FormatCatalog
{
    private static readonly ImmutableArray<FileFormat> FormatList = ImmutableArray.Create(
        // Raster images
        new FileFormat("jpg", "JPEG Image", MediaKind.Image) { Aliases = new[] { "jpeg", "jpe" } },
        new FileFormat("png", "PNG Image", MediaKind.Image),
        new FileFormat("webp", "WebP Image", MediaKind.Image),
        new FileFormat("bmp", "Bitmap Image", MediaKind.Image),
        new FileFormat("tiff", "TIFF Image", MediaKind.Image) { Aliases = new[] { "tif" } },
        new FileFormat("heic", "HEIC Image", MediaKind.Image) { Aliases = new[] { "heif" } },
        new FileFormat("avif", "AVIF Image", MediaKind.Image),
        new FileFormat("jfif", "JFIF Image", MediaKind.Image),
        new FileFormat("ico", "Windows Icon", MediaKind.Image),
        new FileFormat("psd", "Photoshop Document", MediaKind.Image),
        new FileFormat("tga", "Truevision TGA", MediaKind.Image),
        new FileFormat("ppm", "Portable Pixmap", MediaKind.Image),

        // Vector
        new FileFormat("svg", "SVG Vector Image", MediaKind.Vector),
        new FileFormat("eps", "Encapsulated PostScript", MediaKind.Vector),

        // Animation
        new FileFormat("gif", "GIF Animation", MediaKind.Animation),
        new FileFormat("apng", "Animated PNG", MediaKind.Animation),

        // Camera RAW (read-only: RAW is a capture format, we never write it back)
        new FileFormat("cr2", "Canon RAW 2", MediaKind.RawImage, CanWrite: false),
        new FileFormat("cr3", "Canon RAW 3", MediaKind.RawImage, CanWrite: false),
        new FileFormat("nef", "Nikon RAW", MediaKind.RawImage, CanWrite: false),
        new FileFormat("arw", "Sony RAW", MediaKind.RawImage, CanWrite: false),
        new FileFormat("dng", "Adobe Digital Negative", MediaKind.RawImage, CanWrite: false),
        new FileFormat("orf", "Olympus RAW", MediaKind.RawImage, CanWrite: false),
        new FileFormat("rw2", "Panasonic RAW", MediaKind.RawImage, CanWrite: false),
        new FileFormat("raf", "Fujifilm RAW", MediaKind.RawImage, CanWrite: false),
        new FileFormat("pef", "Pentax RAW", MediaKind.RawImage, CanWrite: false),
        new FileFormat("srw", "Samsung RAW", MediaKind.RawImage, CanWrite: false),

        // Video
        new FileFormat("mp4", "MP4 Video", MediaKind.Video),
        // "qt" is the same QuickTime container under its older extension, not a distinct
        // format — an alias, so it is recognised without offering a second identical
        // choice in the output picker.
        new FileFormat("mov", "QuickTime Video", MediaKind.Video) { Aliases = new[] { "qt" } },
        new FileFormat("mkv", "Matroska Video", MediaKind.Video),
        new FileFormat("avi", "AVI Video", MediaKind.Video),
        new FileFormat("webm", "WebM Video", MediaKind.Video),
        new FileFormat("flv", "Flash Video", MediaKind.Video),
        new FileFormat("wmv", "Windows Media Video", MediaKind.Video),
        new FileFormat("mpeg", "MPEG Video", MediaKind.Video) { Aliases = new[] { "mpg" } },
        new FileFormat("3gp", "3GPP Video", MediaKind.Video),
        new FileFormat("m4v", "iTunes Video", MediaKind.Video),
        new FileFormat("ts", "MPEG Transport Stream", MediaKind.Video),
        // Read-only: FFmpeg demuxes these fine, but they are not containers this app
        // should ever be asked to write — VOB is a DVD-authoring artifact, and F4V/F4P
        // are old Flash-era MP4 variants nothing targets as an output today.
        new FileFormat("vob", "DVD Video Object", MediaKind.Video, CanWrite: false),
        new FileFormat("f4v", "Flash MP4 Video", MediaKind.Video, CanWrite: false) { Aliases = new[] { "f4p" } },

        // Audio
        new FileFormat("mp3", "MP3 Audio", MediaKind.Audio),
        new FileFormat("wav", "WAVE Audio", MediaKind.Audio),
        new FileFormat("aac", "AAC Audio", MediaKind.Audio),
        new FileFormat("flac", "FLAC Audio", MediaKind.Audio),
        new FileFormat("ogg", "Ogg Vorbis Audio", MediaKind.Audio),
        new FileFormat("m4a", "MPEG-4 Audio", MediaKind.Audio),
        new FileFormat("wma", "Windows Media Audio", MediaKind.Audio),
        new FileFormat("aiff", "AIFF Audio", MediaKind.Audio) { Aliases = new[] { "aif" } },
        new FileFormat("opus", "Opus Audio", MediaKind.Audio),
        // AC-3's encoder ships in FFmpeg's standard build with no licensing gate, unlike
        // the optional AMR encoders below, so this one is safe to offer as an output too.
        new FileFormat("ac3", "Dolby Digital Audio", MediaKind.Audio),
        // Read-only: FFmpeg decodes AMR fine, but the encoders are optional, licensed
        // components not guaranteed present in a given build, so this is not offered as
        // an output format the way AC-3 is.
        new FileFormat("amr", "Adaptive Multi-Rate Audio", MediaKind.Audio, CanWrite: false),

        // Documents
        new FileFormat("docx", "Word Document", MediaKind.Document),
        new FileFormat("doc", "Word 97-2003 Document", MediaKind.Document),
        new FileFormat("odt", "OpenDocument Text", MediaKind.Document),
        new FileFormat("rtf", "Rich Text Format", MediaKind.Document),
        new FileFormat("txt", "Plain Text", MediaKind.Document),
        new FileFormat("html", "HTML Document", MediaKind.Document) { Aliases = new[] { "htm" } },
        new FileFormat("md", "Markdown Document", MediaKind.Document),

        // Ebooks
        new FileFormat("epub", "EPUB Ebook", MediaKind.Ebook),
        new FileFormat("mobi", "Mobipocket Ebook", MediaKind.Ebook),
        new FileFormat("azw3", "Kindle Format 8", MediaKind.Ebook),

        // PDF
        new FileFormat("pdf", "PDF Document", MediaKind.Pdf),

        // Archives (RAR is extract-only: creating RAR needs a licensed encoder)
        new FileFormat("zip", "ZIP Archive", MediaKind.Archive),
        new FileFormat("7z", "7-Zip Archive", MediaKind.Archive),
        new FileFormat("tar", "TAR Archive", MediaKind.Archive),
        new FileFormat("gz", "GZIP Archive", MediaKind.Archive) { Aliases = new[] { "gzip" } },
        new FileFormat("rar", "RAR Archive", MediaKind.Archive, CanWrite: false));

    private static readonly ImmutableDictionary<string, FileFormat> ByExtension =
        FormatList
            .SelectMany(f => f.AllExtensions.Select(ext => KeyValuePair.Create(ext, f)))
            .ToImmutableDictionary(pair => pair.Key, pair => pair.Value, StringComparer.OrdinalIgnoreCase);

    /// <summary>Every format in the catalogue.</summary>
    public static IReadOnlyList<FileFormat> All => FormatList;

    /// <summary>Formats belonging to a single family.</summary>
    public static IEnumerable<FileFormat> OfKind(MediaKind kind) => FormatList.Where(f => f.Kind == kind);

    /// <summary>Formats that can be produced as output.</summary>
    public static IEnumerable<FileFormat> Writable => FormatList.Where(f => f.CanWrite);

    /// <summary>
    /// Looks up a format by extension. Accepts ".JPG", "JPG" or "jpg"; returns
    /// <c>null</c> when the extension is not in the catalogue.
    /// </summary>
    public static FileFormat? FromExtension(string? extension)
    {
        if (string.IsNullOrWhiteSpace(extension))
        {
            return null;
        }

        var normalized = extension.Trim().TrimStart('.');
        return ByExtension.TryGetValue(normalized, out var format) ? format : null;
    }

    /// <summary>Looks up a format from a file path, using its extension.</summary>
    public static FileFormat? FromPath(string? path) =>
        string.IsNullOrWhiteSpace(path) ? null : FromExtension(Path.GetExtension(path));

    /// <summary>Best guess at what family a file belongs to, based on its extension.</summary>
    public static MediaKind KindOf(string? path) => FromPath(path)?.Kind ?? MediaKind.Unknown;
}
