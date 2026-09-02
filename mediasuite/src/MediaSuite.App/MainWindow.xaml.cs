using System.Windows;
using MediaSuite.App.Services;

namespace MediaSuite.App;

public partial class MainWindow : Window
{
    private readonly ThemeService _themeService;

    public MainWindow(ThemeService themeService)
    {
        _themeService = themeService;
        InitializeComponent();
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);

        // The window handle only exists from here on, which is what the dark title bar needs.
        _themeService.ApplyTitleBarTheme(this);
    }
}
