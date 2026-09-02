namespace MediaSuite.Core.Calculators;

/// <summary>
/// Time zones, Unix timestamps, duration units and frame counts. Pure arithmetic and
/// calendar math, like <see cref="UnitConverter"/> — nothing here touches a file or an
/// external process.
/// </summary>
public static class TimeConverter
{
    // --- Time zones -----------------------------------------------------------------------

    /// <summary>Re-expresses an instant in another time zone. The instant itself never moves.</summary>
    public static DateTimeOffset ConvertTimeZone(DateTimeOffset instant, TimeZoneInfo targetZone)
    {
        ArgumentNullException.ThrowIfNull(targetZone);
        return TimeZoneInfo.ConvertTime(instant, targetZone);
    }

    /// <summary>
    /// As above, but by the zone's id. .NET on Windows resolves both Windows zone ids
    /// ("Pacific Standard Time") and IANA ids ("America/Los_Angeles"); this only decides
    /// which family the picker should offer, not whether one works.
    /// </summary>
    public static DateTimeOffset ConvertTimeZone(DateTimeOffset instant, string targetZoneId) =>
        ConvertTimeZone(instant, TimeZoneInfo.FindSystemTimeZoneById(targetZoneId));

    // --- Unix timestamps --------------------------------------------------------------------

    public static long ToUnixSeconds(DateTimeOffset instant) => instant.ToUnixTimeSeconds();

    public static long ToUnixMilliseconds(DateTimeOffset instant) => instant.ToUnixTimeMilliseconds();

    public static DateTimeOffset FromUnixSeconds(long seconds) => DateTimeOffset.FromUnixTimeSeconds(seconds);

    public static DateTimeOffset FromUnixMilliseconds(long milliseconds) => DateTimeOffset.FromUnixTimeMilliseconds(milliseconds);

    // --- Durations -----------------------------------------------------------------------

    private sealed record DurationUnit(string Id, string DisplayName, double UnitsPerSecond);

    private static readonly IReadOnlyList<DurationUnit> DurationUnits = new[]
    {
        new DurationUnit("ms", "Milliseconds", 1000),
        new DurationUnit("s", "Seconds", 1),
        new DurationUnit("min", "Minutes", 1.0 / 60),
        new DurationUnit("hour", "Hours", 1.0 / 3600),
        new DurationUnit("day", "Days", 1.0 / 86400),
    };

    /// <summary>Every duration unit id, for populating a picker.</summary>
    public static IReadOnlyList<string> DurationUnitIds { get; } = DurationUnits.Select(u => u.Id).ToList();

    public static double ConvertDuration(double value, string fromUnit, string toUnit)
    {
        var from = FindDurationUnit(fromUnit);
        var to = FindDurationUnit(toUnit);

        var seconds = value / from.UnitsPerSecond;
        return seconds * to.UnitsPerSecond;
    }

    private static DurationUnit FindDurationUnit(string unitId) =>
        DurationUnits.FirstOrDefault(u => string.Equals(u.Id, unitId, StringComparison.OrdinalIgnoreCase))
        ?? throw new ArgumentException($"'{unitId}' is not a known duration unit.", nameof(unitId));

    // --- Frame counts ----------------------------------------------------------------------

    /// <summary>How many frames a duration holds at a given frame rate, rounded to the nearest whole frame.</summary>
    public static long SecondsToFrames(double seconds, double framesPerSecond)
    {
        if (framesPerSecond <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(framesPerSecond), "Frame rate must be greater than zero.");
        }

        return (long)Math.Round(seconds * framesPerSecond, MidpointRounding.AwayFromZero);
    }

    /// <summary>How long a frame count lasts at a given frame rate.</summary>
    public static double FramesToSeconds(long frames, double framesPerSecond)
    {
        if (framesPerSecond <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(framesPerSecond), "Frame rate must be greater than zero.");
        }

        return frames / framesPerSecond;
    }
}
