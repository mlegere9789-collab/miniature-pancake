using MediaSuite.App.Mvvm;

namespace MediaSuite.App.ViewModels;

/// <summary>One destination in the navigation rail.</summary>
public abstract class PageViewModel : ObservableObject
{
    protected PageViewModel(string title, string glyph)
    {
        Title = title;
        Glyph = glyph;
    }

    /// <summary>Label in the nav rail.</summary>
    public string Title { get; }

    /// <summary>Segoe Fluent Icons glyph for the nav rail.</summary>
    public string Glyph { get; }
}
