namespace MediaSuite.Core.Jobs;

/// <summary>
/// Keeps the set of registered engines and picks the one that will run a given job.
/// Engines registered later win ties, which lets a specialised engine (say a
/// libvips fast path) shadow a general one without removing it.
/// </summary>
public sealed class EngineRegistry
{
    private readonly List<IConversionEngine> _engines = new();

    public IReadOnlyList<IConversionEngine> Engines => _engines;

    public EngineRegistry Register(IConversionEngine engine)
    {
        ArgumentNullException.ThrowIfNull(engine);
        _engines.Add(engine);
        return this;
    }

    /// <summary>Returns the engine that will run <paramref name="spec"/>, or null when none will.</summary>
    public IConversionEngine? Resolve(JobSpec spec)
    {
        ArgumentNullException.ThrowIfNull(spec);

        for (var i = _engines.Count - 1; i >= 0; i--)
        {
            if (_engines[i].CanHandle(spec))
            {
                return _engines[i];
            }
        }

        return null;
    }

    /// <summary>
    /// True when some engine claims this operation id. Used by the UI to enable only the
    /// tools that are actually wired up, rather than offering one that will fail.
    /// </summary>
    public bool SupportsOperation(string operationId)
    {
        if (string.IsNullOrWhiteSpace(operationId))
        {
            return false;
        }

        var probe = new JobSpec
        {
            OperationId = operationId,
            InputPaths = Array.Empty<string>(),
            Output = new OutputTarget { Directory = "." },
        };

        return Resolve(probe) is not null;
    }

    /// <summary>Like <see cref="Resolve"/> but throws when nothing can handle the job.</summary>
    public IConversionEngine ResolveRequired(JobSpec spec) =>
        Resolve(spec) ?? throw new InvalidOperationException(
            $"No engine is registered for operation '{spec.OperationId}'.");
}
