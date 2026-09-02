using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class InputCollectorTests
{
    [Fact]
    public void Files_are_returned_in_drop_order()
    {
        using var temp = new TempDirectory();
        var first = temp.CreateFile("a.jpg");
        var second = temp.CreateFile("b.png");

        var files = InputCollector.Expand(new[] { first, second });

        Assert.Equal(new[] { first, second }, files);
    }

    [Fact]
    public void Folders_are_expanded_recursively()
    {
        using var temp = new TempDirectory();
        temp.CreateFile("photos", "one.jpg");
        temp.CreateFile("photos", "nested", "two.jpg");

        var files = InputCollector.Expand(new[] { temp.Combine("photos") });

        Assert.Equal(2, files.Count);
    }

    [Fact]
    public void Folders_can_be_expanded_without_recursing()
    {
        using var temp = new TempDirectory();
        temp.CreateFile("photos", "one.jpg");
        temp.CreateFile("photos", "nested", "two.jpg");

        var files = InputCollector.Expand(new[] { temp.Combine("photos") }, recurse: false);

        Assert.Single(files);
    }

    [Fact]
    public void The_same_file_dropped_twice_is_only_counted_once()
    {
        using var temp = new TempDirectory();
        var file = temp.CreateFile("photos", "one.jpg");

        var files = InputCollector.Expand(new[] { file, temp.Combine("photos"), file });

        Assert.Single(files);
    }

    [Fact]
    public void Missing_paths_null_and_blanks_are_skipped()
    {
        using var temp = new TempDirectory();
        var real = temp.CreateFile("real.jpg");

        var files = InputCollector.Expand(new string?[] { null, "  ", temp.Combine("ghost.jpg"), real });

        Assert.Equal(new[] { real }, files);
    }

    [Fact]
    public void A_null_drop_produces_an_empty_list_rather_than_throwing()
    {
        Assert.Empty(InputCollector.Expand(null));
    }
}
