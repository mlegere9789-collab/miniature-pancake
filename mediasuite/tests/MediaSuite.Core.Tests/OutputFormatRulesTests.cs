using MediaSuite.Core.Features;
using Xunit;

namespace MediaSuite.Core.Tests;

public class OutputFormatRulesTests
{
    [Theory]
    [InlineData("image.heic-to-jpg", "jpg")]
    [InlineData("image.webp-to-png", "png")]
    [InlineData("video.mp4-to-mp3", "mp3")]
    [InlineData("video.mov-to-mp4", "mp4")]
    [InlineData("audio.mp3-to-ogg", "ogg")]
    [InlineData("audio.compress.wav", "wav")]
    [InlineData("gif.from-video", "gif")]
    [InlineData("gif.compress", "gif")]
    [InlineData("gif.maker", "gif")]
    [InlineData("gif.to-mp4", "mp4")]
    [InlineData("gif.to-apng", "apng")]
    [InlineData("pdf.merge", "pdf")]
    [InlineData("pdf.rotate", "pdf")]
    [InlineData("pdf.split", "pdf")]
    [InlineData("pdf.to-word", "docx")]
    [InlineData("pdf.to-jpg", "jpg")]
    [InlineData("image.heic-to-pdf", "pdf")]
    [InlineData("image.jpg-to-pdf", "pdf")]
    [InlineData("pdf.to-epub", "epub")]
    [InlineData("ebook.epub-to-pdf", "pdf")]
    [InlineData("document.docx-to-pdf", "pdf")]
    public void A_tool_named_for_one_format_is_not_offered_a_choice(string operationId, string expected)
    {
        Assert.Equal(expected, OutputFormatRules.ForcedFormat(operationId));
    }

    [Theory]
    [InlineData("image.convert")]
    [InlineData("video.convert")]
    [InlineData("audio.convert")]
    [InlineData("pdf.convert")]
    [InlineData("document.convert")]
    [InlineData("ebook.convert")]
    [InlineData("archive.convert")]
    public void Open_ended_tools_leave_the_format_to_the_user(string operationId)
    {
        Assert.Null(OutputFormatRules.ForcedFormat(operationId));
    }

    [Fact]
    public void Extracting_images_leaves_the_format_to_whatever_was_embedded()
    {
        // A PDF's embedded images can be JPEG or PNG or something else entirely; the tool
        // does not force one, unlike every other PDF operation.
        Assert.Null(OutputFormatRules.ForcedFormat("pdf.extract-images"));
    }

    [Theory]
    [InlineData("image.resize")]
    [InlineData("image.rotate")]
    [InlineData("video.compress")]
    [InlineData("video.trim")]
    [InlineData("video.crop")]
    public void Edit_in_place_tools_keep_whatever_came_in(string operationId)
    {
        Assert.True(OutputFormatRules.KeepsSourceFormat(operationId));
    }

    [Theory]
    [InlineData("image.convert")]
    [InlineData("video.mp4-to-mp3")]
    [InlineData("audio.mp3-to-ogg")]
    [InlineData("gif.compress")]
    public void Conversions_do_not_keep_the_source_format(string operationId)
    {
        Assert.False(OutputFormatRules.KeepsSourceFormat(operationId));
    }
}
