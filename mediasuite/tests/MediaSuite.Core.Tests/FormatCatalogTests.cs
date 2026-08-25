using MediaSuite.Core.Formats;
using Xunit;

namespace MediaSuite.Core.Tests;

public class FormatCatalogTests
{
    [Theory]
    [InlineData("jpg")]
    [InlineData("JPG")]
    [InlineData(".jpg")]
    [InlineData("jpeg")]
    [InlineData(".JPEG")]
    public void FromExtension_normalizes_case_dots_and_aliases(string extension)
    {
        var format = FormatCatalog.FromExtension(extension);

        Assert.NotNull(format);
        Assert.Equal("jpg", format!.Extension);
        Assert.Equal(MediaKind.Image, format.Kind);
    }

    [Fact]
    public void FromExtension_returns_null_for_unknown_and_empty_input()
    {
        Assert.Null(FormatCatalog.FromExtension("not-a-real-format"));
        Assert.Null(FormatCatalog.FromExtension(""));
        Assert.Null(FormatCatalog.FromExtension(null));
    }

    [Fact]
    public void FromPath_uses_the_extension()
    {
        var format = FormatCatalog.FromPath(@"C:\photos\holiday\DSC_0001.NEF");

        Assert.NotNull(format);
        Assert.Equal(MediaKind.RawImage, format!.Kind);
    }

    [Fact]
    public void KindOf_falls_back_to_Unknown()
    {
        Assert.Equal(MediaKind.Unknown, FormatCatalog.KindOf("notes.qqq"));
        Assert.Equal(MediaKind.Video, FormatCatalog.KindOf("clip.mkv"));
    }

    [Fact]
    public void Raw_formats_are_read_only()
    {
        foreach (var format in FormatCatalog.OfKind(MediaKind.RawImage))
        {
            Assert.True(format.CanRead, $"{format.Extension} should be readable");
            Assert.False(format.CanWrite, $"{format.Extension} should not be writable: RAW is a capture format");
        }
    }

    [Fact]
    public void Rar_can_be_extracted_but_not_created()
    {
        var rar = FormatCatalog.FromExtension("rar");

        Assert.NotNull(rar);
        Assert.True(rar!.CanRead);
        Assert.False(rar.CanWrite);
    }

    [Fact]
    public void Every_extension_maps_to_exactly_one_format()
    {
        var duplicates = FormatCatalog.All
            .SelectMany(f => f.AllExtensions)
            .GroupBy(ext => ext, StringComparer.OrdinalIgnoreCase)
            .Where(group => group.Count() > 1)
            .Select(group => group.Key)
            .ToList();

        Assert.Empty(duplicates);
    }
}
