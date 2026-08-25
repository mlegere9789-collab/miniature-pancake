using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using MediaSuite.Core.Settings;
using Microsoft.Win32;

namespace MediaSuite.App.Services;

/// <summary>
/// Owns the light/dark palette swap and, in <see cref="ThemeMode.System"/>, keeps the
/// app in step with the Windows "Choose your mode" setting while it is running.
/// </summary>
public sealed class ThemeService : IDisposable
{
    private const string PersonalizeKey = @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize";
    private const string AppsUseLightThemeValue = "AppsUseLightTheme";

    /// <summary>Index of the palette dictionary inside App.xaml's merged dictionaries.</summary>
    private const int PaletteDictionaryIndex = 0;

    private static readonly Uri LightPalette = new("Themes/Palette.Light.xaml", UriKind.Relative);
    private static readonly Uri DarkPalette = new("Themes/Palette.Dark.xaml", UriKind.Relative);

    private bool _subscribed;
    private bool _disposed;

    /// <summary>The mode the user picked (which may be "follow Windows").</summary>
    public ThemeMode Mode { get; private set; } = ThemeMode.System;

    /// <summary>True when the palette currently in use is the dark one.</summary>
    public bool IsDark { get; private set; }

    /// <summary>Applies a theme and starts or stops following Windows as needed.</summary>
    public void Apply(ThemeMode mode)
    {
        Mode = mode;
        IsDark = mode switch
        {
            ThemeMode.Light => false,
            ThemeMode.Dark => true,
            _ => IsWindowsUsingDarkMode(),
        };

        SwapPalette(IsDark);
        ApplyTitleBarTheme();

        if (mode == ThemeMode.System)
        {
            Subscribe();
        }
        else
        {
            Unsubscribe();
        }
    }

    /// <summary>
    /// Applies the dark title bar to a window that opened after the theme was set.
    /// </summary>
    public void ApplyTitleBarTheme(Window? window = null)
    {
        if (Application.Current is null)
        {
            return;
        }

        var windows = window is not null
            ? new[] { window }
            : Application.Current.Windows.OfType<Window>().ToArray();

        foreach (var target in windows)
        {
            TrySetImmersiveDarkMode(target, IsDark);
        }
    }

    private static void SwapPalette(bool dark)
    {
        var dictionaries = Application.Current?.Resources.MergedDictionaries;
        if (dictionaries is null)
        {
            return;
        }

        var palette = new ResourceDictionary { Source = dark ? DarkPalette : LightPalette };

        if (dictionaries.Count > PaletteDictionaryIndex)
        {
            dictionaries[PaletteDictionaryIndex] = palette;
        }
        else
        {
            dictionaries.Insert(PaletteDictionaryIndex, palette);
        }
    }

    /// <summary>Reads the Windows app-mode preference; assumes light when it cannot be read.</summary>
    public static bool IsWindowsUsingDarkMode()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(PersonalizeKey);
            return key?.GetValue(AppsUseLightThemeValue) is int appsUseLightTheme && appsUseLightTheme == 0;
        }
        catch (Exception ex) when (ex is System.Security.SecurityException or UnauthorizedAccessException or IOException)
        {
            return false;
        }
    }

    private void Subscribe()
    {
        if (_subscribed)
        {
            return;
        }

        SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;
        _subscribed = true;
    }

    private void Unsubscribe()
    {
        if (!_subscribed)
        {
            return;
        }

        SystemEvents.UserPreferenceChanged -= OnUserPreferenceChanged;
        _subscribed = false;
    }

    private void OnUserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e)
    {
        if (e.Category is not (UserPreferenceCategory.General or UserPreferenceCategory.VisualStyle))
        {
            return;
        }

        // The registry value is written slightly after the notification arrives, so
        // re-read it on the dispatcher rather than inline.
        _ = Application.Current?.Dispatcher.InvokeAsync(() =>
        {
            if (Mode != ThemeMode.System)
            {
                return;
            }

            var dark = IsWindowsUsingDarkMode();
            if (dark == IsDark)
            {
                return;
            }

            IsDark = dark;
            SwapPalette(dark);
            ApplyTitleBarTheme();
        });
    }

    private static void TrySetImmersiveDarkMode(Window window, bool dark)
    {
        try
        {
            var handle = new WindowInteropHelper(window).Handle;
            if (handle == IntPtr.Zero)
            {
                return;
            }

            var useDark = dark ? 1 : 0;
            _ = DwmSetWindowAttribute(handle, DwmwaUseImmersiveDarkMode, ref useDark, sizeof(int));
        }
        catch (DllNotFoundException)
        {
            // Older Windows without dwmapi — the title bar just stays light.
        }
        catch (EntryPointNotFoundException)
        {
        }
    }

    private const int DwmwaUseImmersiveDarkMode = 20;

    [DllImport("dwmapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        Unsubscribe();
        _disposed = true;
    }
}
