using MediaSuite.Core.Engines;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class DocumentCommandBuilderTests
{
    // --- Tool selection ------------------------------------------------------------------

    [Theory]
    [InlineData("docx", "odt")]
    [InlineData("odt", "html")]
    [InlineData("md", "docx")]
    [InlineData("txt", "rtf")]
    [InlineData("html", "md")]
    public void Ordinary_document_pairs_go_through_pandoc(string from, string to)
    {
        Assert.Equal(ExternalToolId.Pandoc, DocumentCommandBuilder.ToolForDocumentPair(from, to));
    }

    [Theory]
    [InlineData("doc", "docx")]
    [InlineData("docx", "doc")]
    [InlineData("doc", "doc")]
    [InlineData("doc", "odt")]
    public void The_legacy_doc_format_on_either_end_goes_through_libreoffice(string from, string to)
    {
        // Pandoc's DOCX reader only understands the modern OOXML container and has no DOC
        // reader or writer at all, so the old binary format needs LibreOffice's own filter.
        Assert.Equal(ExternalToolId.LibreOffice, DocumentCommandBuilder.ToolForDocumentPair(from, to));
    }

    // --- Pandoc ----------------------------------------------------------------------------

    [Theory]
    [InlineData("docx", "docx")]
    [InlineData("odt", "odt")]
    [InlineData("rtf", "rtf")]
    [InlineData("html", "html")]
    [InlineData("htm", "html")]
    [InlineData("md", "markdown")]
    [InlineData("txt", "markdown")]
    public void Pandoc_format_names_follow_pandocs_own_vocabulary(string extension, string pandocName)
    {
        Assert.Equal(pandocName, DocumentCommandBuilder.PandocFormatFor(extension));
    }

    [Fact]
    public void An_unknown_extension_is_refused_rather_than_guessed_at()
    {
        Assert.Throws<ArgumentException>(() => DocumentCommandBuilder.PandocFormatFor("pdf"));
        Assert.Throws<ArgumentException>(() => DocumentCommandBuilder.PandocFormatFor("epub"));
    }

    [Fact]
    public void Converting_names_the_input_and_output_formats_explicitly_and_writes_standalone()
    {
        var arguments = DocumentCommandBuilder.Convert("notes.md", "notes.docx", "md", "docx").ToList();

        Assert.Contains("--standalone", arguments);
        Assert.Equal("markdown", arguments[arguments.IndexOf("-f") + 1]);
        Assert.Equal("docx", arguments[arguments.IndexOf("-t") + 1]);
        Assert.Equal("notes.docx", arguments[arguments.IndexOf("-o") + 1]);
        Assert.Equal("notes.md", arguments[^1]);
    }

    // --- LibreOffice -------------------------------------------------------------------------

    [Fact]
    public void Libreoffice_takes_a_destination_folder_never_a_destination_file_name()
    {
        var arguments = DocumentCommandBuilder.ConvertViaLibreOffice("report.doc", @"C:\work", "pdf").ToList();

        Assert.Contains("--headless", arguments);
        Assert.Equal("pdf", arguments[arguments.IndexOf("--convert-to") + 1]);
        Assert.Equal(@"C:\work", arguments[arguments.IndexOf("--outdir") + 1]);
        Assert.Equal("report.doc", arguments[^1]);
    }

    // --- Calibre -------------------------------------------------------------------------

    [Fact]
    public void Ebook_conversion_is_just_the_two_files_the_format_comes_from_the_extension()
    {
        Assert.Equal(new[] { "book.epub", "book.mobi" }, DocumentCommandBuilder.ConvertEbook("book.epub", "book.mobi"));
    }
}
