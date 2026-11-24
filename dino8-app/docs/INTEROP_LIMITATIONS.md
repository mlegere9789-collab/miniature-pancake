# Why there is no native Parasolid or ACIS support (and how DWG works)

Dino 8 reads and writes `.3dm` (its own OpenNURBS-based format), `.obj`,
`.stl`, `.ply`, `.dxf`, `.dwg`, `.igs`/`.iges`, and `.stp`/`.step`. It has
**no** Parasolid (`.x_t`/`.x_b`) or ACIS (`.sat`/`.sab`) kernel or
import/export. This is not an oversight or a missing afternoon of work —
Parasolid and ACIS are proprietary, commercially-licensed pieces of
technology, and there is no legal, from-scratch path to reimplementing them
the way this project reimplemented, say, its own NURBS/mesh/boolean kernel or
its DXF and STEP readers.

## DWG

`.dwg` is Autodesk's native drawing format and, unlike DXF (which Autodesk
does document publicly), it is not a published specification either. This
document used to say flatly that there was no legal, from-scratch way to
support it, and named exactly two paths: an ODA SDK membership, or a
RealDWG license from Autodesk. **That was wrong** — it missed a third,
real option this project now uses: **GNU LibreDWG**
(https://www.gnu.org/software/libredwg/), a from-scratch, independently
reverse-engineered, GPLv3-licensed DWG reader/writer maintained by the FSF
and GNU project since 2009. It is not a derivative of the ODA's or
Autodesk's proprietary work, is not covered by either company's NDA, and is
distributed as ordinary open-source source code anyone can fetch, build, and
link against — which is exactly what `dino8-app/CMakeLists.txt` now does
(`FetchContent_Declare(libredwg ...)`), the same way FreeCAD's `importDWG`
addon and other free CAD tools get DWG support. See
`dino8-app/src/io/FileExchange.cpp`'s `ExportDwg`/`ImportDwg` and
`dino8-app/src/io/FileExchange.h` for what is actually implemented (LINE,
POINT, CIRCLE, ARC, LWPOLYLINE, block instances, layers and colours; text,
dimensions, hatches, splines and 3D solids/meshes are not read back yet) and
`dino8-app/THIRD_PARTY_LICENSES.md` for what linking GPLv3 code means for
the rest of this codebase's licensing — that part is a real, non-cosmetic
consequence, not a footnote.
This does **not** change the analysis below for Parasolid/ACIS: there is no
GPL, BSD, or otherwise open equivalent of LibreDWG for either of those
formats (see "Parasolid and ACIS" below for why), so the two-commercial-
licenses-or-nothing conclusion still stands there. The rest of this DWG
section is kept, lightly corrected, as the honest history of that mistake
and why the two commercial paths remain true statements about DWG *on their
own* — they are simply no longer the only paths.

There are two commercial ways to read or write real-world `.dwg` files
beyond LibreDWG's open-source path:

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

These two commercial paths share the same shape and the same real cost this
document originally described in detail: dues/licensing running from the
low five figures a year and up, an NDA covering the SDK's own internals, and
(for RealDWG specifically) Autodesk's documented history of not licensing it
to products that compete with AutoCAD. Neither is what this project actually
uses.

What this project *does* use — GNU LibreDWG — is a fundamentally different
thing from "reimplementing DWG from scratch for this project": it is an
existing, independent, `~15+`-year-old GNU project that already did that
reverse-engineering work as a public, GPL-licensed effort (in the same spirit
as, and unrelated to, the ODA's private one), the way Samba independently
reverse-engineered SMB/CIFS or Wine reverse-engineered the Win32 ABI. Using
it does carry real, non-hypothetical consequences of its own — GPLv3 is a
copyleft license, so linking it into Dino 8 puts the *combined, distributed
binary* under GPLv3 terms (see `dino8-app/THIRD_PARTY_LICENSES.md`) — but
that is a licensing-compliance question, not a "nobody has ever done this
legally" one. The risk profile the paragraphs above describe (Autodesk
contesting *unlicensed*, *reverse-engineered-by-this-project* DWG code) does
not apply to *using* an already-published, decade-plus-mature GPL library
the same way any other GPL dependency is used; it is the same category of
choice as any other GPL-vs-permissive dependency decision, not a novel IP
exposure this project is creating.

**Bottom line**: DWG read/write now works via GNU LibreDWG (see
`ExportDwg`/`ImportDwg` in `dino8-app/src/io/FileExchange.cpp`), with the
entity coverage documented there and in `dino8-app/src/io/FileExchange.h`.
The ODA-membership and RealDWG paths above remain the only ways to get
Autodesk's or the ODA's *own* proprietary DWG implementations (their
specific fidelity, their specific edge-case handling) directly — a project
that specifically needs that, rather than LibreDWG's independent
implementation, still needs one of those two commercial licenses.

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
  same category of IP exposure a from-scratch, reverse-engineered-by-this-
  project DWG reader would have risked (see the DWG section above for why
  this project uses an existing GPL implementation, GNU LibreDWG, instead of
  writing one), compounded by active patent portfolios specific to
  solid-modeling algorithms that DWG's format-only exposure does not carry.
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
