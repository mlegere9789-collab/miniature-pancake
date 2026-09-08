# Why there is no native DWG, Parasolid, or ACIS support

Dino 8 reads and writes `.3dm` (its own OpenNURBS-based format), `.obj`,
`.stl`, `.ply`, `.dxf`, `.igs`/`.iges`, and `.stp`/`.step`. It does **not**
read or write `.dwg`, and it has no Parasolid (`.x_t`/`.x_b`) or ACIS
(`.sat`/`.sab`) kernel or import/export. This is not an oversight or a
missing afternoon of work — each of these formats is a proprietary,
commercially-licensed piece of technology, and there is no legal, from-scratch
path to reimplementing them the way this project reimplemented, say, its own
NURBS/mesh/boolean kernel or its DXF and STEP readers.

## DWG

`.dwg` is Autodesk's native drawing format. It is not an open, published
specification like DXF (which Autodesk does document publicly, and which
this project does support — see `dino8-app/src/io/FileExchange.cpp`'s
`ExportDxf`/`ImportDxf`). There are exactly two legitimate ways to read or
write real-world `.dwg` files:

1. **License the Open Design Alliance's (ODA) Teigha/ODA SDK.** The ODA is a
   consortium (its members include Autodesk's DWG competitors and many CAD
   vendors) that reverse-engineered DWG under a cooperative, dues-funded
   effort and licenses the result — the "Kernel SDK" / "Drawings SDK" — to
   member companies. Membership + SDK licensing is commercial: dues are
   tiered by company revenue and are not published as a fixed price list;
   historically this has run from roughly **$5,000 to $50,000+ per year**
   depending on tier and which SDK modules are needed, on top of a
   non-disclosure agreement covering the SDK's internals. It is licensed to
   companies, not distributed as a redistributable open-source library, and
   its terms prohibit reverse-engineering or re-exposing the SDK's own
   internals — so it could be *used* by Dino 8 as a linked dependency, but
   its code could never be vendored into this repository or reimplemented
   from what it reveals.
2. **License DWG technology directly from Autodesk**, e.g. via Autodesk's
   RealDWG SDK. Autodesk does not offer RealDWG as a general-purpose,
   freely-licensable SDK for arbitrary third-party open-source projects; it
   is generally licensed to specific partners under negotiated terms, and
   Autodesk has historically restricted or discontinued RealDWG licensing
   for products that would directly compete with AutoCAD-family products —
   which a free Rhino-alternative like Dino 8 plainly is. This path is not
   realistically available to this project at all.

There is no third option. Nobody has ever produced a legally clean,
from-scratch, public-domain DWG reader/writer, because DWG is not a published
format — it is a proprietary binary format whose structure is a trade
secret, discovered only through the ODA's licensed, NDA'd reverse-engineering
work (and Autodesk's own internal implementation). Reimplementing DWG support
"from scratch" without a license from the ODA or Autodesk would mean either:

- **Independently reverse-engineering `.dwg` from scratch** — reading
  arbitrary sample files byte-by-byte and inferring the format. This is a
  multi-year undertaking even for a well-resourced team (it took the ODA's
  member consortium years, and DWG's internal structure has changed release
  to release since AutoCAD's earliest versions); realistically it could not
  be done correctly or completely by any near-term effort, would trail every
  new AutoCAD release, and would produce a reader/writer that silently
  mishandles or corrupts entity types nobody happened to test.
- **A real legal risk even if attempted.** DWG is an actively maintained,
  actively licensed proprietary format; Autodesk has a documented history of
  contesting unlicensed DWG compatibility efforts (the "OpenDWG"/"TrustedDWG"
  history and Autodesk's public statements framing unlicensed DWG read/write
  as encroaching on its IP are part of the public record around why the ODA
  exists as a licensed alternative in the first place). Shipping an
  unlicensed DWG implementation in a public, redistributed free tool is not
  a hypothetical risk — it invites exactly the kind of claim the ODA's
  licensing model exists to avoid.

**Bottom line**: native DWG support is available to Dino 8 only by the
project owner obtaining an ODA membership (a recurring five-figure business
expense with an NDA) and linking their SDK, or by Autodesk granting a RealDWG
license (unlikely for a competing free product). Documenting this honestly
is the deliverable here; no DWG code has been added, and none should be
written without one of those two licenses in hand.

## Parasolid and ACIS

Parasolid and ACIS are the two dominant commercial B-rep (boundary
representation) solid-modeling kernels in the CAD industry — the actual
geometric-modeling engines that many commercial CAD tools (including real
Rhino's own optional Parasolid-based `.x_t` import, and many others) are
built on or interoperate with:

- **Parasolid** is owned by **Siemens Digital Industries Software**
  (Siemens PLM). It is licensed to OEMs and end users under commercial
  agreements (per-seat and/or per-deployment licensing negotiated directly
  with Siemens); there is no public price list and no open or community
  license — it is closed-source, commercially licensed software, full stop.
  `.x_t` (text) and `.x_b` (binary) are its native file formats.
- **ACIS** is owned by **Spatial Corp.** (a Dassault Systèmes subsidiary).
  Same shape: commercially licensed SDK, closed-source, negotiated
  per-product/per-seat terms with Spatial. `.sat` (text) and `.sab` (binary)
  are its native formats.

Both kernels represent decades of specialized, patented geometric-modeling
engineering (exact NURBS boolean operations, robust numerical tolerancing,
fillet/blend/shell algorithms tuned over 30+ years of CAD industry use) —
this is precisely the kind of proprietary, IP-protected core technology that
a from-scratch open project cannot legally or practically reproduce:

- **Legally**: both companies hold active patents covering specific
  algorithms within their kernels (robust boolean evaluation, blending,
  healing), and both formats are proprietary/trade-secret file formats, not
  published specifications. Reimplementing "a kernel that reads `.sat`/`.x_t`
  files and reproduces their exact geometry" without a license risks the
  same category of IP exposure as unlicensed DWG, compounded by active
  patent portfolios specific to solid-modeling algorithms.
- **Practically**: this is not "add another importer." Dino 8 already has
  its own NURBS/mesh/boolean geometry kernel (`dino8-kernel`, built on
  OpenNURBS + Manifold) — that from-scratch work is realistic because
  OpenNURBS is itself a genuinely open, BSD-licensed, publicly documented
  library that Rhino's own developers released for exactly this purpose, and
  Manifold is a from-scratch, permissively-licensed mesh boolean library.
  Parasolid and ACIS have no equivalent open counterpart: there is no
  published spec to implement against, so a from-scratch clone would mean
  independently deriving both the exact undocumented file formats and
  reproducing 30 years of a commercial team's numerically robust B-rep
  algorithm work, well beyond a "multi-year" effort for even a large team,
  with no guarantee of ever matching real-world file fidelity.

**Bottom line**: the only legitimate way for Dino 8 to read/write
Parasolid or ACIS files is for the project owner to negotiate and pay for a
commercial SDK license directly with Siemens (Parasolid) or Spatial/Dassault
(ACIS). No such integration exists in this codebase, and none should be
attempted without those licenses — this document, stating that plainly, is
the intended deliverable for this limitation.

See `dino8-app/docs/CODE_SIGNING.md` for the equivalent honest accounting of
why installer signing needs a purchased certificate rather than code.
