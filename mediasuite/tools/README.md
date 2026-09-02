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
| `imagemagick` | `magick.exe` | Images — **required** | ImageMagick License | not yet | https://imagemagick.org/script/download.php |
| `libvips` | `vipsthumbnail.exe` | Fast path for big images and batches | LGPL-2.1 | not yet | https://github.com/libvips/build-win64-mxe/releases |
| `libraw` | `dcraw_emu.exe` | Camera RAW — **required** | LGPL-2.1 / CDDL | not yet | https://www.libraw.org/download |
| `mupdf` | `mutool.exe` | PDF rendering and page tools | AGPL-3.0 | not yet | https://mupdf.com/releases |
| `qpdf` | `qpdf.exe` | Lossless PDF page operations | Apache-2.0 | **yes** | — |
| `ghostscript` | `gswin64c.exe` | PDF compression and flattening | AGPL-3.0 | not yet | https://www.ghostscript.com/releases/gsdnld.html |
| `pandoc` | `pandoc.exe` | Document conversion | GPL-2.0-or-later | **yes** | — |
| `libreoffice` | `soffice.exe` | High-fidelity DOCX to PDF | MPL-2.0 | not yet | https://www.libreoffice.org/download/download-libreoffice/ |
| `calibre` | `ebook-convert.exe` | Ebook conversion | GPL-3.0 | not yet | https://calibre-ebook.com/download_windows |
| `7zip` | `7z.exe` | Archives, RAR extraction | LGPL-2.1 (+ unRAR licence) | **yes** | — |
| `realesrgan` | `realesrgan-ncnn-vulkan.exe` | AI upscaling | BSD-3-Clause | **yes** | — |
| `rsvg` | `rsvg-convert.exe` | SVG rasterising | LGPL-2.1 | not yet | https://gitlab.gnome.org/GNOME/librsvg |
| `potrace` | `potrace.exe` | PNG to SVG tracing | GPL-2.0 | **yes** | — |

"Not yet" is a real gap, not a permanent one — `imagemagick` and `libraw` are both
**required** and still manual today because they need more than a plain zip download
(a portable ImageMagick build and a Windows `dcraw_emu` binary both need verifying
before CI can fetch them unattended); the rest ship only as GUI installers that need
silent-install wiring. Tracked as the next pass on `installer/fetch-tools.ps1`.

Settings → Bundled tools re-scans on demand and shows exactly where each binary was found
or where it is expected.

## Real-ESRGAN and CUDA

The download above is the ncnn/Vulkan build, which works on any GPU and is the fallback.
For the CUDA path on the RTX workstation GPU, build or fetch the CUDA variant and put its
executable in `realesrgan/`; GPU detection and the CPU fallback land in build step 10.
