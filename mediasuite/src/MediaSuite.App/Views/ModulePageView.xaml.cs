using System.Windows.Controls;

namespace MediaSuite.App.Views;

/// <summary>
/// Shared view for the four module pages. Which tools it shows comes entirely from the
/// bound <c>ModulePageViewModel</c>, so Convert, Compress, Tools and Upscale need no
/// separate XAML.
/// </summary>
public partial class ModulePageView : UserControl
{
    public ModulePageView() => InitializeComponent();
}
