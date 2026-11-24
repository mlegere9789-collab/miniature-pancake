using System.Threading.Tasks;
using MediaSuite.App.Services;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// <see cref="SettingsViewModel"/> is the Settings screen: every change applies
/// immediately and writes straight to disk, so most of what is worth testing here is
/// "does the right thing get persisted, and does the right event fire so an already-open
/// module page or a running queue picks it up without a restart" — not anything that
/// needs a real Window. <see cref="ThemeService"/> is real (not faked): every place it
/// touches WPF (<c>Application.Current</c>, a real <c>Window</c>) is already null-safe in
/// production for exactly this reason (the app can construct its view models before the
/// main window exists), so it is safe to exercise directly. <c>BrowseOutputDirectoryCommand</c>,
/// <c>BrowseTempDirectoryCommand</c>, <c>BrowseToolsDirectoryCommand</c> and
/// <c>BrowseGoogleDriveCredentialsCommand</c> are not covered — each opens a real
/// <c>Microsoft.Win32</c> dialog, which nothing here can drive.
/// </summary>
public sealed class SettingsViewModelTests
{
    private sealed record Fixture(
        SettingsViewModel Settings,
        AppSettings AppSettings,
        FakeSettingsStore Store,
        FakeGoogleDriveClient Drive,
        ThemeService Theme) : IDisposable
    {
        public void Dispose() => Theme.Dispose();
    }

    private static Fixture CreateSettings()
    {
        var settings = new AppSettings();
        var store = new FakeSettingsStore(settings);
        var drive = new FakeGoogleDriveClient();
        var theme = new ThemeService();
        // No search roots and an empty PATH: every bundled tool resolves to "not found",
        // deterministically, without needing any real binary on the test machine.
        var toolLocator = new ToolLocator(Array.Empty<string>(), pathVariable: string.Empty);

        var page = new SettingsViewModel(settings, store, theme, toolLocator, drive);
        return new Fixture(page, settings, store, drive, theme);
    }

    [Fact]
    public void Defaults_to_System_theme_with_nothing_persisted_yet()
    {
        using var fixture = CreateSettings();

        Assert.True(fixture.Settings.IsThemeSystem);
        Assert.False(fixture.Settings.IsThemeLight);
        Assert.False(fixture.Settings.IsThemeDark);
        Assert.Equal(0, fixture.Store.SaveCount);
    }

    [Fact]
    public void Choosing_a_theme_is_a_radio_button_not_a_checkbox()
    {
        using var fixture = CreateSettings();

        fixture.Settings.IsThemeDark = true;

        Assert.True(fixture.Settings.IsThemeDark);
        Assert.False(fixture.Settings.IsThemeLight);
        Assert.False(fixture.Settings.IsThemeSystem);
        Assert.Equal(ThemeMode.Dark, fixture.AppSettings.Theme);
        Assert.Equal(1, fixture.Store.SaveCount);

        // Explicitly "unchecking" Dark is a no-op -- there is no state that means "none
        // of the three", so nothing here should move or persist again.
        fixture.Settings.IsThemeDark = false;

        Assert.True(fixture.Settings.IsThemeDark);
        Assert.Equal(1, fixture.Store.SaveCount);

        fixture.Settings.IsThemeLight = true;

        Assert.True(fixture.Settings.IsThemeLight);
        Assert.False(fixture.Settings.IsThemeDark);
        Assert.Equal(2, fixture.Store.SaveCount);
    }

    [Fact]
    public void Setting_the_output_directory_persists_only_on_a_real_change()
    {
        using var fixture = CreateSettings();

        fixture.Settings.OutputDirectory = @"D:\Exports";
        Assert.Equal(@"D:\Exports", fixture.Settings.OutputDirectory);
        Assert.Equal(1, fixture.Store.SaveCount);

        fixture.Settings.OutputDirectory = @"D:\Exports";
        Assert.Equal(1, fixture.Store.SaveCount);
    }

    [Fact]
    public void MaxConcurrentJobs_is_clamped_and_notifies_the_live_queue()
    {
        using var fixture = CreateSettings();
        var raised = new List<int>();
        fixture.Settings.MaxConcurrentJobsChanged += (_, value) => raised.Add(value);

        fixture.Settings.MaxConcurrentJobs = 0;
        Assert.Equal(1, fixture.Settings.MaxConcurrentJobs);

        fixture.Settings.MaxConcurrentJobs = fixture.Settings.MaxConcurrencyLimit + 100;
        Assert.Equal(fixture.Settings.MaxConcurrencyLimit, fixture.Settings.MaxConcurrentJobs);

        Assert.Equal(new[] { 1, fixture.Settings.MaxConcurrencyLimit }, raised);
    }

    [Fact]
    public void ConcurrencyHint_names_this_machines_core_count()
    {
        using var fixture = CreateSettings();

        Assert.Contains(fixture.Settings.CoreCount.ToString(), fixture.Settings.ConcurrencyHint);
        Assert.Contains("cores)", fixture.Settings.ConcurrencyHint);
    }

    [Fact]
    public void Turning_on_a_custom_temp_folder_and_setting_it_both_take_effect()
    {
        using var fixture = CreateSettings();
        Assert.False(fixture.Settings.UseCustomTempDirectory);

        fixture.Settings.UseCustomTempDirectory = true;
        fixture.Settings.TempDirectory = @"D:\Scratch";

        Assert.True(fixture.Settings.UseCustomTempDirectory);
        Assert.Equal(@"D:\Scratch", fixture.Settings.TempDirectory);
        Assert.Equal(TempStorageMode.CustomFolder, fixture.AppSettings.TempStorage);
    }

    [Fact]
    public void GoogleDriveEnabled_toggle_notifies_every_open_module_page()
    {
        using var fixture = CreateSettings();
        bool? raised = null;
        fixture.Settings.GoogleDriveEnabledChanged += (_, enabled) => raised = enabled;

        fixture.Settings.GoogleDriveEnabled = true;

        Assert.True(fixture.Settings.GoogleDriveEnabled);
        Assert.True(raised);
        Assert.Equal(1, fixture.Store.SaveCount);
    }

    [Fact]
    public void GoogleDriveCredentialsPath_has_a_friendly_default_until_one_is_chosen()
    {
        using var fixture = CreateSettings();

        Assert.Equal("google-drive-credentials.json in the settings folder", fixture.Settings.GoogleDriveCredentialsPath);

        fixture.Settings.GoogleDriveCredentialsPath = @"C:\secrets\client.json";

        Assert.Equal(@"C:\secrets\client.json", fixture.Settings.GoogleDriveCredentialsPath);
    }

    [Fact]
    public void RefreshTools_lists_every_manifest_tool_as_missing_when_nothing_is_installed()
    {
        using var fixture = CreateSettings();

        Assert.Equal(ToolManifest.All.Count, fixture.Settings.Tools.Count);
        Assert.All(fixture.Settings.Tools, tool => Assert.False(tool.IsFound));
        Assert.Contains(fixture.Settings.Tools, tool => tool.IsRequired);
        Assert.Contains("required tool(s) still missing", fixture.Settings.ToolSummary);
        Assert.StartsWith($"0 of {ToolManifest.All.Count} tools found", fixture.Settings.ToolSummary);
    }

    [Fact]
    public void Starts_out_signed_out_and_signing_in_flips_status_and_the_commands()
    {
        using var fixture = CreateSettings();

        Assert.False(fixture.Settings.IsSignedInToGoogleDrive);
        Assert.Equal("Not signed in.", fixture.Settings.GoogleDriveStatus);
        Assert.True(fixture.Settings.SignInToGoogleDriveCommand.CanExecute(null));
        Assert.False(fixture.Settings.SignOutOfGoogleDriveCommand.CanExecute(null));

        fixture.Settings.SignInToGoogleDriveCommand.Execute(null);

        Assert.True(fixture.Settings.IsSignedInToGoogleDrive);
        Assert.Equal("Signed in.", fixture.Settings.GoogleDriveStatus);
        Assert.False(fixture.Settings.SignInToGoogleDriveCommand.CanExecute(null));
        Assert.True(fixture.Settings.SignOutOfGoogleDriveCommand.CanExecute(null));

        fixture.Settings.SignOutOfGoogleDriveCommand.Execute(null);

        Assert.False(fixture.Settings.IsSignedInToGoogleDrive);
        Assert.Equal("Not signed in.", fixture.Settings.GoogleDriveStatus);
    }

    [Fact]
    public void A_failed_sign_in_leaves_a_clear_message_instead_of_crashing_the_screen()
    {
        using var fixture = CreateSettings();
        fixture.Drive.SignInFailure = new InvalidOperationException("no browser available");

        fixture.Settings.SignInToGoogleDriveCommand.Execute(null);

        Assert.False(fixture.Settings.IsSignedInToGoogleDrive);
        Assert.Equal("Sign-in failed: no browser available", fixture.Settings.GoogleDriveStatus);
    }

    [Fact]
    public void A_failed_sign_out_leaves_a_clear_message_instead_of_crashing_the_screen()
    {
        // GoogleDriveClient.SignOutAsync deletes the token folder on disk, which can throw
        // (a locked file, antivirus scanning it) -- SignOutOfGoogleDriveCommand's execute
        // delegate is effectively an async void lambda, so without a local catch this would
        // only be caught by the app's generic unhandled-exception dialog instead of the same
        // inline feedback SignInToGoogleDriveAsync already gives on its own failures.
        using var fixture = CreateSettings();
        fixture.Settings.SignInToGoogleDriveCommand.Execute(null);
        fixture.Drive.SignOutFailure = new InvalidOperationException("token file is in use");

        fixture.Settings.SignOutOfGoogleDriveCommand.Execute(null);

        Assert.Equal("Sign-out failed: token file is in use", fixture.Settings.GoogleDriveStatus);
        // Still considered signed in -- the failure happened before SignOutAsync's caller
        // would have flipped this, so the screen must not claim a sign-out that didn't happen.
        Assert.True(fixture.Settings.IsSignedInToGoogleDrive);
        Assert.True(fixture.Settings.SignOutOfGoogleDriveCommand.CanExecute(null));
    }

    [Fact]
    public void SignInToGoogleDriveCommand_cannot_fire_a_second_time_while_the_first_call_is_still_in_flight()
    {
        // CanExecute only re-queries on the usual WPF input events, not the instant the
        // command's own async work starts -- without an explicit busy guard, a double-click
        // on "Sign in" while the OAuth round trip is still pending would start a second
        // sign-in flow.
        using var fixture = CreateSettings();
        var pending = new TaskCompletionSource();
        fixture.Drive.PendingSignIn = pending;

        Assert.True(fixture.Settings.SignInToGoogleDriveCommand.CanExecute(null));
        fixture.Settings.SignInToGoogleDriveCommand.Execute(null);

        Assert.False(fixture.Settings.SignInToGoogleDriveCommand.CanExecute(null));
        Assert.False(fixture.Settings.IsSignedInToGoogleDrive);

        pending.SetResult();

        Assert.True(fixture.Settings.IsSignedInToGoogleDrive);
        Assert.True(fixture.Settings.SignOutOfGoogleDriveCommand.CanExecute(null));
    }
}
