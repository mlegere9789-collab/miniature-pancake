using MediaSuite.Core.Engines;
using Xunit;

namespace MediaSuite.Core.Tests;

public class UpscaleCommandBuilderTests
{
    [Theory]
    [InlineData("general", false, "realesr-general-x4v3")]
    [InlineData("general", true, "realesr-general-wdn-x4v3")]
    [InlineData("anime", false, "realesrgan-x4plus-anime")]
    [InlineData("anime", true, "realesrgan-x4plus-anime")]
    [InlineData("ANIME", false, "realesrgan-x4plus-anime")]
    public void Model_names_match_the_real_ncnn_vulkan_release_files(string model, bool denoise, string expected)
    {
        Assert.Equal(expected, UpscaleCommandBuilder.ModelNameFor(model, denoise));
    }

    [Fact]
    public void Face_enhance_is_not_a_model_this_builder_offers()
    {
        // There is no face-restoration model in the bundled release; asking for one
        // should fail loudly rather than silently falling back to the general model.
        Assert.Throws<ArgumentException>(() => UpscaleCommandBuilder.ModelNameFor("face-enhance", false));
    }

    [Theory]
    [InlineData(2, new[] { 2 })]
    [InlineData(4, new[] { 4 })]
    [InlineData(8, new[] { 4, 2 })]
    public void Scales_map_to_the_passes_that_reach_them(int totalScale, int[] expectedPasses)
    {
        Assert.Equal(expectedPasses, UpscaleCommandBuilder.PassScalesFor(totalScale));
    }

    [Fact]
    public void An_eight_x_request_is_two_passes_because_no_model_is_natively_eight_x()
    {
        var passes = UpscaleCommandBuilder.PassScalesFor(8);

        // 4 * 2 = 8, using scale values every release accepts, rather than trusting
        // every build to take "-s 8" directly.
        Assert.Equal(8, passes.Aggregate(1, (product, pass) => product * pass));
    }

    [Fact]
    public void An_unsupported_scale_is_refused()
    {
        Assert.Throws<ArgumentException>(() => UpscaleCommandBuilder.PassScalesFor(3));
        Assert.Throws<ArgumentException>(() => UpscaleCommandBuilder.PassScalesFor(16));
    }

    [Fact]
    public void Building_a_pass_names_the_model_folder_explicitly()
    {
        var arguments = UpscaleCommandBuilder.Build(
            "in.png", "out.png", 4, "realesr-general-x4v3", @"C:\tools\realesrgan\models", "png", forceCpu: false).ToList();

        Assert.Equal("in.png", arguments[arguments.IndexOf("-i") + 1]);
        Assert.Equal("out.png", arguments[arguments.IndexOf("-o") + 1]);
        Assert.Equal("4", arguments[arguments.IndexOf("-s") + 1]);
        Assert.Equal("realesr-general-x4v3", arguments[arguments.IndexOf("-n") + 1]);
        Assert.Equal(@"C:\tools\realesrgan\models", arguments[arguments.IndexOf("-m") + 1]);
        Assert.Equal("png", arguments[arguments.IndexOf("-f") + 1]);
        Assert.DoesNotContain("-g", arguments);
    }

    [Fact]
    public void Forcing_cpu_asks_for_gpu_device_minus_one()
    {
        var arguments = UpscaleCommandBuilder.Build(
            "in.png", "out.png", 4, "realesr-general-x4v3", "models", "png", forceCpu: true);

        var gpuIndex = arguments.ToList().IndexOf("-g");
        Assert.True(gpuIndex >= 0);
        Assert.Equal("-1", arguments[gpuIndex + 1]);
    }

    [Fact]
    public void Sharpening_is_an_ordinary_imagemagick_unsharp_call()
    {
        var arguments = UpscaleCommandBuilder.Sharpen("upscaled.png", "final.png");

        Assert.Equal(new[] { "upscaled.png", "-unsharp", "0x1", "final.png" }, arguments);
    }

    [Fact]
    public void Face_enhance_mirrors_the_upscaler_pass_its_own_flag_shapes()
    {
        var arguments = UpscaleCommandBuilder.FaceEnhance("upscaled.png", "final.png", @"C:\tools\gfpgan\models");

        Assert.Equal(
            new[] { "-i", "upscaled.png", "-o", "final.png", "-m", @"C:\tools\gfpgan\models" },
            arguments);
    }
}
