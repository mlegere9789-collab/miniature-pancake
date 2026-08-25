using MediaSuite.Core.Features;

namespace MediaSuite.App.ViewModels;

/// <summary>A sub-heading on a module page and the tools underneath it.</summary>
public sealed class FeatureGroupViewModel
{
    public FeatureGroupViewModel(string name, IReadOnlyList<FeatureDescriptor> features)
    {
        Name = name;
        Features = features;
    }

    public string Name { get; }

    public IReadOnlyList<FeatureDescriptor> Features { get; }

    public string CountLabel => Features.Count == 1 ? "1 tool" : $"{Features.Count} tools";
}
