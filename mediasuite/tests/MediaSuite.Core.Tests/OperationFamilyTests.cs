using MediaSuite.Core.Features;
using MediaSuite.Core.Formats;
using Xunit;

namespace MediaSuite.Core.Tests;

public class OperationFamilyTests
{
    [Theory]
    [InlineData("pdf.convert", MediaKind.Pdf)]
    [InlineData("image.convert", MediaKind.Image)]
    [InlineData("video.convert", MediaKind.Video)]
    [InlineData("audio.convert", MediaKind.Audio)]
    [InlineData("gif.from-video", MediaKind.Animation)]
    public void The_family_prefix_decides_the_output_kind(string operationId, MediaKind expected)
    {
        Assert.Equal(expected, OperationFamily.OutputKindFor(operationId));
    }

    [Fact]
    public void The_pdf_converter_can_write_pdf_or_rasterise_to_jpg_or_png()
    {
        var extensions = OperationFamily.OutputFormatsFor("pdf.convert").Select(f => f.Extension).ToList();

        Assert.Contains("pdf", extensions);
        Assert.Contains("jpg", extensions);
        Assert.Contains("png", extensions);

        // Only the two raster formats the PDF module actually renders to — not every
        // writable image format there is.
        Assert.DoesNotContain("webp", extensions);
        Assert.DoesNotContain("bmp", extensions);
    }

    [Fact]
    public void The_image_converter_still_offers_only_images_and_vectors()
    {
        var extensions = OperationFamily.OutputFormatsFor("image.convert").Select(f => f.Extension).ToList();

        Assert.Contains("png", extensions);
        Assert.Contains("svg", extensions);
        Assert.DoesNotContain("pdf", extensions);
    }

    [Fact]
    public void An_unrecognised_operation_offers_nothing()
    {
        Assert.Empty(OperationFamily.OutputFormatsFor("nonsense.operation"));
    }

    [Fact]
    public void The_audio_converter_offers_ac3_but_not_the_read_only_formats()
    {
        var extensions = OperationFamily.OutputFormatsFor("audio.convert").Select(f => f.Extension).ToList();

        Assert.Contains("ac3", extensions);
        Assert.DoesNotContain("amr", extensions);
    }

    [Fact]
    public void The_video_converter_does_not_offer_the_read_only_source_formats()
    {
        var extensions = OperationFamily.OutputFormatsFor("video.convert").Select(f => f.Extension).ToList();

        Assert.DoesNotContain("vob", extensions);
        Assert.DoesNotContain("f4v", extensions);
        Assert.DoesNotContain("qt", extensions);
    }
}
