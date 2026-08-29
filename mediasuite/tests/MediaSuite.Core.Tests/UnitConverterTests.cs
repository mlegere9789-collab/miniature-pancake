using MediaSuite.Core.Calculators;
using Xunit;

namespace MediaSuite.Core.Tests;

public class UnitConverterTests
{
    // --- Linear categories -----------------------------------------------------------------

    [Theory]
    [InlineData(1, "m", "cm", 100)]
    [InlineData(1, "km", "m", 1000)]
    [InlineData(1, "mi", "km", 1.609344)]
    [InlineData(12, "in", "ft", 1)]
    public void Length_converts_through_metres(double value, string from, string to, double expected)
    {
        Assert.Equal(expected, UnitConverter.Convert(value, from, to), 3);
    }

    [Theory]
    [InlineData(1, "kg", "g", 1000)]
    [InlineData(1000, "g", "kg", 1)]
    [InlineData(1, "lb", "oz", 16)]
    public void Mass_converts_through_kilograms(double value, string from, string to, double expected)
    {
        Assert.Equal(expected, UnitConverter.Convert(value, from, to), 2);
    }

    [Theory]
    [InlineData(1, "byte", "bit", 8)]
    [InlineData(1024, "byte", "kb", 1)]
    [InlineData(1, "mb", "kb", 1024)]
    [InlineData(1, "gb", "mb", 1024)]
    public void Data_uses_binary_1024_steps_not_the_si_decimal_ones(double value, string from, string to, double expected)
    {
        Assert.Equal(expected, UnitConverter.Convert(value, from, to), 6);
    }

    [Theory]
    [InlineData(1, "kmh", "mps", 0.277778)]
    [InlineData(60, "mph", "kmh", 96.5606)]
    public void Speed_converts_through_metres_per_second(double value, string from, string to, double expected)
    {
        Assert.Equal(expected, UnitConverter.Convert(value, from, to), 3);
    }

    [Fact]
    public void Converting_a_value_to_its_own_unit_is_a_no_op()
    {
        Assert.Equal(42, UnitConverter.Convert(42, "m", "m"), 9);
    }

    [Fact]
    public void Mixing_categories_is_refused_rather_than_producing_a_meaningless_number()
    {
        Assert.Throws<ArgumentException>(() => UnitConverter.Convert(1, "m", "kg"));
        Assert.Throws<ArgumentException>(() => UnitConverter.Convert(1, "l", "mph"));
    }

    [Fact]
    public void An_unknown_unit_is_refused()
    {
        Assert.Throws<ArgumentException>(() => UnitConverter.Convert(1, "furlong", "m"));
    }

    // --- Temperature -------------------------------------------------------------------

    [Theory]
    [InlineData(0, "celsius", "fahrenheit", 32)]
    [InlineData(100, "celsius", "fahrenheit", 212)]
    [InlineData(32, "fahrenheit", "celsius", 0)]
    [InlineData(0, "celsius", "kelvin", 273.15)]
    [InlineData(0, "kelvin", "celsius", -273.15)]
    [InlineData(-40, "fahrenheit", "celsius", -40)]
    public void Temperature_conversions_match_the_standard_formulas(double value, string from, string to, double expected)
    {
        Assert.Equal(expected, UnitConverter.Convert(value, from, to), 6);
    }

    [Fact]
    public void Temperature_cannot_be_mixed_with_a_linear_category()
    {
        Assert.Throws<ArgumentException>(() => UnitConverter.Convert(1, "celsius", "m"));
        Assert.Throws<ArgumentException>(() => UnitConverter.Convert(1, "kg", "fahrenheit"));
    }

    // --- Category lookups ----------------------------------------------------------------

    [Fact]
    public void Every_length_unit_reports_the_length_category()
    {
        foreach (var unit in UnitConverter.UnitsIn(UnitCategory.Length))
        {
            Assert.Equal(UnitCategory.Length, UnitConverter.CategoryOf(unit));
        }
    }

    [Fact]
    public void Temperature_units_are_not_in_the_linear_unit_list_but_still_resolve()
    {
        var units = UnitConverter.UnitsIn(UnitCategory.Temperature);

        Assert.Equal(new[] { "celsius", "fahrenheit", "kelvin" }, units);
        Assert.All(units, unit => Assert.Equal(UnitCategory.Temperature, UnitConverter.CategoryOf(unit)));
    }
}
