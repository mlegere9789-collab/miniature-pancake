namespace MediaSuite.Core.Settings;

/// <summary>Well-known locations the app writes to.</summary>
public static class AppPaths
{
    private const string AppFolderName = "MediaSuite";

    /// <summary>Roaming app data folder — holds settings.</summary>
    public static string DataDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), AppFolderName);

    /// <summary>Local app data folder — holds temp working files and caches.</summary>
    public static string LocalDataDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), AppFolderName);

    public static string SettingsFile => Path.Combine(DataDirectory, "settings.json");

    public static string DefaultTempDirectory => Path.Combine(LocalDataDirectory, "temp");

    /// <summary>Where converted files land until the user picks somewhere else.</summary>
    public static string DefaultOutputDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), AppFolderName);

    /// <summary>
    /// Default location for the Google Drive OAuth client file the user downloads from
    /// Google Cloud Console — see Settings → Google Drive for how to get one.
    /// </summary>
    public static string DefaultGoogleDriveCredentialsFile =>
        Path.Combine(DataDirectory, "google-drive-credentials.json");

    /// <summary>Where the signed-in account's cached OAuth token lives.</summary>
    public static string GoogleDriveTokenDirectory => Path.Combine(LocalDataDirectory, "google-drive-token");
}
