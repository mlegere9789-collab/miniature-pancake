namespace MediaSuite.Core.Calculators;

/// <summary>The categories the Unit Converter covers.</summary>
public enum UnitCategory
{
    Length,
    Mass,
    Area,
    Volume,
    Temperature,
    Data,
    Speed,
}

/// <summary>
/// Length, mass, area, volume, temperature, data and speed conversion. Pure arithmetic —
/// this tool has no file to convert, so unlike every other module it never touches the
/// job queue or an external process at all.
/// </summary>
public static class UnitConverter
{
    /// <summary>
    /// A unit whose conversion is a straight multiplication against a category's base
    /// unit (metres for length, kilograms for mass, and so on). Temperature is the one
    /// category this does not cover, since Celsius/Fahrenheit/Kelvin also need an offset.
    /// </summary>
    private sealed record LinearUnit(string Id, UnitCategory Category, string DisplayName, double UnitsPerBase);

    private static readonly IReadOnlyList<LinearUnit> LinearUnits = new[]
    {
        // Length — base unit: metre.
        new LinearUnit("mm", UnitCategory.Length, "Millimetres", 1000),
        new LinearUnit("cm", UnitCategory.Length, "Centimetres", 100),
        new LinearUnit("m", UnitCategory.Length, "Metres", 1),
        new LinearUnit("km", UnitCategory.Length, "Kilometres", 0.001),
        new LinearUnit("in", UnitCategory.Length, "Inches", 39.3700787401575),
        new LinearUnit("ft", UnitCategory.Length, "Feet", 3.28083989501312),
        new LinearUnit("yd", UnitCategory.Length, "Yards", 1.09361329833771),
        new LinearUnit("mi", UnitCategory.Length, "Miles", 0.000621371192237334),

        // Mass — base unit: kilogram.
        new LinearUnit("mg", UnitCategory.Mass, "Milligrams", 1_000_000),
        new LinearUnit("g", UnitCategory.Mass, "Grams", 1000),
        new LinearUnit("kg", UnitCategory.Mass, "Kilograms", 1),
        new LinearUnit("t", UnitCategory.Mass, "Tonnes", 0.001),
        new LinearUnit("oz", UnitCategory.Mass, "Ounces", 35.27396194958),
        new LinearUnit("lb", UnitCategory.Mass, "Pounds", 2.20462262184878),

        // Area — base unit: square metre.
        new LinearUnit("m2", UnitCategory.Area, "Square metres", 1),
        new LinearUnit("km2", UnitCategory.Area, "Square kilometres", 0.000001),
        new LinearUnit("ft2", UnitCategory.Area, "Square feet", 10.7639104167097),
        new LinearUnit("acre", UnitCategory.Area, "Acres", 0.000247105381467165),
        new LinearUnit("hectare", UnitCategory.Area, "Hectares", 0.0001),

        // Volume — base unit: litre.
        new LinearUnit("ml", UnitCategory.Volume, "Millilitres", 1000),
        new LinearUnit("l", UnitCategory.Volume, "Litres", 1),
        new LinearUnit("m3", UnitCategory.Volume, "Cubic metres", 0.001),
        new LinearUnit("gal", UnitCategory.Volume, "Gallons (US)", 0.264172052358148),
        new LinearUnit("qt", UnitCategory.Volume, "Quarts (US)", 1.05668820943912),
        new LinearUnit("floz", UnitCategory.Volume, "Fluid ounces (US)", 33.8140227018538),

        // Data — base unit: byte. Binary steps (1024), matching how Windows itself and
        // most everyday tools report file sizes, rather than the SI-decimal (1000)
        // definition that "kilobyte" strictly means.
        new LinearUnit("bit", UnitCategory.Data, "Bits", 8),
        new LinearUnit("byte", UnitCategory.Data, "Bytes", 1),
        new LinearUnit("kb", UnitCategory.Data, "Kilobytes", 1.0 / 1024),
        new LinearUnit("mb", UnitCategory.Data, "Megabytes", 1.0 / (1024 * 1024)),
        new LinearUnit("gb", UnitCategory.Data, "Gigabytes", 1.0 / (1024 * 1024 * 1024)),
        new LinearUnit("tb", UnitCategory.Data, "Terabytes", 1.0 / (1024d * 1024 * 1024 * 1024)),

        // Speed — base unit: metres per second.
        new LinearUnit("mps", UnitCategory.Speed, "Metres per second", 1),
        new LinearUnit("kmh", UnitCategory.Speed, "Kilometres per hour", 3.6),
        new LinearUnit("mph", UnitCategory.Speed, "Miles per hour", 2.23693629205440),
        new LinearUnit("knot", UnitCategory.Speed, "Knots", 1.94384449244057),
        new LinearUnit("ftps", UnitCategory.Speed, "Feet per second", 3.28083989501312),
    };

    private static readonly IReadOnlySet<string> TemperatureUnits =
        new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "celsius", "fahrenheit", "kelvin" };

    /// <summary>Converts a value from one unit to another. Both must be the same category.</summary>
    public static double Convert(double value, string fromUnit, string toUnit)
    {
        if (IsTemperatureUnit(fromUnit) || IsTemperatureUnit(toUnit))
        {
            return ConvertTemperature(value, fromUnit, toUnit);
        }

        var from = FindLinear(fromUnit);
        var to = FindLinear(toUnit);

        if (from.Category != to.Category)
        {
            throw new ArgumentException($"'{fromUnit}' and '{toUnit}' are not the same kind of unit.");
        }

        var baseValue = value / from.UnitsPerBase;
        return baseValue * to.UnitsPerBase;
    }

    /// <summary>Every unit id in a category, for populating a picker.</summary>
    public static IReadOnlyList<string> UnitsIn(UnitCategory category) =>
        category == UnitCategory.Temperature
            ? new[] { "celsius", "fahrenheit", "kelvin" }
            : LinearUnits.Where(u => u.Category == category).Select(u => u.Id).ToList();

    /// <summary>Which category a unit id belongs to.</summary>
    public static UnitCategory CategoryOf(string unitId) =>
        IsTemperatureUnit(unitId) ? UnitCategory.Temperature : FindLinear(unitId).Category;

    private static bool IsTemperatureUnit(string unitId) => TemperatureUnits.Contains(unitId);

    private static LinearUnit FindLinear(string unitId) =>
        LinearUnits.FirstOrDefault(u => string.Equals(u.Id, unitId, StringComparison.OrdinalIgnoreCase))
        ?? throw new ArgumentException($"'{unitId}' is not a known unit.", nameof(unitId));

    /// <summary>
    /// Temperature needs an offset as well as a scale, so it cannot share the linear-unit
    /// table above — Celsius to Fahrenheit is not a plain multiplication.
    /// </summary>
    private static double ConvertTemperature(double value, string fromUnit, string toUnit)
    {
        if (!IsTemperatureUnit(fromUnit) || !IsTemperatureUnit(toUnit))
        {
            throw new ArgumentException($"'{fromUnit}' and '{toUnit}' are not the same kind of unit.");
        }

        var celsius = fromUnit.ToLowerInvariant() switch
        {
            "celsius" => value,
            "fahrenheit" => (value - 32) * 5.0 / 9.0,
            "kelvin" => value - 273.15,
            _ => throw new ArgumentException($"'{fromUnit}' is not a known temperature unit.", nameof(fromUnit)),
        };

        return toUnit.ToLowerInvariant() switch
        {
            "celsius" => celsius,
            "fahrenheit" => celsius * 9.0 / 5.0 + 32,
            "kelvin" => celsius + 273.15,
            _ => throw new ArgumentException($"'{toUnit}' is not a known temperature unit.", nameof(toUnit)),
        };
    }
}
