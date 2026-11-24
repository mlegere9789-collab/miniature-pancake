# Archicad: Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: 924.

## Access route
* The official JSON command interface through the official Python package, talking to a running Archicad instance on a local port
  (the project is opened by the owner or by our script through a public command). Archicad is not headless: the application runs with a
  new, empty project in the background.
* For measurements the JSON interface does not expose, a small C++ add-on built against the public development kit (the kit is a free download; the
  C++ build tools are already installed). Add-ons load only through the standard add-on folder and need the owner's one-time trust.
* Third-party add-on command sets (community JSON extensions) are not used unless the owner chooses to; they are not vendor-supported.
* Library parts and their scripts are vendor assets: not opened, not copied. Only parameter values we set and read through the API are recorded.

## What it can measure
| Family | Measured |
|---|---|
| Walls, slabs, roofs, shells, morphs | built-in computed quantities (area, volume) read through property-value commands; element counts; bounding data returned by geometry queries |
| Wall junctions and solid element operations | quantity changes after cleanup and after subtractive operations |
| Composites and building materials | per-layer quantities for layered structures we define |
| Zones | area, perimeter, bounding results for known room shapes |
| Structure | beam and column profile areas and volumes, intersections |
| Stairs, curtain walls | counts of treads, risers, panels, frames |
| Object parameters | get and set round-trips and value ranges for parameters of objects (names recorded as identifiers) |
| Working units | rounding and conversion numbers |
| Classification and properties | assigning and reading values (identifiers and numbers only) |
| IFC and other exchange | IFC export: entity counts and base quantities; DWG round-trip counts |

## Tolerances
Archicad works in metres internally for most quantities: relative 1e-6 for areas and volumes, exact for counts, absolute 1e-6 m for coordinates.

## Cannot be measured headlessly
GDL internals and library behaviour, rendering engines, many modelling tools that the interface does not expose (their results are visible only
through the GUI), publisher and layout outputs, teamwork servers. Anything that would need driving menus is left to a supervised GUI session or to research.

## Unknowns (routed to research and own R&D)
Solid-element-operation priority rules, zone-boundary heuristics, slab and roof intersection ordering.

## Needs from the owner
Archicad installed (version), the Python package (free), and the development kit for the add-on. Per-program G0: covered by `G0_BLANKET.md`.
