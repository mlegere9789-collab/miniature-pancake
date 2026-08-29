using MediaSuite.Core.Settings;
using Xunit;

namespace MediaSuite.Core.Tests;

public class CustomPresetTests
{
    private static CustomPreset Preset(string name, params (string Key, string Value)[] options) => new()
    {
        Name = name,
        Options = options.ToDictionary(o => o.Key, o => o.Value),
    };

    [Fact]
    public void An_operation_with_no_saved_presets_returns_an_empty_list()
    {
        var settings = new AppSettings();

        Assert.Empty(settings.PresetsFor("video.compress"));
    }

    [Fact]
    public void Saving_a_preset_makes_it_show_up_for_that_operation_only()
    {
        var settings = new AppSettings();

        settings.SaveCustomPreset("video.compress", Preset("Archive", ("crf", "28")));

        Assert.Single(settings.PresetsFor("video.compress"));
        Assert.Empty(settings.PresetsFor("image.compress"));
    }

    [Fact]
    public void Saving_under_a_name_already_in_use_overwrites_it_instead_of_duplicating()
    {
        var settings = new AppSettings();
        settings.SaveCustomPreset("video.compress", Preset("Archive", ("crf", "28")));

        settings.SaveCustomPreset("video.compress", Preset("archive", ("crf", "30")));

        var saved = settings.PresetsFor("video.compress");
        Assert.Single(saved);
        Assert.Equal("30", saved[0].Options["crf"]);
    }

    [Fact]
    public void Presets_are_returned_in_the_order_they_were_saved()
    {
        var settings = new AppSettings();
        settings.SaveCustomPreset("video.compress", Preset("First"));
        settings.SaveCustomPreset("video.compress", Preset("Second"));

        var saved = settings.PresetsFor("video.compress");

        Assert.Equal(new[] { "First", "Second" }, saved.Select(p => p.Name));
    }

    [Fact]
    public void Deleting_a_preset_by_name_is_case_insensitive()
    {
        var settings = new AppSettings();
        settings.SaveCustomPreset("video.compress", Preset("Archive"));

        var removed = settings.DeleteCustomPreset("video.compress", "ARCHIVE");

        Assert.True(removed);
        Assert.Empty(settings.PresetsFor("video.compress"));
    }

    [Fact]
    public void Deleting_a_preset_that_does_not_exist_returns_false()
    {
        var settings = new AppSettings();

        Assert.False(settings.DeleteCustomPreset("video.compress", "Archive"));
    }

    [Fact]
    public void Deleting_the_last_preset_for_an_operation_removes_the_operation_entirely()
    {
        var settings = new AppSettings();
        settings.SaveCustomPreset("video.compress", Preset("Archive"));

        settings.DeleteCustomPreset("video.compress", "Archive");

        Assert.False(settings.CustomPresets.ContainsKey("video.compress"));
    }

    [Fact]
    public void Saving_requires_a_non_blank_name()
    {
        var settings = new AppSettings();

        Assert.Throws<ArgumentException>(() => settings.SaveCustomPreset("video.compress", Preset("   ")));
    }

    [Fact]
    public void Presets_survive_a_save_and_reload()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        var settings = new AppSettings();
        settings.SaveCustomPreset("video.compress", Preset("Archive", ("crf", "28"), ("bitrate", "1500k")));

        new JsonSettingsStore(path).Save(settings);
        var reloaded = new JsonSettingsStore(path).Load();

        var saved = reloaded.PresetsFor("video.compress");
        Assert.Single(saved);
        Assert.Equal("Archive", saved[0].Name);
        Assert.Equal("28", saved[0].Options["crf"]);
        Assert.Equal("1500k", saved[0].Options["bitrate"]);
    }

    [Fact]
    public void A_null_preset_list_from_a_hand_edited_file_is_dropped_on_load()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, """{ "CustomPresets": { "video.compress": null } }""");

        var settings = new JsonSettingsStore(path).Load();

        Assert.False(settings.CustomPresets.ContainsKey("video.compress"));
    }

    [Fact]
    public void A_null_entry_in_a_preset_list_from_a_hand_edited_file_is_dropped_on_load()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, """
            {
              "CustomPresets": {
                "video.compress": [ null, { "Name": "Archive", "Options": { "crf": "28" } } ]
              }
            }
            """);

        var settings = new JsonSettingsStore(path).Load();

        var saved = settings.PresetsFor("video.compress");
        Assert.Single(saved);
        Assert.Equal("Archive", saved[0].Name);
    }

    [Fact]
    public void A_preset_with_a_blank_name_from_a_hand_edited_file_is_dropped_on_load()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, """
            {
              "CustomPresets": {
                "video.compress": [ { "Name": "   ", "Options": {} } ]
              }
            }
            """);

        var settings = new JsonSettingsStore(path).Load();

        Assert.Empty(settings.PresetsFor("video.compress"));
        Assert.False(settings.CustomPresets.ContainsKey("video.compress"));
    }

    [Fact]
    public void A_preset_with_null_options_from_a_hand_edited_file_is_dropped_on_load()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, """
            {
              "CustomPresets": {
                "video.compress": [ { "Name": "Archive", "Options": null } ]
              }
            }
            """);

        var settings = new JsonSettingsStore(path).Load();

        Assert.Empty(settings.PresetsFor("video.compress"));
    }
}
