namespace MediaSuite.Core.Jobs;

/// <summary>
/// The Quick / Balanced / Best / Custom preset system every tool exposes.
/// Engines translate the preset into their own parameters; <see cref="Custom"/>
/// means "ignore the preset and use the raw options on the job".
/// </summary>
public enum QualityPreset
{
    /// <summary>Fastest encode, smallest file, visible quality loss acceptable.</summary>
    Quick,

    /// <summary>Default. Good quality at a sensible speed and size.</summary>
    Balanced,

    /// <summary>Highest quality the format allows; slow and large.</summary>
    Best,

    /// <summary>Every parameter comes from the job's advanced options.</summary>
    Custom,
}
