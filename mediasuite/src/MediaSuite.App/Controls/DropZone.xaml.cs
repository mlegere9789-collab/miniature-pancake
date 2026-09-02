using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using MediaSuite.Core.Jobs;
using Microsoft.Win32;

namespace MediaSuite.App.Controls;

/// <summary>
/// The drop target used by every module: drag files or folders onto it, or click to
/// browse. Folders are expanded to their files before the command fires, so a batch
/// and a single file arrive at the queue in exactly the same shape.
/// </summary>
public partial class DropZone : UserControl
{
    public static readonly DependencyProperty PromptProperty = DependencyProperty.Register(
        nameof(Prompt), typeof(string), typeof(DropZone),
        new PropertyMetadata("Drop files here"));

    public static readonly DependencyProperty HintProperty = DependencyProperty.Register(
        nameof(Hint), typeof(string), typeof(DropZone),
        new PropertyMetadata("Single files, whole folders or thousands at once — same queue either way."));

    public static readonly DependencyProperty FilesDroppedCommandProperty = DependencyProperty.Register(
        nameof(FilesDroppedCommand), typeof(ICommand), typeof(DropZone),
        new PropertyMetadata(null));

    public DropZone()
    {
        InitializeComponent();

        DragEnter += OnDragEnter;
        DragOver += OnDragOver;
        DragLeave += OnDragLeave;
        Drop += OnDrop;
        MouseLeftButtonUp += (_, _) => BrowseForFiles();
    }

    /// <summary>Headline shown inside the zone.</summary>
    public string Prompt
    {
        get => (string)GetValue(PromptProperty);
        set => SetValue(PromptProperty, value);
    }

    /// <summary>Second line, usually the accepted formats.</summary>
    public string Hint
    {
        get => (string)GetValue(HintProperty);
        set => SetValue(HintProperty, value);
    }

    /// <summary>Invoked with an <c>IReadOnlyList&lt;string&gt;</c> of file paths.</summary>
    public ICommand? FilesDroppedCommand
    {
        get => (ICommand?)GetValue(FilesDroppedCommandProperty);
        set => SetValue(FilesDroppedCommandProperty, value);
    }

    /// <summary>Raised alongside the command, for code-behind consumers.</summary>
    public event EventHandler<IReadOnlyList<string>>? FilesDropped;

    private void OnDragEnter(object sender, DragEventArgs e) => SetActive(HasFiles(e));

    private void OnDragOver(object sender, DragEventArgs e)
    {
        e.Effects = HasFiles(e) ? DragDropEffects.Copy : DragDropEffects.None;
        e.Handled = true;
    }

    private void OnDragLeave(object sender, DragEventArgs e) => SetActive(false);

    private void OnDrop(object sender, DragEventArgs e)
    {
        SetActive(false);

        if (e.Data.GetData(DataFormats.FileDrop) is not string[] dropped)
        {
            return;
        }

        e.Handled = true;
        Publish(InputCollector.Expand(dropped));
    }

    private void OnBrowseClick(object sender, RoutedEventArgs e)
    {
        e.Handled = true;
        BrowseForFiles();
    }

    private void BrowseForFiles()
    {
        var dialog = new OpenFileDialog
        {
            Title = "Choose files",
            Multiselect = true,
            CheckFileExists = true,
        };

        if (dialog.ShowDialog() == true)
        {
            Publish(InputCollector.Expand(dialog.FileNames));
        }
    }

    private void Publish(IReadOnlyList<string> files)
    {
        if (files.Count == 0)
        {
            return;
        }

        FilesDropped?.Invoke(this, files);

        if (FilesDroppedCommand?.CanExecute(files) == true)
        {
            FilesDroppedCommand.Execute(files);
        }
    }

    private static bool HasFiles(DragEventArgs e) => e.Data.GetDataPresent(DataFormats.FileDrop);

    private void SetActive(bool active)
    {
        // Border.Background / Border.BorderBrush, not the Control.* properties this
        // UserControl also has — they are distinct dependency properties.
        Surface.SetResourceReference(
            Border.BackgroundProperty,
            active ? "Brush.DropZone.ActiveBackground" : "Brush.DropZone.Background");
        Surface.SetResourceReference(
            Border.BorderBrushProperty,
            active ? "Brush.DropZone.ActiveBorder" : "Brush.DropZone.Border");
    }
}
