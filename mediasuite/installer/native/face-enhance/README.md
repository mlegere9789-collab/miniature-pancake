# Face Enhance native source

`face.h`/`face.cpp` and `gfpgan.h`/`gfpgan.cpp` are vendored, unmodified, from
[Qengineering/GFPGAN-ncnn-Raspberry-Pi-4](https://github.com/Qengineering/GFPGAN-ncnn-Raspberry-Pi-4)
(BSD-3-Clause — see `LICENSE-THIRD-PARTY.txt`), itself built on
[FeiGeChuanShu/GFPGAN-ncnn](https://github.com/FeiGeChuanShu/GFPGAN-ncnn)'s ncnn port of
[TencentARC/GFPGAN](https://github.com/TencentARC/GFPGAN). Face restoration through GFPGAN
needs two supporting models this same source tree carries: a `yolov5-blazeface` face
detector, and the `GFPGANCleanv1-NoCE-C2` weights (`encoder`/`style`) themselves.

`face_enhance.cpp` is MediaSuite's own driver — see the file's own header comment for how
and why it differs from the original demo it is built against.

Compiled at installer build time by `installer/fetch-tools.ps1` (`opencv4` and `ncnn` via
vcpkg, then `cl.exe` directly against these four files — see the CI workflow's "Set up
MSVC dev environment" step), the same from-source pattern already used there for LibRaw's
`dcraw_emu`. No official prebuilt Windows binary exists for this pipeline at all, so this
is a real compile of real source producing a real `.exe`, not a placeholder standing in
for one. Also fetched there: the three model files, from the same repository's own
release.

CPU-only — GFPGAN-ncnn's own Vulkan support is still an open upstream TODO as of this
writing, and this driver does not attempt to force it on. A single face is a much smaller
network pass than upscaling a whole image, so this is expected to cost real but tolerable
seconds per face, not minutes.
