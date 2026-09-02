# Bundled tools

MediaSuite shells out to third-party binaries instead of reimplementing decades of codec
work. They are **not** committed to git — but as of the installer's `fetch-tools.ps1`
step, most of them no longer need to be downloaded by hand either: CI fetches real
Windows binaries for the tools marked **auto-bundled** below and ships them straight
inside the installer, so a fresh install already has them in `tools\`. The remaining
tools still need a manual download and drop into this folder, tracked in
[installer/fetch-tools.ps1](../installer/fetch-tools.ps1)'s follow-up work — Settings →
Bundled tools always shows exactly what is and isn't present on your machine either way.

If you build the installer yourself with `installer\build.ps1`, it runs
`fetch-tools.ps1` for you before compiling, so a local build also comes out
self-contained for the auto-bundled tools.

The locator checks, in order:

1. an explicit per-tool path set in Settings;
2. `<tools>/<folder>/<exe>` and `<tools>/<folder>/bin/<exe>`, so most vendor archives can
   be unpacked as-is;
3. `<tools>/<exe>`, for a binary dropped straight in;
4. your system PATH, as a development convenience.

`<tools>` is the folder set in Settings, then `%MEDIASUITE_TOOLS_DIR%`, then `tools\`
next to `MediaSuite.exe`.

| Folder | Binary | Used for | Licence | Auto-bundled? | Download (if not) |
| --- | --- | --- | --- | --- | --- |
| `ffmpeg` | `ffmpeg.exe`, `ffprobe.exe` | Video, audio, GIF — **required** | LGPL-2.1 (shared build) | **yes** | — |
| `imagemagick` | `magick.exe` | Images — **required** | ImageMagick License | **yes** | — |
| `libvips` | `vipsthumbnail.exe` | Fast path for big images and batches | LGPL-2.1 | **yes** | — |
| `libraw` | `dcraw_emu.exe` | Camera RAW — **required** | LGPL-2.1 / CDDL | attempted (compiled from source) | see note below |
| `mupdf` | `mutool.exe` | PDF rendering and page tools | AGPL-3.0 | attempted (pinned version, unverified) | see note below |
| `qpdf` | `qpdf.exe` | Lossless PDF page operations | Apache-2.0 | **yes** | — |
| `ghostscript` | `gswin64c.exe` | PDF compression and flattening | AGPL-3.0 | attempted (unofficial extraction) | see note below |
| `pandoc` | `pandoc.exe` | Document conversion | GPL-2.0-or-later | **yes** | — |
| `libreoffice` | `soffice.exe` | High-fidelity DOCX to PDF | MPL-2.0 | not yet | https://www.libreoffice.org/download/download-libreoffice/ |
| `calibre` | `ebook-convert.exe` | Ebook conversion | GPL-3.0 | not yet | https://calibre-ebook.com/download_windows |
| `7zip` | `7z.exe` | Archives, RAR extraction | LGPL-2.1 (+ unRAR licence) | **yes** | — |
| `realesrgan` | `realesrgan-ncnn-vulkan.exe` | AI upscaling | BSD-3-Clause | **yes** | — |
| `rsvg` | `rsvg-convert.exe` | SVG rasterising | LGPL-2.1 | attempted (inclusion unconfirmed) | see note below |
| `potrace` | `potrace.exe` | PNG to SVG tracing | GPL-2.0 | **yes** | — |

"Not yet" is a real gap, not a permanent one. `libraw` is the one tool with no official
or actively-maintained prebuilt Windows binary at all, so `fetch-tools.ps1` compiles
`dcraw_emu.exe` from LibRaw's own source instead — real, but meaningfully more
speculative than every plain-download tool above it: it needs a working MSVC + vcpkg
toolchain in CI (set up as a dedicated workflow step) and is written to fail soft rather
than take the rest of the fetch down if that compile doesn't work on a given run. If it
fails, ImageMagick's own bundled LibRaw delegate (`magick.exe photo.CR2 out.jpg`) is the
practical fallback for camera RAW. `ghostscript` is attempted the same fail-soft way, for
a different reason: it only ships a GUI installer, and Artifex deliberately removed the
installer's silent-install flag in 10.01.0+, so `fetch-tools.ps1` extracts the installer's
payload directly with 7-Zip instead of running it — unofficial, since Artifex never
blessed extracting it this way, so it's written to not take the rest of the fetch down if
a future installer format change breaks it. `mupdf` is attempted too, for a third reason:
mupdf.com is unreachable from the environment `fetch-tools.ps1` was written in, so the
pinned version in its URL was confirmed only via web search, not a direct HTTP check —
plausibly stale by the time this runs, hence also fail-soft. `rsvg` is attempted from
`wingtk/gvsbuild`'s GTK4 bundle, which includes librsvg as a dependency and normally
produces `rsvg-convert.exe` alongside it — but that inclusion was inferred, not confirmed
by browsing the actual zip contents, so it's fail-soft too. `libreoffice` and `calibre`
both have a real acquisition path identified (a silent-installable MSI/installer) but
aren't wired into `fetch-tools.ps1` yet — tracked there as the next pass.

Settings → Bundled tools re-scans on demand and shows exactly where each binary was found
or where it is expected.

## Real-ESRGAN and CUDA

The download above is the ncnn/Vulkan build, which works on any GPU and is the fallback.
For the CUDA path on the RTX workstation GPU, build or fetch the CUDA variant and put its
executable in `realesrgan/`; GPU detection and the CPU fallback land in build step 10.
