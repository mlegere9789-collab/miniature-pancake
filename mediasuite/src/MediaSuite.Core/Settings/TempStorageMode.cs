namespace MediaSuite.Core.Settings;

/// <summary>Where intermediate files live while a job runs.</summary>
public enum TempStorageMode
{
    /// <summary>Default: a folder on disk, so a 10k-file batch cannot exhaust RAM.</summary>
    Disk,

    /// <summary>A user-supplied folder — point it at a RAM disk for maximum speed.</summary>
    CustomFolder,
}
