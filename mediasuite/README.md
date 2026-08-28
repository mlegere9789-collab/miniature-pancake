# MediaSuite

A local, offline Windows 11 media converter, compressor and toolbox — the FreeConvert
Convert / Compress / Tools feature set running entirely on your own machine, plus an AI
photo upscaler. Personal, single-user build: no accounts, no licence keys, no upload
limits, no server.

> **Status: build steps 1–6 of 18.** The scaffold, shell, job queue, image module,
> video/audio module and GIF module are in place. PDF is next — see
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
- **Running jobs from the UI** — pick a tool, an output format and a quality preset,
  choose where results go, and the staged files are queued one job each — except for
  tools that merge their inputs, which take the whole selection as a single job

## Build order

| Step | Work | State |
| --- | --- | --- |
| 1 | Project scaffold | done |
| 2 | Core shell UI — nav, drop zone, theme system | done |
| 3 | Job queue manager | done |
| 4 | Image module (incl. RAW) | done |
| 5 | Video / audio module | done |
| 6 | GIF module | done |
| 7 | PDF module | next |
| 8 | Document / ebook module | |
| 9 | Archive / unit / time converters | |
| 10 | AI upscaler (CUDA + CPU fallback) | |
| 11 | Settings system — presets | partial (shell settings done) |
| 12 | Google Drive integration | |
| 13 | Format-parity audit vs FreeConvert | |
| 14 | QA pass against real sample files | |
| 15 | Inno Setup installer | |
| 16 | Update check | |
| 17 | Polish — tooltips, error and empty states, keyboard nav | |
| 18 | Final build and handoff | |

## Licence note

This is a personal, non-distributed build, so the GPL/AGPL tools (Ghostscript, Calibre,
Pandoc, MuPDF) ship as-is. If it is ever distributed, those need replacing or licensing
commercially. RAR creation is not supported for the same reason — extraction only.
