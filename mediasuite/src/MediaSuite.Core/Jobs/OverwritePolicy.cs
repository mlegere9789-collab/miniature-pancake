namespace MediaSuite.Core.Jobs;

/// <summary>What to do when the output file already exists.</summary>
public enum OverwritePolicy
{
    /// <summary>Append " (1)", " (2)" … until the name is free. Default.</summary>
    Rename,

    /// <summary>Replace the existing file.</summary>
    Overwrite,

    /// <summary>Fail the job rather than touch the existing file.</summary>
    Fail,
}
