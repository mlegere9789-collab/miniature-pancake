using MediaSuite.Core.Engines;

namespace MediaSuite.Core.Features;

/// <summary>
/// How an operation treats a multi-file selection.
/// </summary>
/// <remarks>
/// The queue's default is a job per file, so a batch runs in parallel and one failure does
/// not take the rest with it. A few tools break that rule because their whole point is to
/// merge — a GIF built from a folder of stills is one animation, not twenty. Later build
/// steps add PDF merge and archive creation to the same list.
/// </remarks>
public static class OperationInputRules
{
    /// <summary>True when every selected file belongs to a single job producing one output.</summary>
    public static bool CombinesInputs(string operationId) =>
        GifOperations.CombinesInputs(operationId);
}
