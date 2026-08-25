namespace MediaSuite.Core.Jobs;

/// <summary>Hands each job a private scratch folder.</summary>
public interface ITempWorkspaceFactory
{
    /// <summary>Creates and returns an empty workspace for the given job.</summary>
    TempWorkspace Create(Guid jobId);
}
