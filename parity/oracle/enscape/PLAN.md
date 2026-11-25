# Enscape (ships in Dino 8 as "Outscape"): Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: none yet (the coordinator builds them). Installed here: Enscape 4.18.0.1529, with the plug-in menu entry for Rhino.

## Access route (the hardest program)
* A public scripting or command-line interface for rendering was not found; the vendor's SDK is sold, not published. So there is no clean Phase B channel
  equivalent to the other programs. What remains is ordinary licensed use:
  1. build the scene from our own analytic geometry inside Rhino 8 (already scripted and recorded),
  2. start Enscape and export images through its normal image-export function, with a supervised or screen-automated session,
  3. read the exported images with our own tooling and record statistics only.
* Rhino can start an installed plug-in's commands through the public command runner; that is ordinary use, not an Enscape API, and will be tried only if the owner agrees at setup.
* The program's assets (materials, vegetation, people, sky maps, shaders, icons, UI text, settings text) are never copied. Only numbers from our own scenes are kept.

## What it can measure (from exported images of our scenes)
| Family | Measured |
|---|---|
| Sky and sun | sky and ground luminance against time, date, latitude; sun disc direction from shadow length (h / tan(elevation)) |
| Exposure and tone mapping | grey-ramp patch values: the response curve; exposure steps |
| Camera | field of view and aspect: pixel positions of known edges; orthographic scale; depth of field blur radius from point features |
| Lights | illuminance on a plane against distance (inverse square) for point and spot lights; photometric-file distribution on a plane, using our own files in the public standard format |
| Materials | albedo to reflectance, roughness to highlight width, glass index to refraction offset, emissive intensity to luminance, with our own parameter values |
| Global illumination | white-furnace test, colour bleeding sample, ambient occlusion depth on a corner |
| Shadows and reflections | penumbra width, mirror reflectance against angle (Fresnel shape) |
| Post effects | bloom threshold response, vignette falloff |
| Cost | render time and memory per resolution (hardware-specific; recorded with the GPU name) |

## Tolerances
Real-time and accumulated output is noisy: each case is captured over N frames; the golden record stores mean and spread of region-of-interest luminance in linear values (EXR if the export allows, otherwise
8-bit with the tone curve measured). Dino 8 must fall inside the vendor's own spread. Geometric positions: within one pixel.

## Cannot be measured by us
Shaders and algorithms (no Phase D by this Observer), virtual reality, the asset library, proprietary denoiser and upscaler, anything needing the vendor's servers, behaviour on other GPUs.
These go to research (published rendering literature, physically based rendering standards) and Dino 8's own R&D, checked against the measured luminance curves.

## Needs from the owner
Confirmation that Enscape is licensed for the Rhino host, the GPU model, a supervised session in which images are exported while we watch, and a decision on whether screen automation may be used. Per-program G0: covered by `G0_BLANKET.md`;
the Chaos licence terms for Enscape are worth a read because this program has no public API for observation.
