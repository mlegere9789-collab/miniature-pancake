# Grasshopper (and plug-ins): Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: 773. Installed here: Grasshopper 1 as shipped with Rhino 8.36.26296.17001 (plug-ins: none inventoried yet).

## Access route
* Inside Rhino 8's Python 3 environment, build a document through the public Grasshopper SDK types: create a document, add components and parameters by
  looking them up in the public component server by name, wire them, set inputs, solve, and read output data. Component names are recorded as
  identifiers only; descriptions and tooltips are never read into the corpus.
* Same launch method as the Rhino runner (a driver script run inside Rhino), so builds and timings are directly comparable.
* No canvas is needed; the document is solved in memory.

## What it can measure
| Family | Measured |
|---|---|
| Data trees | path structure and item counts after graft, flatten, simplify, path mapper, partition, shift, and list matching (shortest, longest, cross) |
| Maths and sets | series, range, domain, sort, dispatch, expression language results (operators, functions, precedence, domain errors) |
| Vector, curve, surface | outputs (lengths, areas, points) from known inputs; these coincide with Rhino kernel behaviour, so they cross-check the Rhino corpus |
| Mesh and intersect | counts, areas, intersection point counts |
| Fields and transforms | numeric field values, transform matrices |
| Solver behaviour | expiration order, number of solutions, errors and warnings raised (types, not text), null and empty handling |
| Sliders and parameters | clamping, rounding, range rules, persistent data |
| Plug-ins (one corpus each) | numbers produced by their public components on closed-form inputs: Kangaroo (short fixed-iteration solves only), Ladybug and Honeybee (needs external simulation engines), Weaverbird, Pufferfish, LunchBox, Human, Elefront, Wallacei, Galapagos (seeded, tiny budgets) |

## Tolerances
Pure math: relative 1e-9. Geometry components: the Rhino classes. Tree structure: exact. Solver physics (Kangaroo) and stochastic search (Galapagos): statistics only,
fixed seeds and iteration counts, stored with run-to-run spread.

## Cannot be measured headlessly
Canvas layout, wire drawing, preview display, scribbles, UI interaction, Rhino.Inside links to other programs, anything needing an external engine we do not have installed.

## Unknowns (routed to research and own R&D)
Plug-in algorithms with non-published solvers (Kangaroo goal weighting details, Galapagos selection mechanics); only outputs are matched.

## Needs from the owner
Name the plug-ins that matter (the Register's 773 rows presumably say which). Each is a separate free install, usually under open-source licences: if copyleft, behaviour only. Per-program G0: covered by `G0_BLANKET.md`;
Grasshopper rides on the same Rhino 8 licence (the Educational title noted in OBSERVER_DECISIONS.md applies).
