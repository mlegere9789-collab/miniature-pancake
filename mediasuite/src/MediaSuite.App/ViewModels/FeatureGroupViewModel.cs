namespace MediaSuite.App.ViewModels;

/// <summary>A sub-heading on a module page and the tools underneath it.</summary>
public sealed class FeatureGroupViewModel
{
    public FeatureGroupViewModel(string name, IReadOnlyList<FeatureViewModel> features)
    {
        Name = name;
        Features = features;
    }

    public string Name { get; }

    public IReadOnlyList<FeatureViewModel> Features { get; }

    public string CountLabel
    {
        get
        {
            var ready = Features.Count(feature => feature.IsAvailable);
            var total = Features.Count;
            var tools = total == 1 ? "1 tool" : $"{total} tools";

            return ready == total ? tools : $"{tools} · {ready} ready";
        }
    }
}
