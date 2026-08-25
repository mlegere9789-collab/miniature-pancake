using MediaSuite.Core.Features;

namespace MediaSuite.App.ViewModels;

/// <summary>One tool card, and whether it can actually run yet.</summary>
public sealed class FeatureViewModel
{
    public FeatureViewModel(FeatureDescriptor descriptor, bool isAvailable)
    {
        Descriptor = descriptor;
        IsAvailable = isAvailable;
    }

    public FeatureDescriptor Descriptor { get; }

    public string Name => Descriptor.Name;

    public string Description => Descriptor.Description;

    public string OperationId => Descriptor.OperationId;

    /// <summary>True when an engine claims this operation, so the card is live.</summary>
    public bool IsAvailable { get; }

    public string StatusLabel => IsAvailable ? "Ready" : $"Build step {Descriptor.BuildStep}";

    public override string ToString() => Name;
}
