using MediaSuite.Core.Calculators;
using Xunit;

namespace MediaSuite.Core.Tests;

public class TimeConverterTests
{
    // --- Time zones --------------------------------------------------------------------

    [Fact]
    public void Converting_to_utc_never_moves_the_instant_only_its_label()
    {
        var instant = new DateTimeOffset(2026, 6, 1, 12, 0, 0, TimeSpan.FromHours(5));

        var converted = TimeConverter.ConvertTimeZone(instant, TimeZoneInfo.Utc);

        Assert.Equal(instant.ToUniversalTime(), converted.ToUniversalTime());
        Assert.Equal(TimeSpan.Zero, converted.Offset);
    }

    [Fact]
    public void A_fixed_offset_zone_shifts_the_clock_time_by_exactly_its_offset()
    {
        // A custom zone rather than a named system one, so this does not depend on the
        // machine's own time zone database at all.
        var instant = new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);
        var plusFive = TimeZoneInfo.CreateCustomTimeZone("Test+5", TimeSpan.FromHours(5), "Test+5", "Test+5");

        var converted = TimeConverter.ConvertTimeZone(instant, plusFive);

        Assert.Equal(new DateTime(2026, 1, 1, 5, 0, 0), converted.DateTime);
        Assert.Equal(TimeSpan.FromHours(5), converted.Offset);
    }

    [Fact]
    public void Converting_by_the_utc_id_resolves_without_any_system_time_zone_data()
    {
        var instant = new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);

        var converted = TimeConverter.ConvertTimeZone(instant, "UTC");

        Assert.Equal(TimeSpan.Zero, converted.Offset);
    }

    // --- Unix timestamps -----------------------------------------------------------------

    [Fact]
    public void The_unix_epoch_is_zero()
    {
        var epoch = new DateTimeOffset(1970, 1, 1, 0, 0, 0, TimeSpan.Zero);

        Assert.Equal(0, TimeConverter.ToUnixSeconds(epoch));
        Assert.Equal(0, TimeConverter.ToUnixMilliseconds(epoch));
    }

    [Fact]
    public void Timestamp_conversion_round_trips()
    {
        var instant = new DateTimeOffset(2026, 8, 29, 3, 30, 15, TimeSpan.Zero);

        var seconds = TimeConverter.ToUnixSeconds(instant);
        Assert.Equal(instant, TimeConverter.FromUnixSeconds(seconds));

        var milliseconds = TimeConverter.ToUnixMilliseconds(instant);
        Assert.Equal(instant, TimeConverter.FromUnixMilliseconds(milliseconds));
    }

    // --- Durations -------------------------------------------------------------------------

    [Theory]
    [InlineData(1, "day", "hour", 24)]
    [InlineData(1, "hour", "min", 60)]
    [InlineData(1, "min", "s", 60)]
    [InlineData(1, "s", "ms", 1000)]
    [InlineData(3600, "s", "hour", 1)]
    public void Duration_units_convert_through_seconds(double value, string from, string to, double expected)
    {
        Assert.Equal(expected, TimeConverter.ConvertDuration(value, from, to), 6);
    }

    [Fact]
    public void An_unknown_duration_unit_is_refused()
    {
        Assert.Throws<ArgumentException>(() => TimeConverter.ConvertDuration(1, "fortnight", "s"));
    }

    [Fact]
    public void Every_duration_unit_id_is_usable_in_both_directions()
    {
        foreach (var unit in TimeConverter.DurationUnitIds)
        {
            Assert.Equal(1, TimeConverter.ConvertDuration(1, unit, unit), 6);
        }
    }

    // --- Frame counts ----------------------------------------------------------------------

    [Theory]
    [InlineData(1, 24, 24)]
    [InlineData(1, 25, 25)]
    [InlineData(1, 29.97, 30)]
    [InlineData(10, 23.976, 240)]
    public void Seconds_convert_to_the_nearest_whole_frame(double seconds, double fps, long expectedFrames)
    {
        Assert.Equal(expectedFrames, TimeConverter.SecondsToFrames(seconds, fps));
    }

    [Fact]
    public void Frames_convert_back_to_seconds()
    {
        Assert.Equal(1.0, TimeConverter.FramesToSeconds(24, 24), 9);
        Assert.Equal(2.0, TimeConverter.FramesToSeconds(48, 24), 9);
    }

    [Fact]
    public void A_frame_rate_of_zero_or_less_is_refused()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => TimeConverter.SecondsToFrames(1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => TimeConverter.FramesToSeconds(1, -1));
    }
}
