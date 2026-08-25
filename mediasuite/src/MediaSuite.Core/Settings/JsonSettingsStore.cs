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

    public AppSettings Load()
    {
        if (!File.Exists(_filePath))
        {
            return new AppSettings().Normalize();
        }

        try
        {
            var json = File.ReadAllText(_filePath);
            var settings = JsonSerializer.Deserialize<AppSettings>(json, SerializerOptions);
            return (settings ?? new AppSettings()).Normalize();
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            QuarantineCorruptFile();
            return new AppSettings().Normalize();
        }
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
        var tempPath = _filePath + ".tmp";

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
