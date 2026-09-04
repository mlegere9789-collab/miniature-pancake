# MediaSuite

[![MediaSuite CI](https://github.com/mlegere9789-collab/miniature-pancake/actions/workflows/mediasuite-ci.yml/badge.svg?branch=main)](https://github.com/mlegere9789-collab/miniature-pancake/actions/workflows/mediasuite-ci.yml)

A local, offline Windows 11 media converter, compressor and toolbox — the FreeConvert
Convert / Compress / Tools feature set running entirely on your own machine, plus an AI
photo upscaler. Personal, single-user build: no accounts, no licence keys, no upload
limits, no server.

> **Status: build steps 1–18 of 18, complete, plus a self-contained installer.** Every
> conversion module from the brief is in place, every tool has a Custom preset backed by
> named, savable option sets, jobs can optionally upload their output to Google Drive, and
> the format catalogue has been audited against FreeConvert's format support. The
> installer now bundles all 14 third-party tools automatically — every one CI actually
> verified working, not just referenced — so a fresh install needs no manual downloads
> for the full feature set; see [Bundled tools](#bundled-tools). [QA.md](QA.md)
> is the checklist for actually verifying a real conversion — this project was built
> without a Windows machine, so no real conversion has ever been run despite the tools
> now being bundled. There is a real, CI-compiled Inno Setup installer (see
> [Installer](#installer)), and the app checks its own GitHub releases on launch — never
> silently, only ever offering the download page. This build carries version 1.0.0. See
> [Build order](#build-order).

## Layout

```
mediasuite/
├── MediaSuite.sln
├── src/
│   ├── MediaSuite.Core/      net8.0        — domain model, engine contract, tools, settings
│   └── MediaSuite.App/       net8.0-windows — WPF shell (UI only)
├── tests/
│   ├── MediaSuite.Core.Tests/              — xUnit tests for everything in Core
│   └── MediaSuite.App.Tests/               — xUnit tests for App-layer logic that does not
│                                              need a real Window: SingleInstanceGuard's
│                                              real mutex/pipe IPC, JobRowViewModel's
│                                              computed properties (StatusText, Copy
│                                              details visibility, live updates) driven
│                                              through a real QueuedJob, and
│                                              JobQueueViewModel's status strip and
│                                              taskbar-progress state driven through a real
│                                              JobQueueManager (pause/resume, cancel-one,
│                                              cancel-all, clear-finished, running/progress
│                                              state), and ModulePageViewModel -- the actual
│                                              Convert/Compress/etc. screen -- driven
│                                              through the real feature catalogue plus a
│                                              real JobLauncher/JobQueueManager (which
│                                              features are ready, staging and starting
│                                              files, output-format resolution, saved
│                                              presets, Google Drive folder loading), and
│                                              SettingsViewModel -- theme switching,
│                                              output/temp-directory and concurrency
│                                              persistence (clamped, with the running
│                                              queue notified), the Google Drive master
│                                              switch, bundled-tool discovery, and Drive
│                                              sign-in/out, and MainViewModel -- the wiring
│                                              between Settings and the live queue/module
│                                              pages (concurrency and the Drive switch both
│                                              reaching an already-running queue/open
│                                              pages without a restart), the dependency
│                                              warning banner, "Open with" file routing,
│                                              and the update-available banner. Anything
│                                              that does need a real Window still has no
│                                              automated coverage, only Build compiling it
│                                              and QA.md
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

The app shells out to third-party binaries, but you shouldn't need to find any of them
yourself: `installer/fetch-tools.ps1` downloads real Windows binaries for all 14 during
the CI build — from each tool's own official release channel wherever one exists as a
plain zip/7z, and by other means where it doesn't (LibRaw's `dcraw_emu.exe` has no
prebuilt binary anywhere, so it's compiled from source with vcpkg + MSVC; Ghostscript's
installer had its silent-install flag removed upstream, so its payload is extracted
directly with 7-Zip instead of run; LibreOffice and Calibre only ship as full installers,
so those install silently onto the CI machine itself and the result is harvested) — and
bundles the result straight into the installer. Every one of the 14 was confirmed
actually working by reading the real CI log, not assumed from a green checkmark; see
[tools/README.md](tools/README.md) for the exact method and evidence per tool.

You'd only need `tools/` yourself if you're running from source rather than the built
installer, or want to override a bundled tool with your own copy — drop a folder in and
Settings → Bundled tools shows what was found. The app runs without any of them too:
features whose tools are missing stay disabled instead of failing at the moment you press
Convert.

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
  bar and cancel button. A failed row also gets a "Copy details" button carrying the
  tool's full stderr/stdout, not just the one-line summary — `JobResult.Diagnostics`
  was being captured in every engine's `ToolExecutionException` all along and then
  discarded before this, which would have left a real failure with no way to see what
  the underlying tool actually said. Closing the app while anything is still running or
  queued asks for confirmation first — closing the window used to kill every in-flight
  job's process outright with no warning at all, silently throwing away work that could
  be most of the way through a long encode or upscale
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
- **AI upscaler** — 2x/4x/8x with general or anime models, optional denoise, sharpen and
  face enhance, through Real-ESRGAN's ncnn-vulkan build (GPU via Vulkan, with a CPU
  fallback); 8x is two chained passes rather than trusting every build to accept a single
  "-s 8"; sharpening is an ordinary ImageMagick unsharp pass afterwards, since Real-ESRGAN
  has none of its own. Face enhance is an extra pass, not a third model — detects faces in
  the already-upscaled image and restores each one through GFPGAN, the same shape the real
  upstream `realesrgan` Python CLI's own `--face_enhance` flag has. Its tool
  (`gfpgan/face_enhance.exe`, CPU-only) has no official prebuilt Windows binary anywhere,
  so `fetch-tools.ps1` compiles real vendored source for it against vcpkg-built `opencv4`
  and `ncnn` — see `installer/native/face-enhance/README.md` and `tools/README.md` for
  exactly where that source and its models come from, and why it's the most speculative,
  most fail-soft entry in the whole fetch: unlike everything else bundled so far, this is
  a real from-source compile confirmed working by real CI, not a known-good download, and
  its actual visual output quality has not been verified by a human on a real photo
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
- **QA runbook** — [QA.md](QA.md) is a checklist for running real conversions on a real
  Windows machine with the bundled tools actually installed, since nothing in this
  sandbox could ever do that. One automated test,
  `tests/MediaSuite.Core.Tests/Qa/RealToolSmokeTests.cs`, runs every registered engine
  through the real (non-fake) `ProcessRunner`, `ToolLocator` and `JobQueueManager` with
  no tools present, confirming a brand-new install fails cleanly rather than crashing —
  the one piece of real-wiring QA this environment can actually verify
- **Installer** — a traditional Inno Setup wizard: Program Files install with an
  elevation prompt, Start Menu shortcut, optional desktop shortcut, a proper uninstaller
  that never touches the tools folder or user settings, self-contained so a fresh
  Windows 11 machine does not also need the .NET runtime installed separately
- **Explorer integration** — the installer registers MediaSuite as an "Open with" choice
  in Explorer for every extension in the format catalogue, without ever overwriting a
  file's actual default handler (a JPG still opens in Photos by default; MediaSuite is
  only ever offered as a choice). Opening a file this way, or dragging one onto the exe
  or its shortcut, launches straight into the Convert page with that file already staged
  instead of an empty shell you then have to feed manually. The extension list is kept in
  sync with the format catalogue by a real test (`InstallerFileAssociationTests`) that
  reads the actual `.iss` file off disk and fails the moment they drift apart
- **Single instance** — opening a second file while MediaSuite is already running (via
  "Open with", a dragged file, or just launching it again) hands that file to the window
  already open and brings it to the front, instead of spawning a redundant second process
  with its own queue, settings writer and tool discovery. A named mutex decides who is
  first; every later launch forwards its file arguments to that instance over a local
  named pipe and exits immediately rather than doing any startup work of its own. Backed
  by a real automated test (`MediaSuite.App.Tests`, new) that runs the actual mutex and
  named-pipe handoff — not a fake standing in for them — since "compiles" was never proof
  this specific mechanism (a second launch racing the first instance's listener startup)
  actually worked; that gap is exactly what caught the real startup-race bug fixed earlier
- **Real GitHub release** — every push to `main` publishes the CI-built installer to this
  repo's Releases page, gated so a PR build never does it. Before this, the only way to
  get the installer was a 90-day CI artifact behind a GitHub login, which quietly broke
  the promise the README and the in-app update checker both already made
- **Self-contained tool bundling** — `installer/fetch-tools.ps1` fetches all 14
  third-party tools during the CI build and packages them straight into the installer,
  so a fresh install needs zero manual downloads; a plain zip/7z download where one
  exists, and something more particular where it doesn't — LibRaw's `dcraw_emu.exe`
  compiled from source (no prebuilt binary exists anywhere), Ghostscript's installer
  payload extracted directly since its silent-install flag was removed upstream, and
  LibreOffice/Calibre installed silently onto the CI machine itself and harvested. Every
  one of the 14 was confirmed actually working from the real CI log, including two that
  needed a real fix after an honest first failure (a stale MuPDF version pin, caught by
  a 404 in CI and corrected) rather than being assumed to work
- **Update check** — on launch, if the setting is on, the app checks this repository's
  own GitHub releases for a newer tagged version and shows a dismissible banner if one
  exists; it never downloads or installs anything itself, only offers the release page.
  A failed check (offline, GitHub unreachable) fails silently — the banner just doesn't
  appear, since a network hiccup is not something a personal, offline-first app should
  ever interrupt startup to complain about
- **Polish** — tooltips across every non-obvious control; an actual empty state for a
  module page where nothing is ready yet, instead of a leftover "arrives in a later
  build step" message from before every module existed; keyboard mnemonics (Alt+letter)
  on the primary action of every page, which needed a real fix, not just markup — the
  custom button styles' `ContentPresenter` had no `RecognizesAccessKey`, so a `_`
  mnemonic would have rendered as a literal underscore instead of working
- **Final build** — version set to `1.0.0` in `Directory.Build.props` and kept in step in
  `installer/MediaSuite.iss`, so the installer's file name, `AppVersion` and the running
  app's own assembly version all agree; all 18 build steps from the brief are done

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

## Installer

`installer/MediaSuite.iss` is an [Inno Setup](https://jrsoftware.org/isinfo.php) script
that packages a self-contained `win-x64` publish of `MediaSuite.App` — no separate .NET
runtime install required — plus all 14 bundled tools (see
[Bundled tools](#bundled-tools)) into a traditional wizard-style installer: Program Files
under an elevation prompt, a Start Menu group, an optional desktop shortcut, and a real
uninstaller. Uninstall still deliberately leaves `tools\` and the user's settings folder
alone — a tool you've overridden with your own copy, or presets and Google Drive sign-in,
should never disappear just because the app did.

```powershell
installer\build.ps1
```

runs `dotnet publish`, then `installer/fetch-tools.ps1` (fetching the bundled tools), then
the Inno Setup compiler, and writes the finished installer to `dist\`. It needs the .NET 8
SDK, Inno Setup 6 (`iscc.exe`) — the script falls back to Inno Setup's default install
location if `iscc.exe` is not already on PATH — and, for the full tool set, an MSVC +
vcpkg toolchain and Chocolatey (both already present on a normal Windows 11 dev machine
with Visual Studio installed; a local build without them still succeeds, just without
LibRaw and Calibre, the two tools that specifically need them).

Unlike the app itself, this is something the CI in this repository can actually verify
end to end rather than only compile-check: `.github/workflows/mediasuite-ci.yml`'s
`installer` job runs the exact same publish-then-compile steps on `windows-latest`,
installing Inno Setup via Chocolatey, and uploads the resulting `.exe` as a build
artifact. A green run there means a real installer really was produced, not just that
the C# behind it compiles.

On every push to `main`, a third `release` job takes that same installer and publishes
it to this repo's Releases page — the actual download link for anyone who isn't building
from source, and the same `releases/latest` endpoint the in-app update checker (see
[What is done](#what-is-done)) already polls.

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
| 10 | AI upscaler (CUDA + CPU fallback) | done — Vulkan GPU path, plus an optional face-enhance pass (GFPGAN-ncnn, compiled from source, CPU-only) |
| 11 | Settings system — presets | done |
| 12 | Google Drive integration | done |
| 13 | Format-parity audit vs FreeConvert | done — spreadsheet/presentation/PostScript formats deliberately deferred, see the FormatCatalog doc comment |
| 14 | QA pass against real sample files | done — QA.md is the checklist for a human to run on real hardware; nothing here can execute a real conversion |
| 15 | Inno Setup installer | done — CI actually builds it end to end, see [Installer](#installer) |
| 16 | Update check | done — GitHub releases, check-and-prompt, never silent installs |
| 17 | Polish — tooltips, error and empty states, keyboard nav | done |
| 18 | Final build and handoff | done — version bumped to 1.0.0 in `Directory.Build.props` and the installer script |

## Licence note

This is a personal, non-distributed build, so the GPL/AGPL tools (Ghostscript, Calibre,
Pandoc, MuPDF) ship as-is. If it is ever distributed, those need replacing or licensing
commercially. RAR creation is not supported for the same reason — extraction only.
