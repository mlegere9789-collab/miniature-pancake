using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class OutputPathResolverTests : IDisposable
{
    private readonly TempDirectory _temp = new();

    public void Dispose() => _temp.Dispose();

    private OutputTarget Target(
        string? format = "png",
        string template = "{name}.{ext}",
        OverwritePolicy policy = OverwritePolicy.Rename,
        bool preserveStructure = false) => new()
    {
        Directory = _temp.Combine("out"),
        Format = format,
        FileNameTemplate = template,
        OverwritePolicy = policy,
        PreserveFolderStructure = preserveStructure,
    };

    [Fact]
    public void The_output_keeps_the_input_name_and_takes_the_target_extension()
    {
        var resolved = OutputPathResolver.Resolve(_temp.CreateFile("holiday.jpg"), Target());

        Assert.Equal("holiday.png", Path.GetFileName(resolved));
        Assert.Equal(_temp.Combine("out"), Path.GetDirectoryName(resolved));
    }

    [Fact]
    public void The_output_folder_is_created()
    {
        OutputPathResolver.Resolve(_temp.CreateFile("a.jpg"), Target());

        Assert.True(Directory.Exists(_temp.Combine("out")));
    }

    [Fact]
    public void With_no_target_format_the_input_extension_is_kept()
    {
        var resolved = OutputPathResolver.Resolve(_temp.CreateFile("clip.mp4"), Target(format: null));

        Assert.Equal("clip.mp4", Path.GetFileName(resolved));
    }

    [Theory]
    [InlineData("{name}.{ext}", "holiday.png")]
    [InlineData("{name}-converted.{ext}", "holiday-converted.png")]
    [InlineData("{index}-{name}.{ext}", "7-holiday.png")]
    [InlineData("photo-{index}.{ext}", "photo-7.png")]
    public void The_name_template_is_applied(string template, string expected)
    {
        var resolved = OutputPathResolver.Resolve(
            _temp.CreateFile("holiday.jpg"), Target(template: template), index: 7);

        Assert.Equal(expected, Path.GetFileName(resolved));
    }

    [Fact]
    public void Rename_is_the_default_and_never_destroys_an_existing_file()
    {
        var input = _temp.CreateFile("holiday.jpg");
        var existing = _temp.CreateFile("out", "holiday.png");
        File.WriteAllText(existing, "the original");

        var resolved = OutputPathResolver.Resolve(input, Target());

        Assert.Equal("holiday (1).png", Path.GetFileName(resolved));
        Assert.Equal("the original", File.ReadAllText(existing));
    }

    [Fact]
    public void Two_jobs_resolving_the_same_name_before_either_writes_its_file_do_not_collide()
    {
        // The real scenario this guards against: JobQueueManager runs several jobs from the
        // same batch concurrently, and each one calls Resolve() itself, right as it starts --
        // not once up front for the whole batch. Two inputs that would produce the same
        // output name (e.g. same filename from different subfolders, or two different
        // extensions converting to the same target extension) can both ask for a name before
        // either has actually written anything to disk. A plain File.Exists check alone would
        // hand out the identical path to both -- this simulates exactly that race
        // deterministically, without needing real threads: call Resolve() twice back to back
        // for the same candidate name with no file ever created in between.
        var firstInput = _temp.CreateFile("song.wav");
        var secondInput = _temp.CreateFile("song.flac");
        var target = Target(template: "song.{ext}");

        var first = OutputPathResolver.Resolve(firstInput, target);
        var second = OutputPathResolver.Resolve(secondInput, target);

        Assert.NotEqual(first, second);
        Assert.Equal("song.png", Path.GetFileName(first));
        Assert.Equal("song (1).png", Path.GetFileName(second));
    }

    [Fact]
    public void A_third_concurrent_resolution_keeps_counting_past_a_reserved_name_too()
    {
        var target = Target(template: "song.{ext}");

        var first = OutputPathResolver.Resolve(_temp.CreateFile("a.wav"), target);
        var second = OutputPathResolver.Resolve(_temp.CreateFile("b.flac"), target);
        var third = OutputPathResolver.Resolve(_temp.CreateFile("c.ogg"), target);

        Assert.Equal(3, new[] { first, second, third }.Distinct().Count());
    }

    [Fact]
    public void Overwrite_lets_two_concurrent_resolutions_share_the_same_path()
    {
        // Overwrite is a deliberate "last write wins" choice, unlike the default Rename --
        // two jobs colliding under it is the user's own call, not something to guard against.
        var target = Target(template: "song.{ext}", policy: OverwritePolicy.Overwrite);

        var first = OutputPathResolver.Resolve(_temp.CreateFile("a.wav"), target);
        var second = OutputPathResolver.Resolve(_temp.CreateFile("b.flac"), target);

        Assert.Equal(first, second);
    }

    [Fact]
    public void Rename_keeps_counting_past_the_first_collision()
    {
        var input = _temp.CreateFile("holiday.jpg");
        _temp.CreateFile("out", "holiday.png");
        _temp.CreateFile("out", "holiday (1).png");

        var resolved = OutputPathResolver.Resolve(input, Target());

        Assert.Equal("holiday (2).png", Path.GetFileName(resolved));
    }

    [Fact]
    public void Overwrite_returns_the_original_path()
    {
        var input = _temp.CreateFile("holiday.jpg");
        var existing = _temp.CreateFile("out", "holiday.png");

        var resolved = OutputPathResolver.Resolve(input, Target(policy: OverwritePolicy.Overwrite));

        Assert.Equal(existing, resolved);
    }

    [Fact]
    public void Fail_refuses_rather_than_touching_an_existing_file()
    {
        var input = _temp.CreateFile("holiday.jpg");
        _temp.CreateFile("out", "holiday.png");

        Assert.Throws<IOException>(() =>
            OutputPathResolver.Resolve(input, Target(policy: OverwritePolicy.Fail)));
    }

    [Fact]
    public void Preserving_the_folder_structure_rebuilds_the_source_tree()
    {
        var first = _temp.CreateFile("photos", "2024", "one.jpg");
        var second = _temp.CreateFile("photos", "2025", "trip", "two.jpg");
        var root = OutputPathResolver.FindCommonRoot(new[] { first, second });

        var resolvedFirst = OutputPathResolver.Resolve(first, Target(preserveStructure: true), 1, root);
        var resolvedSecond = OutputPathResolver.Resolve(second, Target(preserveStructure: true), 2, root);

        Assert.Equal(Path.Combine(_temp.Combine("out"), "2024"), Path.GetDirectoryName(resolvedFirst));
        Assert.Equal(Path.Combine(_temp.Combine("out"), "2025", "trip"), Path.GetDirectoryName(resolvedSecond));
    }

    [Fact]
    public void Without_the_option_a_nested_batch_is_flattened()
    {
        var nested = _temp.CreateFile("photos", "2024", "one.jpg");
        var root = _temp.Combine("photos");

        var resolved = OutputPathResolver.Resolve(nested, Target(), 1, root);

        Assert.Equal(_temp.Combine("out"), Path.GetDirectoryName(resolved));
    }

    [Fact]
    public void FindCommonRoot_returns_the_deepest_shared_folder()
    {
        var first = _temp.CreateFile("photos", "2024", "one.jpg");
        var second = _temp.CreateFile("photos", "2024", "two.jpg");

        Assert.Equal(_temp.Combine("photos", "2024"), OutputPathResolver.FindCommonRoot(new[] { first, second }));
    }

    [Fact]
    public void FindCommonRoot_of_a_single_file_is_its_own_folder()
    {
        var only = _temp.CreateFile("photos", "one.jpg");

        Assert.Equal(_temp.Combine("photos"), OutputPathResolver.FindCommonRoot(new[] { only }));
    }

    [Fact]
    public void FindCommonRoot_of_nothing_is_null()
    {
        Assert.Null(OutputPathResolver.FindCommonRoot(Array.Empty<string>()));
    }

    [Fact]
    public void A_template_that_would_produce_an_illegal_name_is_sanitised()
    {
        var resolved = OutputPathResolver.Resolve(
            _temp.CreateFile("holiday.jpg"), Target(template: "we:re/here?.{ext}"));

        // Checked against the platform's own rules rather than a hard-coded character
        // list, since what counts as illegal differs between Windows and Unix.
        var name = Path.GetFileName(resolved);
        Assert.All(Path.GetInvalidFileNameChars(), invalid => Assert.DoesNotContain(invalid, name));
        Assert.EndsWith(".png", name, StringComparison.Ordinal);
    }
}
