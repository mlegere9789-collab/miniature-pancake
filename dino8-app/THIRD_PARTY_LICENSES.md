# Third-party licenses

Dino 8 (this directory, `dino8-app`) and `dino8-kernel` pull in the following
third-party code via CMake `FetchContent`, plus one system dependency looked
up with `find_package`. This file exists to answer one question honestly:
**what does each dependency's license mean for a build of Dino 8 that
includes it?**

| Dependency | License | Pulled in by | What it means here |
|---|---|---|---|
| OpenNURBS | BSD-derived (MIT-like; see its own `LICENSE`) | `dino8-kernel/CMakeLists.txt` | Permissive; no obligations beyond attribution. |
| Manifold | Apache-2.0 | `dino8-kernel/CMakeLists.txt` | Permissive; attribution + a copy of the license/NOTICE with any redistribution. |
| GLFW | zlib/libpng license | `dino8-app/CMakeLists.txt` | Permissive; attribution only. |
| Dear ImGui | MIT | `dino8-app/CMakeLists.txt` | Permissive; attribution only. |
| Lua | MIT | `dino8-app/CMakeLists.txt` | Permissive; attribution only. |
| pybind11 | BSD-3-Clause | `dino8-app/CMakeLists.txt` (only when a system Python3 dev install is found) | Permissive; attribution only. |
| Python 3 (dev headers/libs) | PSF License | found via `find_package(Python3 ...)`, not vendored | Permissive; linked only when embedded scripting is built, and only against whatever Python the host machine already has installed. |
| **GNU LibreDWG** | **GPLv3** | `dino8-app/CMakeLists.txt` (DWG import/export, see `src/io/FileExchange.cpp`'s `ExportDwg`/`ImportDwg`) | **Copyleft — see below. This is the one dependency here with real obligations, not just attribution.** |

## GNU LibreDWG (GPLv3) — what this actually means

Every other dependency above is permissively licensed: using it imposes
essentially no obligations beyond keeping its copyright notice around.
LibreDWG is different, and it is worth being direct about that rather than
burying it in a table cell:

- **GPLv3 is copyleft.** Linking GPLv3 code into a program - even
  statically, even as "just one file's worth of functionality" - means the
  *combined work you distribute* is a "work based on" the GPLv3 program, and
  the GPLv3 requires that the combined work, as distributed, be licensed
  under GPLv3 (or a GPLv3-compatible license) as a whole, with source
  available to whoever receives the binary. This is not a Dino-8-specific
  interpretation; it is what the FSF's GPL FAQ says static and dynamic
  linking against a GPL library both mean for the linking program.
- **What that means for a distributed Dino 8 build**: as soon as a Dino 8
  binary that includes `ExportDwg`/`ImportDwg` (i.e. any normal build - the
  DWG code is not behind a build flag) is distributed to anyone outside this
  repository, that distribution needs to satisfy GPLv3: the recipient needs
  the corresponding source (this repository already is source, so that part
  is satisfied by being open) and needs to be free to redistribute and
  modify Dino 8 itself under GPLv3-compatible terms. In practice, for a
  project that is already open-source, this mostly means: **do not describe
  Dino 8 as available under a plain permissive license (MIT, BSD, etc.)
  without also disclosing that a normal build links GPLv3 code and the
  distributed binary is therefore under GPLv3's terms.** The repository's
  root `LICENSE` file (MIT) covers this repository's own original code; it
  does not and cannot relicense LibreDWG, and it does not by itself describe
  what license terms apply to the linked binary as a whole - that is what
  this section is for.
- **If that is not acceptable for a given distribution** (for example, a
  downstream that specifically wants to ship Dino 8 under a permissive
  license with no GPL obligations at all), the only clean options are: (a)
  build with DWG support left out entirely (nothing else in this codebase
  requires LibreDWG - removing `ExportDwg`/`ImportDwg`'s call sites and the
  `libredwg`/`redwg` FetchContent block from `CMakeLists.txt` removes the
  GPLv3 dependency completely and the rest of the app is unaffected), or (b)
  replace LibreDWG with a non-copyleft DWG backend (none is known to exist
  as of this writing - see `docs/INTEROP_LIMITATIONS.md`), or (c) accept
  GPLv3 for the distributed binary, which is what this repository currently
  does by including LibreDWG in the default build.
- **LibreDWG's own source is not vendored into this repository.** It is
  fetched at configure time from
  https://github.com/LibreDWG/libredwg.git (pinned to tag `0.14.8594`) the
  same way OpenNURBS, Manifold, GLFW, Dear ImGui, Lua and pybind11 are - see
  `dino8-app/CMakeLists.txt`. Its own `COPYING` file (GPLv3 text) travels
  with the fetched source tree.

None of the above is a workaround or a loophole - it is what GPLv3 actually
requires, stated plainly so nobody downstream is surprised by it later.
