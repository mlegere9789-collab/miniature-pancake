using System.Text.Json;
using System.Text.Json.Serialization;

namespace MediaSuite.Core.Settings;

/// <summary>
/// JSON-file settings store.
/// </summary>
/// <remarks>
/// Two deliberate behaviours: saves are atomic (write a temp file, then replace) so a
/// crash mid-save cannot leave a truncated file, and a file we cannot parse is renamed
/// to <c>settings.corrupt.json</c> rather than deleted, so nothing the user typed is
/// lost silently.
/// </remarks>
public sealed class JsonSettingsStore : ISettingsStore
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = { new JsonStringEnumConverter() },
    };

    private readonly string _filePath;

    public JsonSettingsStore(string? filePath = null) =>
        _filePath = filePath ?? AppPaths.SettingsFile;

    public string FilePath => _filePath;

    private const int MaxLoadAttempts = 3;

    public AppSettings Load()
    {
        if (!File.Exists(_filePath))
        {
            return new AppSettings().Normalize();
        }

        for (var attempt = 1; attempt <= MaxLoadAttempts; attempt++)
        {
            try
            {
                var json = File.ReadAllText(_filePath);
                var settings = JsonSerializer.Deserialize<AppSettings>(json, SerializerOptions);
                return (settings ?? new AppSettings()).Normalize();
            }
            catch (JsonException)
            {
                // The file opened fine but its own contents don't parse -- this is
                // genuine corruption (not a transient lock, which would never have let
                // ReadAllText return in the first place), so quarantine it and start
                // fresh rather than retrying something that will never succeed.
                QuarantineCorruptFile();
                return new AppSettings().Normalize();
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Unlike a JsonException, this could be real corruption, or the file
                // being momentarily locked by antivirus/backup software, or racing this
                // store's own Save() mid-File.Replace on another thread -- there is no
                // way to tell which from here, so this retries briefly rather than
                // immediately quarantining a perfectly good settings file (silently
                // resetting every real preference and custom preset to defaults for the
                // rest of the session, and overwriting the user's own file with those
                // defaults the moment anything calls Save() again) just because it
                // happened to be busy for a moment.
                if (attempt == MaxLoadAttempts)
                {
                    return new AppSettings().Normalize();
                }

                Thread.Sleep(TimeSpan.FromMilliseconds(50 * attempt));
            }
        }

        return new AppSettings().Normalize();
    }

    public void Save(AppSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);

        var directory = Path.GetDirectoryName(_filePath);
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var json = JsonSerializer.Serialize(settings.Normalize(), SerializerOptions);

        // A unique-per-call temp file name -- not a single fixed ".tmp" shared by every
        // Save() call -- so two concurrent saves (a background auto-save of window
        // position racing an explicit Settings-screen save, say; nothing here
        // synchronizes callers against each other) can never both write through the
        // same temp path and corrupt or truncate one another's in-flight content.
        var tempPath = $"{_filePath}.{Guid.NewGuid():N}.tmp";

        try
        {
            File.WriteAllText(tempPath, json);

            if (File.Exists(_filePath))
            {
                File.Replace(tempPath, _filePath, destinationBackupFileName: null);
            }
            else
            {
                File.Move(tempPath, _filePath);
            }
        }
        finally
        {
            // File.Replace/Move already remove tempPath on success; this only matters
            // if something above threw first, so a failed save doesn't also litter a
            // stray, uniquely-named temp file behind every time.
            try
            {
                if (File.Exists(tempPath))
                {
                    File.Delete(tempPath);
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Best effort only -- must not mask whatever the real save outcome was.
            }
        }
    }

    private void QuarantineCorruptFile()
    {
        try
        {
            var quarantinePath = Path.ChangeExtension(_filePath, ".corrupt.json");
            File.Copy(_filePath, quarantinePath, overwrite: true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Best effort only — failing to keep a backup must not stop the app starting.
        }
    }
}
