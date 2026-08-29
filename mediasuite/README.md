# MediaSuite

A local, offline Windows 11 media converter, compressor and toolbox — the FreeConvert
Convert / Compress / Tools feature set running entirely on your own machine, plus an AI
photo upscaler. Personal, single-user build: no accounts, no licence keys, no upload
limits, no server.

> **Status: build steps 1–13 of 18.** Every conversion module from the brief is in place,
> every tool has a Custom preset backed by named, savable option sets, jobs can optionally
> upload their output to Google Drive, and the format catalogue has been audited against
> FreeConvert's format support — what is left is QA, the installer and polish. See
> [Build order](#build-order).

## Layout

```
mediasuite/
├── MediaSuite.sln
├── src/
│   ├── MediaSuite.Core/      net8.0        — domain model, engine contract, tools, settings
│   └── MediaSuite.App/       net8.0-windows — WPF shell (UI only)
├── tests/
│   └── MediaSuite.Core.Tests/              — xUnit tests for everything in Core
└── tools/                                  — bundled third-party binaries (not in git)
```

`MediaSuite.Core` deliberately targets plain `net8.0` rather than `net8.0-windows`:
everything except the UI is platform-neutral, which keeps it unit-testable and keeps
Windows-only concerns from leaking into the domain model.

## Architecture

```
WPF shell  (Convert / Compress / Tools / Upscale / Settings)
    │
Job queue manager  (concurrency, progress, cancel, pause)
    │
IConversionEngine  ── the single seam between the queue and the outside world
    │
    ├── FFmpeg            video, audio, GIF
    ├── ImageMagick / libvips / LibRaw    images including camera RAW
    ├── MuPDF / QPDF / Ghostscript        PDF
    ├── Pandoc / LibreOffice / Calibre    documents and ebooks
    ├── 7-Zip                             archives
    └── Real-ESRGAN (CUDA)                AI upscaling
    │
Output handler → local folder, or Google Drive (optional, off by default)
```

Engines shell out through a single `IProcessRunner`, which streams tool output, keeps
only the tail of it for diagnostics, and kills the process tree when a job is canceled.

Every engine is a thin adapter implementing one interface:

```csharp
Task<JobResult> RunAsync(JobSpec spec, IProgress<JobProgress> progress, CancellationToken ct);
```

The queue never learns engine-specific details, so a new format means a new adapter and
nothing else.

## Building

Requires the **.NET 8 SDK on Windows** — `MediaSuite.App` is a WPF project and cannot be
compiled on Linux or macOS. `MediaSuite.Core` and its tests build anywhere.

```powershell
dotnet build mediasuite\MediaSuite.sln -c Release
dotnet test  mediasuite\MediaSuite.sln -c Release
dotnet run   --project mediasuite\src\MediaSuite.App
```

CI (`.github/workflows/mediasuite-ci.yml`) builds and tests the whole solution on
`windows-latest` for every change under `mediasuite/`.

## Bundled tools

The app shells out to third-party binaries; none of them are expected on your PATH. Drop
them into `tools/` next to `MediaSuite.exe` — see [tools/README.md](tools/README.md) for
the exact folder names, download links and licences. Settings → Bundled tools shows what
was found and what is missing, and lets you point at a different folder.

The app runs without them: features whose tools are missing stay disabled instead of
failing at the moment you press Convert.

## What is done

- Solution scaffold, `Directory.Build.props`, nullable + analyzers on everywhere
- **Format catalogue** — seed list of image, RAW, vector, video, audio, document, ebook,
  PDF and archive formats, keyed by extension with alias handling
- **Feature catalogue** — all 68 tools from the brief as typed descriptors, the single
  source of truth for the shell's content and for engine operation ids
- **Engine contract** — `IConversionEngine`, `JobSpec`, `JobResult`, `JobProgress`,
  `OutputTarget`, `EngineRegistry`
- **Tool discovery** — manifest of 15 binaries with licences and download sources, plus a
  locator with override → bundled → PATH resolution
- **Settings** — JSON store with atomic writes, corrupt-file quarantine and range
  clamping; theme, save folder, concurrency, temp storage, update check
- **Shell** — navigation rail, drag-and-drop zone (files *and* folders), staged-file list,
  per-page tool catalogue, full Settings screen
- **Theming** — light, dark and follow-Windows, swapped live at runtime, including the
  title bar; persists between launches
- **Job queue** — engine-agnostic runner with per-job and whole-queue cancel, pause and
  resume, live progress, concurrency auto-tuned to the core count and adjustable while
  running, and a private scratch folder per job that is cleaned up afterwards
- **Queue UI** — status strip plus a live panel: one row per job with its own progress
  bar and cancel button
- **Image module** — convert (including camera RAW via LibRaw), compress, resize, crop,
  rotate, flip, enlarge and PNG-to-SVG tracing, driven by ImageMagick and Potrace
- **Video and audio module** — convert, compress (by quality or by target size), extract
  audio, crop and trim, driven by FFmpeg with progress parsed from its own output, and
  remuxing instead of re-encoding whenever the streams already fit the container
- **GIF module** — video to GIF and back, GIF from a folder of stills, and GIF
  compression, each built on a per-clip palette rather than FFmpeg's default web-safe one
- **PDF module** — merge, split, compress, rotate, protect/unlock, crop, resize, flatten,
  organise/remove/extract pages, extract embedded images, and convert to and from JPG,
  PNG and Word, driven by QPDF, Ghostscript and MuPDF (LibreOffice for PDF to Word), with
  no one binary required for the whole module — a merge only needs QPDF installed
- **Document and ebook module** — DOCX/DOC/ODT/RTF/TXT/HTML/Markdown in any direction
  through Pandoc, with the legacy binary DOC format routed to LibreOffice instead since
  Pandoc can neither read nor write it; DOCX to PDF always through LibreOffice for the
  same reason PDF to Word does; EPUB/MOBI/AZW3 and the PDF↔EPUB bridges through Calibre
- **Archive module** — ZIP/7Z/TAR/GZIP in any direction plus RAR as a read-only source,
  through 7-Zip; since 7-Zip has no single "convert" command, every job extracts then
  recreates in the target format, with a GZIP target routed through an intermediate TAR
  first since gzip holds one stream, not several named entries
- **Unit and time converters** — length/mass/area/volume/temperature/data/speed, and time
  zones/Unix timestamps/durations/frame counts; pure arithmetic with no file to convert,
  so unlike every other module these never touch the job queue at all
- **AI upscaler** — 2x/4x/8x with general or anime models, optional denoise and sharpen,
  through Real-ESRGAN's ncnn-vulkan build (GPU via Vulkan, with a CPU fallback); 8x is two
  chained passes rather than trusting every build to accept a single "-s 8"; sharpening is
  an ordinary ImageMagick unsharp pass afterwards, since Real-ESRGAN has none of its own.
  "Face enhance" from the brief is not implemented — it needs a second bundled model
  (GFPGAN) the tool manifest does not carry yet, so it is a follow-up, not a fake
- **Running jobs from the UI** — pick a tool, an output format and a quality preset,
  choose where results go, and the staged files are queued one job each — except for
  tools that merge their inputs, which take the whole selection as a single job
- **Custom preset system** — a fourth preset, alongside Quick/Balanced/Best, whose
  parameters come entirely from advanced options instead of a built-in table; the
  Settings-backed store lets those options be saved under a name per tool and reloaded
  later, so a one-off "crf=20" doesn't have to be retyped next time
- **Google Drive upload** — off until you sign in from Settings, and off per job until
  you tick the box, through the `drive.file` OAuth scope so the app can only ever see
  files it uploaded itself; a failed upload never fails the job, since the converted
  file already exists locally either way, it just shows as a warning on an otherwise
  completed job
- **Format-parity audit** — every entry in the format catalogue now has to clear two
  bars: FreeConvert actually offers it, and the bundled tool genuinely supports it (a
  real, unlicensed encoder always present in a standard build, not "probably works").
  Adds AC-3 audio as a real output and recognises QuickTime's older `.qt` extension,
  DVD `.vob`, Flash-era `.f4v`/`.f4p` and `.amr` voice recordings as read-only sources

## Settings and presets

Quick/Balanced/Best map to a fixed table of values inside each engine — a compressor's
"Best", for instance, means a specific CRF or DPI, not a formula. Custom is the escape
hatch: every advanced option comes from the job itself rather than that table, using the
same `key=value`-per-line options every engine already reads through `JobSpec.Options`.
Naming and saving a Custom preset writes it into `AppSettings.CustomPresets`, keyed by
operation id, through the same atomic JSON store the rest of Settings uses — so presets
survive a crash mid-save the same way the theme or the concurrency limit does, and a
hand-edited or partially corrupt entry in the file is dropped rather than crashing the
app on load.

## Google Drive

There is no bundled default the way there is for the conversion tools — Drive access has
to come from a Google Cloud project you own, not one shipped in the app:

1. Create a project at [console.cloud.google.com](https://console.cloud.google.com), enable
   the Drive API, and add an OAuth client of type "Desktop app".
2. Download that client's JSON file and point Settings → Google Drive at it (it defaults
   to `google-drive-credentials.json` next to `settings.json`).
3. Sign in from Settings. The consent screen opens in your browser; the resulting token is
   cached locally so you only do this once.

Everything after that is per-job: each module page gets an "Also upload to Google Drive"
checkbox once the master switch is on, with a folder picker limited to Drive's top level
(plus "New folder") rather than a full tree browser. The app requests the `drive.file`
scope, not full Drive access, so it can only ever see files it created — never anything
else already in your Drive. `JobQueueManager` runs the upload after a successful
conversion, through the same `IGoogleDriveClient` seam every engine uses for its own
external tool, so the queue and the tests never depend on the real Google API client.

## Build order

| Step | Work | State |
| --- | --- | --- |
| 1 | Project scaffold | done |
| 2 | Core shell UI — nav, drop zone, theme system | done |
| 3 | Job queue manager | done |
| 4 | Image module (incl. RAW) | done |
| 5 | Video / audio module | done |
| 6 | GIF module | done |
| 7 | PDF module | done |
| 8 | Document / ebook module | done |
| 9 | Archive / unit / time converters | done |
| 10 | AI upscaler (CUDA + CPU fallback) | done — Vulkan GPU path, no face-enhance yet |
| 11 | Settings system — presets | done |
| 12 | Google Drive integration | done |
| 13 | Format-parity audit vs FreeConvert | done — spreadsheet/presentation/PostScript formats deliberately deferred, see the FormatCatalog doc comment |
| 14 | QA pass against real sample files | |
| 15 | Inno Setup installer | |
| 16 | Update check | |
| 17 | Polish — tooltips, error and empty states, keyboard nav | |
| 18 | Final build and handoff | |

## Licence note

This is a personal, non-distributed build, so the GPL/AGPL tools (Ghostscript, Calibre,
Pandoc, MuPDF) ship as-is. If it is ever distributed, those need replacing or licensing
commercially. RAR creation is not supported for the same reason — extraction only.
