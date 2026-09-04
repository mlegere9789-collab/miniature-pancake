using System.Globalization;
using System.Windows;
using MediaSuite.App.Converters;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// Needs only the <see cref="Visibility"/>/<see cref="System.Windows.Data.IValueConverter"/>
/// types, not a live WPF Window, so this is exercised directly rather than through a real
/// binding.
/// </summary>
public class BoolToVisibilityConverterTests
{
    private static readonly BoolToVisibilityConverter Converter = new();

    [Theory]
    [InlineData(true, Visibility.Visible)]
    [InlineData(false, Visibility.Collapsed)]
    public void Convert_maps_bool_to_visibility(bool value, Visibility expected)
    {
        Assert.Equal(expected, Converter.Convert(value, typeof(Visibility), null, CultureInfo.InvariantCulture));
    }

    [Theory]
    [InlineData(true, Visibility.Collapsed)]
    [InlineData(false, Visibility.Visible)]
    public void Convert_with_Invert_parameter_flips_the_result(bool value, Visibility expected)
    {
        Assert.Equal(expected, Converter.Convert(value, typeof(Visibility), "Invert", CultureInfo.InvariantCulture));
    }

    [Fact]
    public void Convert_matches_Invert_case_insensitively()
    {
        Assert.Equal(Visibility.Collapsed, Converter.Convert(true, typeof(Visibility), "invert", CultureInfo.InvariantCulture));
        Assert.Equal(Visibility.Collapsed, Converter.Convert(true, typeof(Visibility), "INVERT", CultureInfo.InvariantCulture));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("not-invert")]
    [InlineData("")]
    public void Convert_treats_any_non_Invert_parameter_as_no_flip(object? parameter)
    {
        Assert.Equal(Visibility.Visible, Converter.Convert(true, typeof(Visibility), parameter, CultureInfo.InvariantCulture));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("not a bool")]
    [InlineData(0)]
    public void Convert_treats_anything_that_is_not_literally_true_as_false(object? value)
    {
        Assert.Equal(Visibility.Collapsed, Converter.Convert(value, typeof(Visibility), null, CultureInfo.InvariantCulture));
    }

    [Theory]
    [InlineData(Visibility.Visible, true)]
    [InlineData(Visibility.Collapsed, false)]
    [InlineData(Visibility.Hidden, false)]
    public void ConvertBack_maps_visibility_to_bool(Visibility value, bool expected)
    {
        Assert.Equal(expected, Converter.ConvertBack(value, typeof(bool), null, CultureInfo.InvariantCulture));
    }

    [Theory]
    [InlineData(Visibility.Visible, false)]
    [InlineData(Visibility.Collapsed, true)]
    public void ConvertBack_with_Invert_parameter_flips_the_result_symmetrically_with_Convert(Visibility value, bool expected)
    {
        // Convert(true, "Invert") -> Collapsed, so ConvertBack(Collapsed, "Invert") must round-trip back to true.
        Assert.Equal(expected, Converter.ConvertBack(value, typeof(bool), "Invert", CultureInfo.InvariantCulture));
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void Convert_and_ConvertBack_round_trip_through_Invert(bool original)
    {
        var visibility = Converter.Convert(original, typeof(Visibility), "Invert", CultureInfo.InvariantCulture);
        var roundTripped = Converter.ConvertBack(visibility, typeof(bool), "Invert", CultureInfo.InvariantCulture);

        Assert.Equal(original, roundTripped);
    }
}
