using System.Globalization;
using MediaSuite.Core.Engines;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ConcatListWriterTests
{
    private static string[] Lines(string list) =>
        list.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    [Fact]
    public void Each_image_gets_a_file_line_and_the_time_it_is_held_for()
    {
        var list = ConcatListWriter.Build(new[] { @"C:\pics\a.png", @"C:\pics\b.png" }, TimeSpan.FromMilliseconds(200));

        Assert.StartsWith(@"file 'C:\pics\a.png'", Lines(list)[0], StringComparison.Ordinal);
        Assert.Equal("duration 0.2", Lines(list)[1]);
    }

    [Fact]
    public void The_last_image_is_listed_twice()
    {
        // The concat demuxer applies a duration to the frame that follows it, so without
        // the repeat the final image would flash past in a single frame.
        var lines = Lines(ConcatListWriter.Build(new[] { "a.png", "b.png" }, TimeSpan.FromSeconds(1)));

        Assert.Equal(5, lines.Length);
        Assert.Equal("file 'b.png'", lines[^1]);
        Assert.Equal("file 'b.png'", lines[2]);
    }

    [Fact]
    public void An_apostrophe_in_a_folder_name_does_not_end_the_quoted_path()
    {
        var list = ConcatListWriter.Build(new[] { @"C:\Ben's photos\a.png" }, TimeSpan.FromSeconds(1));

        Assert.Contains(@"file 'C:\Ben'\''s photos\a.png'", list, StringComparison.Ordinal);
    }

    [Fact]
    public void The_duration_is_written_the_same_way_in_every_locale()
    {
        // A comma-decimal locale would otherwise write "duration 0,2", which FFmpeg reads
        // as 0 and turns a slideshow into a blur.
        var original = CultureInfo.CurrentCulture;

        try
        {
            CultureInfo.CurrentCulture = new CultureInfo("de-DE");
            var list = ConcatListWriter.Build(new[] { "a.png" }, TimeSpan.FromMilliseconds(200));

            Assert.Contains("duration 0.2", list, StringComparison.Ordinal);
            Assert.DoesNotContain("0,2", list, StringComparison.Ordinal);
        }
        finally
        {
            CultureInfo.CurrentCulture = original;
        }
    }

    [Fact]
    public void A_frame_is_never_held_for_no_time_at_all()
    {
        Assert.Contains("duration 0.01", ConcatListWriter.Build(new[] { "a.png" }, TimeSpan.Zero), StringComparison.Ordinal);
    }

    [Fact]
    public void There_is_nothing_to_build_from_no_images()
    {
        Assert.Throws<ArgumentException>(() => ConcatListWriter.Build(Array.Empty<string>(), TimeSpan.FromSeconds(1)));
        Assert.Throws<ArgumentNullException>(() => ConcatListWriter.Build(null!, TimeSpan.FromSeconds(1)));
    }
}
