namespace MediaSuite.Core.Jobs;

/// <summary>
/// <see cref="IProgress{T}"/> that calls straight through on the reporting thread.
/// </summary>
/// <remarks>
/// Deliberately not <see cref="Progress{T}"/>: that captures the current
/// <see cref="SynchronizationContext"/> and posts asynchronously, which reorders ticks
/// and makes tests racy. Marshalling to the UI thread is the UI layer's job.
/// </remarks>
public sealed class DelegateProgress<T> : IProgress<T>
{
    private readonly Action<T> _handler;

    public DelegateProgress(Action<T> handler) =>
        _handler = handler ?? throw new ArgumentNullException(nameof(handler));

    public void Report(T value) => _handler(value);
}
