using MediaSuite.Core.Tooling;

namespace MediaSuite.App.ViewModels;

/// <summary>Row in the Settings "bundled tools" list.</summary>
public sealed class ToolStatusViewModel
{
    public ToolStatusViewModel(ToolDescriptor descriptor, ToolLocation location)
    {
        Name = descriptor.DisplayName;
        Purpose = descriptor.Purpose;
        License = descriptor.License;
        SourceUrl = descriptor.SourceUrl;
        IsRequired = descriptor.IsRequired;
        IsFound = location.Found;

        Status = location.Source switch
        {
            ToolSource.Override => "Found (custom path)",
            ToolSource.Bundled => "Found (bundled)",
            ToolSource.SystemPath => "Found (system PATH)",
            _ => descriptor.IsRequired ? "Missing — required" : "Missing — optional",
        };

        Path = location.Path ?? $"Expected in tools\\{descriptor.FolderName}\\";
    }

    public string Name { get; }

    public string Purpose { get; }

    public string License { get; }

    public string SourceUrl { get; }

    public bool IsRequired { get; }

    public bool IsFound { get; }

    public string Status { get; }

    public string Path { get; }
}
