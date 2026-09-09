using MediaSuite.Core.Engines;

namespace MediaSuite.Core.Features;

/// <summary>
/// How an operation treats a multi-file selection.
/// </summary>
/// <remarks>
/// The queue's default is a job per file, so a batch runs in parallel and one failure does
/// not take the rest with it. A few tools break that rule because their whole point is to
/// merge — a GIF built from a folder of stills is one animation, not twenty, and merging
/// several PDFs is one document, not several. Archive conversion (<see cref="ArchiveEngine"/>)
/// is deliberately not one of these: each selected archive is its own independent
/// extract-and-repack, never combined into one output, so it stays job-per-file like the
/// default. A future "bundle these loose files into one new archive" operation would need
/// registering here the same way GIF/PDF are, since nothing does that today.
/// </remarks>
public static class OperationInputRules
{
    /// <summary>True when every selected file belongs to a single job producing one output.</summary>
    public static bool CombinesInputs(string operationId) =>
        GifOperations.CombinesInputs(operationId)
        || PdfOperations.CombinesInputs(operationId);
}
