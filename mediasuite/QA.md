# QA runbook

Build step 14. This is a checklist for a person to run on a real Windows 11 machine
with the bundled tools actually installed — see [tools/README.md](tools/README.md).
It exists because this project was built in a sandbox with no Windows machine and none
of the bundled binaries, and the CI that checks every push has neither either. Every
other build step's tests prove the C# is correct against a fake standing in for each
tool; nothing in this repository has ever verified that a real conversion actually
comes out right, and nothing can, from where this was built. `tests/MediaSuite.Core.Tests/Qa/RealToolSmokeTests.cs`
is the one exception, and it only proves a brand-new install with no tools yet fails
cleanly rather than crashing — see its own doc comment for exactly what it does and
does not cover.

Work through each section with real files, not the placeholders the automated tests
use. Small, real-world files are enough — a phone photo, a minute of video, a few-page
PDF. Where a specific edge case is called out below, it exists because it is exactly
where an earlier build step found (and fixed) a real bug, or made a deliberate call
that is easy to mistake for a bug — check those first.

## Setup

1. Follow [tools/README.md](tools/README.md) to put every bundled binary in place, then
   open Settings and confirm the tool list shows everything found — the app runs
   without them, but a QA pass is only real once every tool is actually there.
2. `dotnet run --project mediasuite\src\MediaSuite.App`.
3. Gather a small folder of sample files: a JPG and a HEIC or RAW photo, a short MP4, an
   MP3, a multi-page PDF, a DOCX, an EPUB, a ZIP with a few files in it.

## Image module

- [ ] Convert a JPG to PNG, WebP and back — check the output actually opens and looks
  right, not just that a file appears.
- [ ] Convert a camera RAW file (NEF/CR2/ARW/whatever you have) to JPG. RAW is
  decode-only in this app — confirm RAW never appears as an output format choice.
- [ ] Convert a phone photo shot in portrait. EXIF auto-orient runs before every other
  operation — confirm the output is right-side up, not sideways.
- [ ] Trace a PNG with sharp edges (a logo, not a photo) to SVG via PNG to SVG, and
  confirm the traced result is recognisable.
- [ ] Resize with "keep aspect" on and off, and with "only shrink" on a file already
  smaller than the target — confirm it is left alone rather than enlarged.
- [ ] Compress a JPG at Quick, Balanced and Best — confirm the file size actually drops
  in that order and Best still looks acceptable.

## Video and audio module

- [ ] Convert an MP4 that is already H.264/AAC to MP4 with no explicit codec chosen —
  this should remux (near-instant) rather than re-encode. Change the codec explicitly
  and confirm it now takes noticeably longer (a real re-encode).
- [ ] Trim a 30-second piece out of a multi-minute file and confirm it starts almost
  immediately rather than crawling — the seek happens before the input, not after.
- [ ] Extract audio from a video to MP3 and confirm no video track ended up in the
  output.
- [ ] Convert audio to AC-3 — new in this build step, confirm it is not silently falling
  back to AAC or failing.
- [ ] Rename a copy of a working MP4 to `.qt` and drop it in — confirm the app
  recognises it as a video file rather than "unknown format" (it is an alias for MOV,
  not a separate codec path).
- [ ] Compress a video by target size rather than quality, and confirm the resulting
  file lands close to the number you asked for.

## GIF module

- [ ] Video to GIF on a clip with real color variety — confirm the palette looks close
  to the source, not the washed-out result FFmpeg's default web-safe palette gives.
- [ ] Video to GIF on a clip with an odd pixel width or height, converted back to MP4 —
  confirm the round trip doesn't fail (H.264 cannot encode an odd dimension, so this
  gets rounded down automatically).
- [ ] Build a GIF from a folder of still images and confirm each frame holds for the
  duration you set, not a uniform default.
- [ ] Compress an existing GIF at Quick vs Best and confirm Quick actually produces the
  smaller, rougher-looking file.

## PDF module

- [ ] Merge three PDFs and confirm the page order matches the order you added them in,
  not alphabetical.
- [ ] Run PDF Converter on a mixed batch — some PDFs, some JPGs — with the format picker
  set to JPG. Confirm the PDFs in the batch still come out as real PDFs, correctly
  named, not JPGs containing binary PDF data. (This was a real, fixed bug — regression
  test it specifically.)
- [ ] Remove a couple of pages from the middle of a multi-page PDF and confirm the page
  count and remaining page order are right.
- [ ] Crop a PDF and confirm the margins are trimmed uniformly across every page, not
  just the first.
- [ ] Compress at Quick, Balanced and Best and confirm the file size drops in that order
  (screen/ebook/prepress Ghostscript presets).
- [ ] Protect a PDF with a password, then unlock it with the same password, and confirm
  the unlocked copy opens with no password prompt.

## Document and ebook module

- [ ] Convert a DOCX to PDF and to plain TXT — confirm formatting survives reasonably in
  the PDF, and that TXT keeps paragraph breaks rather than running everything together.
- [ ] Convert a legacy `.doc` file (not `.docx`) to PDF. This always routes through
  LibreOffice rather than Pandoc — confirm it actually produces a non-empty file rather
  than silently doing nothing (LibreOffice can exit success having written nothing; the
  engine is supposed to catch that and fail the job instead of reporting success).
- [ ] Convert an EPUB to MOBI or AZW3 via Calibre and confirm the result opens in a
  reader.
- [ ] Convert a PDF to EPUB and back and confirm the round trip is at least readable.

## Archive module

- [ ] Convert a ZIP with several files to 7Z, then back to ZIP, and confirm every file
  survives with the same contents.
- [ ] Convert a folder's worth of files, zipped, to GZIP. GZIP holds one stream, so this
  always bundles into an intermediate TAR first — confirm the result is a real
  `.tar.gz`-shaped archive that a real tool can open, not a GZIP holding only one of the
  files.
- [ ] Extract a RAR archive. Confirm RAR never appears as an output/target format choice
  anywhere — extraction only.

## AI upscaler

- [ ] Upscale a small photo at 2x and 4x with the general model and confirm the output
  dimensions are exactly double/quadruple the input's.
- [ ] Upscale at 8x and confirm the output is truly 8x, not capped at 4x — this runs as
  two chained passes (4x then 2x) rather than a single "-s 8", so it is worth checking
  specifically.
- [ ] Upscale with sharpen on and confirm the result is visibly sharper than the same
  job with sharpen off.
- [ ] Try the anime model on an anime-style image and the general model on a photo, and
  confirm the difference is noticeable — they should not look interchangeable.
- [ ] Confirm "face enhance" is not offered anywhere in the UI — it was deliberately not
  implemented (GFPGAN is not bundled) rather than faked with the general model.

## Custom presets

- [ ] Pick a tool, select the Custom preset, type a couple of real advanced options (an
  operation-appropriate `key=value` pair, e.g. `crf=20` for a video tool), and run a
  job — confirm the option actually took effect (check the output, not just that the
  job succeeded).
- [ ] Save that as a named preset, switch to a different tool and back, and confirm the
  saved preset is still there and reselecting it restores the same options text.
- [ ] Delete a saved preset and confirm the advanced-options box and name field clear
  rather than showing the just-deleted preset's leftover text.

## Google Drive

- [ ] Follow the setup steps in the README's Google Drive section (a real Google Cloud
  OAuth client is required — there is no bundled default) and sign in from Settings.
- [ ] Run a job with "Also upload to Google Drive" checked and confirm the file actually
  appears in your Drive, in the folder you picked (or "New folder" if you created one).
- [ ] Sign out, then run another upload-checked job without signing back in, and confirm
  the job still completes — the local file should exist and the job should show a
  warning about the failed upload, not fail outright.
- [ ] Confirm the app can only see/list folders it has access to under the `drive.file`
  scope — it should not be able to browse your entire Drive, only what it created plus
  what you explicitly picked through its own folder picker.

## Icon, branding and taskbar

Everything in this section is exactly the kind of thing nothing in this sandbox could
ever check — it's either purely visual or depends on the real Windows taskbar/shell,
neither of which CI or a fake process runner can see.

- [ ] Confirm `MediaSuite.exe` shows the real icon (a white two-arrow exchange glyph on
  an indigo-to-cyan gradient), not a generic default, in: the taskbar while running, the
  title bar top-left corner, the Start Menu entry, the desktop shortcut (if created), and
  Explorer's own icon for the exe file itself.
- [ ] Run `installer\build.ps1` (or grab the CI-built installer) and confirm
  `MediaSuiteSetup-1.0.0.exe` itself shows the same icon before you even run it, and that
  the wizard's own pages (welcome, every options page, finished) show the branded panel
  and small badge instead of Inno Setup's stock blue/white artwork.
- [ ] Open Settings and confirm the About card at the bottom shows "MediaSuite v1.0.0"
  and that "View on GitHub" actually opens the real project repo in your browser.
- [ ] Start a real conversion job (anything slow enough to watch — a video convert or an
  upscale, not an instant remux) and confirm the taskbar icon itself shows a progress
  overlay while it runs, not just the in-window progress bar. Pause the job and confirm
  the taskbar icon switches to a paused-looking state rather than looking stalled. Run
  two jobs at once and confirm the taskbar progress reflects roughly their average, not
  just one of them. Let every job finish and confirm the taskbar progress overlay clears
  back to nothing rather than getting stuck at 100%.
- [ ] Right-click `MediaSuite.exe` → Properties → Details tab, and confirm the file
  description, product name, company and version all show something real rather than
  blank or a generic ".NET application" default.
- [ ] Right-click a supported file (a JPG or MP4 is enough) → Open with → confirm
  MediaSuite is offered as a choice, and that picking it launches (or reuses, if already
  running a second instance) the app with the file already staged on the Convert page.
  Confirm the file's actual default app is unchanged — MediaSuite must only ever be
  offered, never take over the double-click action. Drag a file directly onto
  `MediaSuite.exe` or its Start Menu/desktop shortcut and confirm the same thing happens.
  Try an unsupported extension (e.g. `.xlsx`) the same way and confirm MediaSuite does
  not launch at all rather than opening empty.
- [ ] Close the app, then re-launch it with no file argument at all (Start Menu, desktop
  shortcut, or `MediaSuite.exe` with no arguments) and confirm it opens to the Convert
  page with nothing pre-staged — the file-preload path must never leak into a normal
  launch.
- [ ] With MediaSuite already open, launch it again from the Start Menu (or run
  `MediaSuite.exe` a second time) and confirm no second window appears — the existing
  window should just come to the front. Minimize it first and confirm the second launch
  restores it rather than leaving it minimized in the background. With it already open,
  open a supported file via "Open with" or drag one onto the exe and confirm the file
  lands on the Convert page of the *existing* window (still just one window, one process
  in Task Manager), not a second instance.

## Known, deliberate gaps — do not report these as bugs

- **AI upscaler**: no "face enhance" model (needs GFPGAN, not bundled).
- **Archive**: RAR is extraction-only; creating a RAR archive needs a licensed encoder.
- **Format catalogue**: spreadsheet and presentation formats (XLSX, PPTX, ODS, ODP,
  etc.), PostScript, and MIDI are not supported — see the doc comment on
  `FormatCatalog` for why each one specifically was left out rather than added and
  hoped to work.
- **Video/audio formats**: VOB, F4V/F4P and AMR are recognised as inputs only, never as
  output choices — their encoders are either not something this app should ever
  produce (VOB, F4V) or not reliably present in a standard FFmpeg build (AMR).
- **Licensing**: Ghostscript, Calibre, Pandoc and MuPDF ship as-is for this personal,
  non-distributed build. If this is ever distributed, that needs revisiting.
