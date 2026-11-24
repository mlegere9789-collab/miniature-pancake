# Autodesk Forma: Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: 186. Forma is a cloud service: there is no build string, only a service date and API version.

## Access route
* The public HTTP API documented for developers, with a token from an app the owner registers and authorises. Plus the embedded-view SDK, whose extension runs
  inside the Forma web application and talks to it through the SDK. API maturity (some parts were beta) must be confirmed at setup.
* Hard line: ordinary licensed use only. Single throwaway project, low request rate, no scraping, no interception of traffic, no probing of undocumented endpoints.
* The owner performs every sign-in and consent screen; the Observer never handles passwords or tokens beyond what the owner places in an environment variable.

## What it can measure
| Family | Measured |
|---|---|
| Project and proposal data | create, read, update of elements we author: counts, element representations returned for a known box or extrusion |
| Geometry round trips | send a closed-form building mass, read back vertex, face counts, bounding box, volume |
| Built-in area metrics | gross floor area, footprint, unit counts for massings with known floor heights and floor counts |
| Site | terrain and site-limit handling for a flat plane and a known slope |
| Analyses | where the API or extension SDK exposes results: sun hours and daylight on a single box at a known place and date (cross-checked against the analytic solar position), wind and noise only if exposed numerically |
| Library and integration | element types and metadata round trip (identifiers and numbers only) |

## Tolerances
Counts and area metrics: exact or relative 1e-6. Analysis grids: relative 1e-2 and recorded with the service date, because the service can change without a version bump.
Each golden record carries date, API version header and region.

## Cannot be measured headlessly (or at all by us)
Generative or machine-learning features (non-deterministic), UI, connectors to desktop programs, content under other licences, reproducibility across months.

## Unknowns (routed to research and own R&D)
The analysis solvers' physical models and defaults: Dino 8 implements published methods (solar geometry, standard wind and noise models) and is checked against the recorded grids.

## Needs from the owner
A Forma entitlement, an app registered for the developer interface, Node.js if the embedded-view route is used (not currently installed), and a decision on API rate limits. Per-program G0:
covered by `G0_BLANKET.md`; because it contacts a vendor server, this is the one program where the owner should read the developer-terms page once.
