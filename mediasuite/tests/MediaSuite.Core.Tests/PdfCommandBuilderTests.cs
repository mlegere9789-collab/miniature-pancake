using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class PdfCommandBuilderTests
{
    // --- Tool selection ------------------------------------------------------------------

    [Theory]
    [InlineData("pdf.merge", ExternalToolId.QPdf)]
    [InlineData("pdf.rotate", ExternalToolId.QPdf)]
    [InlineData("pdf.unlock", ExternalToolId.QPdf)]
    [InlineData("pdf.protect", ExternalToolId.QPdf)]
    [InlineData("pdf.flatten", ExternalToolId.QPdf)]
    [InlineData("pdf.organize", ExternalToolId.QPdf)]
    [InlineData("pdf.remove-pages", ExternalToolId.QPdf)]
    [InlineData("pdf.extract-pages", ExternalToolId.QPdf)]
    [InlineData("pdf.split", ExternalToolId.QPdf)]
    [InlineData("pdf.crop", ExternalToolId.Ghostscript)]
    [InlineData("pdf.resize", ExternalToolId.Ghostscript)]
    [InlineData("pdf.compress", ExternalToolId.Ghostscript)]
    [InlineData("pdf.to-jpg", ExternalToolId.MuPdf)]
    [InlineData("pdf.extract-images", ExternalToolId.MuPdf)]
    [InlineData("pdf.to-word", ExternalToolId.LibreOffice)]
    [InlineData("image.jpg-to-pdf", ExternalToolId.MuPdf)]
    public void Each_operation_asks_for_the_one_tool_that_actually_does_it(string operationId, ExternalToolId expected)
    {
        Assert.Equal(expected, PdfCommandBuilder.ToolFor(operationId));
    }

    // --- QPDF ------------------------------------------------------------------------------

    [Fact]
    public void Merging_lists_every_file_and_writes_the_output_last()
    {
        var arguments = PdfCommandBuilder.Merge(new[] { "a.pdf", "b.pdf", "c.pdf" }, "merged.pdf");
        var list = arguments.ToList();

        Assert.Equal("--empty", list[0]);
        Assert.Contains("a.pdf", list);
        Assert.Contains("b.pdf", list);
        Assert.Contains("c.pdf", list);

        // Order matters — this is what makes "combine in any order, drag to reorder" true.
        Assert.True(list.IndexOf("a.pdf") < list.IndexOf("b.pdf"));
        Assert.True(list.IndexOf("b.pdf") < list.IndexOf("c.pdf"));
        Assert.Equal("merged.pdf", list[^1]);
    }

    [Fact]
    public void Rotating_puts_the_flag_before_the_files_so_the_output_stays_last()
    {
        var arguments = PdfCommandBuilder.Rotate("in.pdf", "out.pdf", 90, "1-3");
        var list = arguments.ToList();

        Assert.Equal("--rotate=90:1-3", list[0]);
        Assert.Equal("in.pdf", list[1]);
        Assert.Equal("out.pdf", list[^1]);
    }

    [Fact]
    public void Rotating_the_whole_document_omits_the_page_range()
    {
        var arguments = PdfCommandBuilder.Rotate("in.pdf", "out.pdf", 180, pageRange: null);

        Assert.Contains("--rotate=180", arguments);
        Assert.DoesNotContain(arguments, a => a.Contains(':'));
    }

    [Fact]
    public void Unlocking_passes_the_password_and_asks_to_decrypt()
    {
        var arguments = PdfCommandBuilder.Unlock("locked.pdf", "open.pdf", "secret").ToList();

        Assert.Contains("--decrypt", arguments);
        Assert.Contains("--password=secret", arguments);
        Assert.Equal("open.pdf", arguments[^1]);
    }

    [Fact]
    public void Unlocking_without_a_password_still_asks_to_decrypt()
    {
        // Some PDFs restrict printing/copying without an open password at all.
        var arguments = PdfCommandBuilder.Unlock("locked.pdf", "open.pdf", string.Empty);

        Assert.Contains("--decrypt", arguments);
        Assert.DoesNotContain(arguments, a => a.StartsWith("--password", StringComparison.Ordinal));
    }

    [Fact]
    public void Protecting_falls_back_to_the_user_password_when_no_owner_password_is_given()
    {
        // qpdf's --encrypt always needs both; without this the command would be malformed.
        var arguments = PdfCommandBuilder.Protect("in.pdf", "out.pdf", "userpw", null, true, true).ToList();
        var encryptIndex = arguments.IndexOf("--encrypt");

        Assert.Equal("userpw", arguments[encryptIndex + 1]);
        Assert.Equal("userpw", arguments[encryptIndex + 2]);
        Assert.Equal("256", arguments[encryptIndex + 3]);
    }

    [Fact]
    public void Protecting_can_restrict_printing_and_copying_independently()
    {
        var restricted = PdfCommandBuilder.Protect("in.pdf", "out.pdf", "pw", "ownerpw", false, false);

        Assert.Contains("--print=none", restricted);
        Assert.Contains("--extract=n", restricted);

        var permissive = PdfCommandBuilder.Protect("in.pdf", "out.pdf", "pw", "ownerpw", true, true);

        Assert.Contains("--print=full", permissive);
        Assert.Contains("--extract=y", permissive);
    }

    [Fact]
    public void Flattening_asks_for_every_annotation()
    {
        var arguments = PdfCommandBuilder.Flatten("in.pdf", "out.pdf");

        Assert.Contains("--flatten-annotations=all", arguments);
        Assert.Equal("out.pdf", arguments[^1]);
    }

    [Fact]
    public void Selecting_pages_reads_them_from_the_input_itself()
    {
        // "." tells qpdf the page list refers back to its own primary input, rather than
        // needing the file named a second time.
        var arguments = PdfCommandBuilder.SelectPages("in.pdf", "out.pdf", "3,1,2").ToList();

        Assert.Equal(new[] { "in.pdf", "--pages", ".", "3,1,2", "--", "out.pdf" }, arguments);
    }

    [Fact]
    public void Counting_pages_asks_qpdf_directly_with_no_output_file()
    {
        Assert.Equal(new[] { "--show-npages", "in.pdf" }, PdfCommandBuilder.CountPages("in.pdf"));
    }

    [Fact]
    public void Splitting_every_n_pages_ends_with_the_numbered_pattern()
    {
        var arguments = PdfCommandBuilder.SplitEvery("in.pdf", "part-%d.pdf", 2);

        Assert.Contains("--split-pages=2", arguments);
        Assert.Equal("part-%d.pdf", arguments[^1]);
    }

    // --- Ghostscript -------------------------------------------------------------------

    [Fact]
    public void Cropping_writes_the_margin_as_a_postscript_setpagedevice_snippet()
    {
        var arguments = PdfCommandBuilder.Crop("in.pdf", "out.pdf", 36).ToList();

        // -c takes the code as its own argument and -f switches back to file mode — the
        // two must never be concatenated into one token, or Ghostscript sees neither.
        var codeIndex = arguments.IndexOf("-c") + 1;

        Assert.Contains("Margins", arguments[codeIndex], StringComparison.Ordinal);
        Assert.Contains("36", arguments[codeIndex], StringComparison.Ordinal);
        Assert.Equal("-f", arguments[codeIndex + 1]);
        Assert.Equal("in.pdf", arguments[codeIndex + 2]);
    }

    [Fact]
    public void Resizing_fits_the_page_to_fixed_device_dimensions()
    {
        var arguments = PdfCommandBuilder.Resize("in.pdf", "out.pdf", 595.28, 841.89).ToList();

        Assert.Contains("-dPDFFitPage", arguments);
        Assert.Contains("-dFIXEDMEDIA", arguments);
        Assert.Contains("-dDEVICEWIDTHPOINTS=595.28", arguments);
        Assert.Contains("-dDEVICEHEIGHTPOINTS=841.89", arguments);

        var outputIndex = arguments.IndexOf("-o") + 1;
        Assert.Equal("out.pdf", arguments[outputIndex]);
        Assert.Equal("in.pdf", arguments[^1]);
    }

    [Theory]
    [InlineData("a4", 595.28, 841.89)]
    [InlineData("letter", 612, 792)]
    [InlineData("legal", 612, 1008)]
    [InlineData("unknown-size", 595.28, 841.89)]
    public void Paper_sizes_map_to_the_right_points(string name, double width, double height)
    {
        var (actualWidth, actualHeight) = PdfCommandBuilder.PaperSize(name);

        Assert.Equal(width, actualWidth, 2);
        Assert.Equal(height, actualHeight, 2);
    }

    [Fact]
    public void Compressing_picks_the_named_downsampling_preset()
    {
        var arguments = PdfCommandBuilder.Compress("in.pdf", "out.pdf", "ebook");

        Assert.Contains("-dPDFSETTINGS=/ebook", arguments);
    }

    [Theory]
    [InlineData(QualityPreset.Quick, "screen")]
    [InlineData(QualityPreset.Balanced, "ebook")]
    [InlineData(QualityPreset.Best, "prepress")]
    public void The_preset_maps_to_ghostscripts_own_names(QualityPreset preset, string expected)
    {
        Assert.Equal(expected, PdfCommandBuilder.PdfSettingsFor(preset));
    }

    // --- MuPDF -----------------------------------------------------------------------------

    [Fact]
    public void Rendering_pages_asks_for_a_resolution_and_a_numbered_pattern()
    {
        var arguments = PdfCommandBuilder.RenderPages("in.pdf", "page-%d.jpg", 150).ToList();

        Assert.Equal("draw", arguments[0]);
        Assert.Equal("page-%d.jpg", arguments[arguments.IndexOf("-o") + 1]);
        Assert.Equal("150", arguments[arguments.IndexOf("-r") + 1]);
        Assert.Equal("in.pdf", arguments[^1]);
    }

    [Theory]
    [InlineData(QualityPreset.Quick, 96)]
    [InlineData(QualityPreset.Balanced, 150)]
    [InlineData(QualityPreset.Best, 300)]
    public void The_render_resolution_follows_the_preset(QualityPreset preset, int expectedDpi)
    {
        Assert.Equal(expectedDpi, PdfCommandBuilder.DpiFor(preset));
    }

    [Fact]
    public void Assembling_images_keeps_them_in_order_with_the_output_named_up_front()
    {
        var arguments = PdfCommandBuilder.ImagesToPdf(new[] { "1.jpg", "2.jpg", "3.jpg" }, "book.pdf").ToList();

        Assert.Equal("convert", arguments[0]);
        Assert.Equal("book.pdf", arguments[arguments.IndexOf("-o") + 1]);
        Assert.Equal(new[] { "1.jpg", "2.jpg", "3.jpg" }, arguments.Skip(arguments.IndexOf("book.pdf") + 1));
    }

    [Fact]
    public void Extracting_images_needs_nothing_but_the_input()
    {
        Assert.Equal(new[] { "extract", "in.pdf" }, PdfCommandBuilder.ExtractImages("in.pdf"));
    }

    [Fact]
    public void Passing_a_pdf_through_qpdf_unchanged_is_just_the_two_files()
    {
        Assert.Equal(new[] { "in.pdf", "out.pdf" }, PdfCommandBuilder.PassThrough("in.pdf", "out.pdf"));
    }

    [Fact]
    public void An_operation_the_builder_does_not_own_is_refused_rather_than_guessed_at()
    {
        Assert.Throws<ArgumentException>(() => PdfCommandBuilder.ToolFor("pdf.convert"));
        Assert.Throws<ArgumentException>(() => PdfCommandBuilder.ToolFor("image.heic-to-pdf"));
        Assert.Throws<ArgumentException>(() => PdfCommandBuilder.ToolFor("video.convert"));
    }
}
