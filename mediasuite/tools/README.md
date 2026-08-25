# Bundled tools

MediaSuite shells out to third-party binaries instead of reimplementing decades of codec
work. They are **not** committed to git — download them and unpack them here.

The locator checks, in order:

1. an explicit per-tool path set in Settings;
2. `<tools>/<folder>/<exe>` and `<tools>/<folder>/bin/<exe>`, so most vendor archives can
   be unpacked as-is;
3. `<tools>/<exe>`, for a binary dropped straight in;
4. your system PATH, as a development convenience.

`<tools>` is the folder set in Settings, then `%MEDIASUITE_TOOLS_DIR%`, then `tools\`
next to `MediaSuite.exe`.

| Folder | Binary | Used for | Licence | Download |
| --- | --- | --- | --- | --- |
| `ffmpeg` | `ffmpeg.exe`, `ffprobe.exe` | Video, audio, GIF — **required** | LGPL-2.1 (shared build) | https://www.gyan.dev/ffmpeg/builds/ |
| `imagemagick` | `magick.exe` | Images — **required** | ImageMagick License | https://imagemagick.org/script/download.php |
| `libvips` | `vipsthumbnail.exe` | Fast path for big images and batches | LGPL-2.1 | https://github.com/libvips/build-win64-mxe/releases |
| `libraw` | `dcraw_emu.exe` | Camera RAW — **required** | LGPL-2.1 / CDDL | https://www.libraw.org/download |
| `mupdf` | `mutool.exe` | PDF rendering and page tools | AGPL-3.0 | https://mupdf.com/releases |
| `qpdf` | `qpdf.exe` | Lossless PDF page operations | Apache-2.0 | https://github.com/qpdf/qpdf/releases |
| `ghostscript` | `gswin64c.exe` | PDF compression and flattening | AGPL-3.0 | https://www.ghostscript.com/releases/gsdnld.html |
| `pandoc` | `pandoc.exe` | Document conversion | GPL-2.0-or-later | https://github.com/jgm/pandoc/releases |
| `libreoffice` | `soffice.exe` | High-fidelity DOCX to PDF | MPL-2.0 | https://www.libreoffice.org/download/download-libreoffice/ |
| `calibre` | `ebook-convert.exe` | Ebook conversion | GPL-3.0 | https://calibre-ebook.com/download_windows |
| `7zip` | `7z.exe` | Archives, RAR extraction | LGPL-2.1 (+ unRAR licence) | https://www.7-zip.org/download.html |
| `realesrgan` | `realesrgan-ncnn-vulkan.exe` | AI upscaling | BSD-3-Clause | https://github.com/xinntao/Real-ESRGAN/releases |
| `rsvg` | `rsvg-convert.exe` | SVG rasterising | LGPL-2.1 | https://gitlab.gnome.org/GNOME/librsvg |
| `potrace` | `potrace.exe` | PNG to SVG tracing | GPL-2.0 | https://potrace.sourceforge.net/#downloading |

Settings → Bundled tools re-scans on demand and shows exactly where each binary was found
or where it is expected.

## Real-ESRGAN and CUDA

The download above is the ncnn/Vulkan build, which works on any GPU and is the fallback.
For the CUDA path on the RTX workstation GPU, build or fetch the CUDA variant and put its
executable in `realesrgan/`; GPU detection and the CPU fallback land in build step 10.
