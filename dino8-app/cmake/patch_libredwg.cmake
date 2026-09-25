# Fixes a real, platform-dependent memory-corruption bug in LibreDWG
# 0.14.8594's src/decode.c (dwg_decode_unknown_bits/dwg_decode_unknown_rest):
# both computed an underflow-guarded bit count as
#   long num_bits = (X - pos) & ULONG_MAX;
#   if (num_bits < 0) return DWG_ERR_VALUEOUTOFBOUNDS;
# `long` is 64-bit on Linux/macOS (LP64) but only 32-bit on Windows (LLP64),
# and ULONG_MAX matches `long`'s own width on each platform. When `pos`
# exceeds the declared size (a real, not-uncommon-on-real-world-DWGs
# condition, since these are "unknown/fallback" object decoders), the
# subtraction underflows as unsigned arithmetic, producing a huge value.
# On Linux, masking with a 64-bit ULONG_MAX is a no-op, so that huge value
# still reads as negative once reinterpreted as a signed 64-bit `long` - the
# underflow check correctly fires. On Windows, masking with a 32-bit
# ULONG_MAX truncates that huge value down to just its low 32 bits, which
# can land as a small, garbage but POSITIVE 32-bit number - the underflow
# check silently passes, and the bogus bit count is then handed straight to
# bit_read_bits(), corrupting the heap. This is a genuine, Windows-only,
# content-dependent bug in the vendored library, not anything in this
# project's own code - see src/io/FileExchange.cpp's ImportDwg for the
# multi-round investigation (SEH, AddressSanitizer, and a process-wide
# unhandled-exception filter all independently pointed at an uncatchable
# Windows __fastfail from exactly this class of corruption) that led here.
#
# The fix replaces the platform-dependent mask-then-sign-check with a
# straightforward unsigned pre-check (pos > total_bits) done BEFORE the
# subtraction, which is correct and identical on every platform regardless
# of `long`'s width.
#
# Idempotent: FetchContent re-runs PATCH_COMMAND on an already-patched
# checkout whenever this script (or anything else in the FetchContent
# declaration) changes, so every replacement below first checks whether its
# own replacement text is already present and only fails when NEITHER the
# original nor the patched pattern is found (a real upstream change).
#
# Applied via FetchContent's PATCH_COMMAND (see CMakeLists.txt) so it lands
# on every fresh clone, matching this project's discipline of never hand-
# editing a vendored dependency's checked-out source in place.

set(_f "${SOURCE_DIR}/src/decode.c")
file(READ "${_f}" _contents)

# --- Idempotency guard -----------------------------------------------------
# FetchContent's PATCH_COMMAND re-runs this whole script on every reconfigure
# that touches the ExternalProject step's dependencies - and a plain edit to
# this project's own CMakeLists.txt is enough to trigger one, since the
# generated Makefile/Ninja build reruns cmake automatically whenever it sees
# CMakeLists.txt is newer than the build system files. The patches below are
# literal string replacements that leave no "already applied" trace except
# the replaced text itself, so a second run used to find none of the "_old*"
# patterns anywhere and abort the ENTIRE configure with a false "LibreDWG
# source may have changed" FATAL_ERROR - turning "add a CMake target" into a
# broken build. Detect an already-fully-patched tree by the first
# replacement's own distinctive new text and skip straight to success. A
# genuinely different LibreDWG checkout (neither pre- nor post-patch text
# present) still falls through to the real FATAL_ERROR checks below.
string(FIND "${_contents}" "size_t total_bits = (size_t)8 * (size_t)obj->size;" _already_patched)
if(NOT _already_patched EQUAL -1)
  message(STATUS "patch_libredwg.cmake: LibreDWG source already patched (from a previous configure) - skipping")
  return()
endif()

set(_old1 "  size_t pos = bit_position (dat);
  long num_bits = ((8 * obj->size) - pos) & ULONG_MAX;
  if (num_bits < 0)
    return DWG_ERR_VALUEOUTOFBOUNDS;")
set(_new1 "  size_t pos = bit_position (dat);
  size_t total_bits = (size_t)8 * (size_t)obj->size;
  if (pos > total_bits)
    return DWG_ERR_VALUEOUTOFBOUNDS;
  long num_bits = (long)(total_bits - pos);")

set(_old2 "  size_t pos = bit_position (dat);
  long num_bits;
  if (pos < obj->bitsize) // data or text
    num_bits = (obj->bitsize - pos) & ULONG_MAX;
  else // or handles
    num_bits = ((8 * obj->size) - pos) & ULONG_MAX;
  if (num_bits < 0)
    return DWG_ERR_VALUEOUTOFBOUNDS;")
set(_new2 "  size_t pos = bit_position (dat);
  long num_bits;
  if (pos < obj->bitsize) // data or text
    num_bits = (long)((size_t)obj->bitsize - pos);
  else // or handles
    {
      size_t total_bits = (size_t)8 * (size_t)obj->size;
      if (pos > total_bits)
        return DWG_ERR_VALUEOUTOFBOUNDS;
      num_bits = (long)(total_bits - pos);
    }")

string(FIND "${_contents}" "${_old1}" _pos1)
string(FIND "${_contents}" "${_new1}" _applied1)
string(FIND "${_contents}" "${_old2}" _pos2)
string(FIND "${_contents}" "${_new2}" _applied2)
if(_pos1 EQUAL -1 AND _applied1 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_decode_unknown_bits pattern not found in ${_f} - LibreDWG source may have changed, patch needs updating")
endif()
if(_pos2 EQUAL -1 AND _applied2 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_decode_unknown_rest pattern not found in ${_f} - LibreDWG source may have changed, patch needs updating")
endif()

string(REPLACE "${_old1}" "${_new1}" _contents "${_contents}")
string(REPLACE "${_old2}" "${_new2}" _contents "${_contents}")

file(WRITE "${_f}" "${_contents}")
message(STATUS "patch_libredwg.cmake: fixed platform-dependent ULONG_MAX underflow check in ${_f}")

# Round 5: the actual, confirmed root cause of the Windows-only DWG reopen
# crash. (Rounds 1-4 were fprintf(stderr) breadcrumbs threaded through
# decode.c/decode_r2007.c purely to bisect the crash site on CI; they did
# their job and have been removed again - a shipped decoder must not print
# a trace line per object to stderr on every DWG open.) Found via local reproduction -
# a MinGW cross-build of this exact patched source, run under Wine against
# the exact DWG bytes this project's own smoke test writes and reopens (the
# first time this bug was reproduced outside Windows CI, and much faster to
# iterate on: seconds per attempt instead of a ~15-20 minute CI round trip).
#
# The standalone decode completed under Wine but printed two
# "err:msvcrt:_invalid_parameter" diagnostics from Wine's own msvcrt shim,
# both landing inside dwg_decode_header_variables (which the round-4
# breadcrumbs had already bracketed as the crash site). Wine's CRT logs that and carries on; real Windows' MSVC CRT
# default invalid-parameter handler aborts the process instead - with no
# exception for SEH/AddressSanitizer/an unhandled-exception filter to ever
# catch, since it isn't a memory-safety fault at all (matching every
# negative result from those three earlier, more invasive diagnostic
# attempts: they were looking for the wrong class of bug).
#
# src/header_variables.spec's DECODER block for TDCREATE/TDUPDATE runs when
# a DWG's own TDCREATE is unset - exactly what happens for a freshly-
# exported Dino8 document (see FileExchange.cpp's ExportDwg, which never
# sets TDCREATE), deriving it from TDUCREATE instead and calling strftime
# via cvt_TIMEBLL (src/common.c) to build a trace string. TDUCREATE.days is
# itself 0 for such a document (never round-tripped through a real DWG
# creation timestamp before), which sends cvt_TIMEBLL down its "ja < 1000"
# fallback branch - meant for TDINDWG-style relative/duration values, not
# absolute dates - which does `memset (tm, 0, sizeof (struct tm))` and
# nothing else. That leaves tm_mday == 0, which is out of struct tm's valid
# [1,31] range for a calendar date. glibc's and Darwin's strftime silently
# tolerate an out-of-range tm_mday; MSVC's CRT parameter validation treats
# it as an invalid argument and aborts - deterministically, since the same
# degenerate all-zero timestamp is produced every time a fresh document is
# exported and reopened.
#
# The fix is in cvt_TIMEBLL itself, not the call site: after zeroing the
# struct in that fallback branch, also set tm_mday = 1 - a valid, correct
# placeholder day (this branch already produces a nonsense/placeholder
# date by design; the bug was only that it produced an out-of-range one).
# This is a real root-cause fix, not a Windows-specific workaround: an
# out-of-range struct tm is invalid on every platform, and this keeps
# cvt_TIMEBLL's result valid everywhere, not just where the CRT happens to
# enforce it.
set(_f5 "${SOURCE_DIR}/src/common.c")
file(READ "${_f5}" _contents5)
set(_old13 "  if (ja < 1000)
    {
      // TDINDWG: relative minutes
      memset (tm, 0, sizeof (struct tm));
    }")
set(_new13 "  if (ja < 1000)
    {
      // TDINDWG: relative minutes. memset alone leaves tm_mday at 0, which
      // is out of struct tm's valid [1,31] range - harmless on glibc/
      // Darwin's lenient strftime, but MSVC's CRT parameter validation
      // treats it as an invalid argument and aborts the process (see this
      // patch script's own round-5 comment for the full investigation,
      // found via local MinGW+Wine reproduction of the real Windows crash).
      memset (tm, 0, sizeof (struct tm));
      tm->tm_mday = 1;
    }")
string(FIND "${_contents5}" "${_old13}" _pos13)
string(FIND "${_contents5}" "${_new13}" _applied13)
if(_pos13 EQUAL -1 AND _applied13 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: cvt_TIMEBLL's ja<1000 fallback pattern not found in ${_f5} - LibreDWG source may have changed, patch needs updating")
endif()
string(REPLACE "${_old13}" "${_new13}" _contents5 "${_contents5}")
file(WRITE "${_f5}" "${_contents5}")
message(STATUS "patch_libredwg.cmake: fixed cvt_TIMEBLL's out-of-range tm_mday in its degenerate-timestamp fallback (real Windows DWG-reopen crash root cause, confirmed via local MinGW+Wine reproduction)")

# Round 6: a second, separate, real bug - only ever reachable once round 5's
# fix let the Windows build get this far. Windows CI's global ASan build
# (see CMakeLists.txt's MSVC /fsanitize=address block) caught a genuine
# heap-buffer-overflow READ in dwg_add_HATCH's DWG_TYPE_POLYLINE_2D case:
# it loops up to pline->num_owned (the file's declared vertex count) while
# reading dwg_object_polyline_2d_get_points()'s returned array, but that
# getter only allocates and fills as many points as it could actually
# resolve real VERTEX_2D objects for via dwg_ref_object - which can be
# fewer than num_owned if a vertex handle fails to resolve. Looping past
# that smaller allocation reads past its end.
#
# The fix bounds the loop to the actual point count the getter reports
# (via dwg_object_polyline_2d_get_numpoints, the same helper the getter
# itself calls internally), never more than what was actually allocated.
# Verified locally with Valgrind (dwg_fixture_gen ... hatch, 0 errors)
# after the fix; MinGW/GCC does not accept MSVC's /fsanitize=address
# syntax so this could not be reproduced bit-for-bit under Wine the way
# round 5 was, but the fix is correct by construction: the loop bound can
# never exceed the array's actual allocated size.
set(_f6 "${SOURCE_DIR}/src/dwg_api.c")
file(READ "${_f6}" _contents6)
set(_old14 "          case DWG_TYPE_POLYLINE_2D:
            {
              Dwg_Entity_POLYLINE_2D *pline
                  = pathobjs[i]->tio.entity->tio.POLYLINE_2D;
              dwg_point_2d *pts;
              _obj->paths[i].flag = 2 + (is_associative ? 0x200 : 0);
              _obj->paths[i].closed = pline->flag & 1 ? 1 : 0;
              _obj->paths[i].num_segs_or_paths = pline->num_owned;
              _obj->paths[i].polyline_paths
                  = (Dwg_HATCH_PolylinePath *)calloc (
                      pline->num_owned, sizeof (Dwg_HATCH_PolylinePath));
              pts = dwg_object_polyline_2d_get_points (pathobjs[i], &error);
              if (error)
                return NULL;
              for (unsigned j = 0; j < pline->num_owned; j++)
                {
                  _obj->paths[i].polyline_paths[j].parent = &_obj->paths[i];
                  _obj->paths[i].polyline_paths[j].point.x = pts[j].x;
                  _obj->paths[i].polyline_paths[j].point.y = pts[j].y;
                }
              // TODO bulges, curve_type
              free (pts);
            }
            break;")
set(_new14 "          case DWG_TYPE_POLYLINE_2D:
            {
              Dwg_Entity_POLYLINE_2D *pline
                  = pathobjs[i]->tio.entity->tio.POLYLINE_2D;
              dwg_point_2d *pts;
              int numpts_error = 0;
              BITCODE_BL numpts;
              _obj->paths[i].flag = 2 + (is_associative ? 0x200 : 0);
              _obj->paths[i].closed = pline->flag & 1 ? 1 : 0;
              _obj->paths[i].num_segs_or_paths = pline->num_owned;
              _obj->paths[i].polyline_paths
                  = (Dwg_HATCH_PolylinePath *)calloc (
                      pline->num_owned, sizeof (Dwg_HATCH_PolylinePath));
              pts = dwg_object_polyline_2d_get_points (pathobjs[i], &error);
              if (error)
                return NULL;
              /* dwg_object_polyline_2d_get_points only allocates as many
                 points as it could actually resolve real VERTEX_2D
                 objects for, which can be fewer than pline->num_owned if
                 a vertex handle fails to resolve - looping to num_owned
                 here read past the end of that smaller allocation. */
              numpts = dwg_object_polyline_2d_get_numpoints (pathobjs[i], &numpts_error);
              if (numpts > pline->num_owned)
                numpts = pline->num_owned;
              for (unsigned j = 0; j < numpts; j++)
                {
                  _obj->paths[i].polyline_paths[j].parent = &_obj->paths[i];
                  _obj->paths[i].polyline_paths[j].point.x = pts[j].x;
                  _obj->paths[i].polyline_paths[j].point.y = pts[j].y;
                }
              // TODO bulges, curve_type
              free (pts);
            }
            break;")
string(FIND "${_contents6}" "${_old14}" _pos14)
string(FIND "${_contents6}" "${_new14}" _applied14)
if(_pos14 EQUAL -1 AND _applied14 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_add_HATCH's DWG_TYPE_POLYLINE_2D case pattern not found in ${_f6} - LibreDWG source may have changed, patch needs updating")
endif()
string(REPLACE "${_old14}" "${_new14}" _contents6 "${_contents6}")
file(WRITE "${_f6}" "${_contents6}")
message(STATUS "patch_libredwg.cmake: fixed dwg_add_HATCH's out-of-bounds read past dwg_object_polyline_2d_get_points' actual allocation (Windows ASan-caught heap-buffer-overflow, found once round 5's fix let the build reach this code)")

# Round 7: the actual root cause behind round 6's mismatch, not just its
# symptom. Once round 6's clamp stopped the crash, the HATCH pattern-fill
# smoke checks started failing on the real point count/geometry (e.g. "did
# not produce the expected 57 pattern-line curves") - dwg_add_HATCH's
# POLYLINE_2D case was now safe, but wrong: it was building a hatch
# boundary with one fewer point than the real 4-point square boundary.
#
# The cause is in dwg_object_polyline_2d_get_numpoints and
# dwg_object_polyline_2d_get_points' shared R13-R2000 branch (the one this
# project's default AC1015/AutoCAD 2000 export/import path exercises),
# which walks _obj->first_vertex through _obj->last_vertex via
# dwg_next_object. Both used the identical pattern
#   do { ...count/fill current vobj...; } while ((vobj = dwg_next_object(vobj)) && vobj != vlast);
# which checks "vobj != vlast" only AFTER advancing - so the loop stops the
# moment vobj becomes vlast, without ever counting/filling vlast itself.
# That is a genuine off-by-one undercount of exactly 1 vertex, matching
# what round 6 observed (3 points read for a real 4-point boundary).
#
# Fixed by restructuring both loops to walk first_vertex..last_vertex
# INCLUSIVE: count/fill the current vertex, then break if it was vlast,
# otherwise advance. Verified locally (dwg_fixture_gen ... hatch followed
# by the app's own HATCH round-trip smoke checks) that this now produces
# the real 4-point boundary and the correct 57 ANSI31 pattern-line count.
set(_f7 "${SOURCE_DIR}/src/dwg_api.c")
file(READ "${_f7}" _contents7)

set(_old15 "      if (dwg->header.version >= R_2004)
        return obj->tio.entity->tio.POLYLINE_2D->num_owned;
      // iterate over first_vertex - last_vertex
      else if (dwg->header.version >= R_13b1)
        {
          Dwg_Object *vobj = dwg_ref_object (dwg, _obj->first_vertex);
          Dwg_Object *vlast = dwg_ref_object (dwg, _obj->last_vertex);
          if (!vobj)
            *error = 1;
          else
            {
              do
                {
                  if (vobj->fixedtype == DWG_TYPE_VERTEX_2D)
                    num_points++;
                  else
                    *error = 1; // return not all vertices, but some
                }
              while ((vobj = dwg_next_object (vobj)) && vobj != vlast);
            }
        }")
set(_new15 "      if (dwg->header.version >= R_2004)
        return obj->tio.entity->tio.POLYLINE_2D->num_owned;
      // iterate over first_vertex - last_vertex (inclusive: the original
      // do-while checked vobj != vlast only AFTER advancing, so it never
      // counted last_vertex itself - an off-by-one undercount)
      else if (dwg->header.version >= R_13b1)
        {
          Dwg_Object *vobj = dwg_ref_object (dwg, _obj->first_vertex);
          Dwg_Object *vlast = dwg_ref_object (dwg, _obj->last_vertex);
          if (!vobj)
            *error = 1;
          else
            {
              for (;;)
                {
                  if (vobj->fixedtype == DWG_TYPE_VERTEX_2D)
                    num_points++;
                  else
                    *error = 1; // return not all vertices, but some
                  if (vobj == vlast)
                    break;
                  vobj = dwg_next_object (vobj);
                  if (!vobj)
                    break;
                }
            }
        }")
string(FIND "${_contents7}" "${_old15}" _pos15)
string(FIND "${_contents7}" "${_new15}" _applied15)
if(_pos15 EQUAL -1 AND _applied15 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_object_polyline_2d_get_numpoints's first_vertex/last_vertex loop pattern not found in ${_f7} - LibreDWG source may have changed, patch needs updating")
endif()
string(REPLACE "${_old15}" "${_new15}" _contents7 "${_contents7}")

set(_old16 "          Dwg_Object *vobj = dwg_ref_object (dwg, _obj->first_vertex);
          Dwg_Object *vlast = dwg_ref_object (dwg, _obj->last_vertex);
          if (!vobj)
            *error = 1;
          else
            {
              i = 0;
              do
                {
                  if (vobj->fixedtype == DWG_TYPE_VERTEX_2D
                      && (vertex = dwg_object_to_VERTEX_2D (vobj)))
                    {
                      ptx[i].x = vertex->point.x;
                      ptx[i].y = vertex->point.y;
                      i++;
                      if (i > num_points)
                        {
                          *error = 1;
                          break;
                        }
                    }
                  else
                    {
                      *error = 1; // return not all vertices, but some
                    }
                }
              while ((vobj = dwg_next_object (vobj)) && vobj != vlast);
            }
        }")
set(_new16 "          Dwg_Object *vobj = dwg_ref_object (dwg, _obj->first_vertex);
          Dwg_Object *vlast = dwg_ref_object (dwg, _obj->last_vertex);
          if (!vobj)
            *error = 1;
          else
            {
              i = 0;
              // inclusive first_vertex..last_vertex walk - see the matching
              // fix and comment in dwg_object_polyline_2d_get_numpoints
              // above; this loop had the identical off-by-one.
              for (;;)
                {
                  if (vobj->fixedtype == DWG_TYPE_VERTEX_2D
                      && (vertex = dwg_object_to_VERTEX_2D (vobj)))
                    {
                      ptx[i].x = vertex->point.x;
                      ptx[i].y = vertex->point.y;
                      i++;
                      if (i > num_points)
                        {
                          *error = 1;
                          break;
                        }
                    }
                  else
                    {
                      *error = 1; // return not all vertices, but some
                    }
                  if (vobj == vlast)
                    break;
                  vobj = dwg_next_object (vobj);
                  if (!vobj)
                    break;
                }
            }
        }")
string(FIND "${_contents7}" "${_old16}" _pos16)
string(FIND "${_contents7}" "${_new16}" _applied16)
if(_pos16 EQUAL -1 AND _applied16 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_object_polyline_2d_get_points's first_vertex/last_vertex fill-loop pattern not found in ${_f7} - LibreDWG source may have changed, patch needs updating")
endif()
string(REPLACE "${_old16}" "${_new16}" _contents7 "${_contents7}")
file(WRITE "${_f7}" "${_contents7}")
message(STATUS "patch_libredwg.cmake: fixed the first_vertex/last_vertex off-by-one undercount in dwg_object_polyline_2d_get_numpoints/get_points (real correctness bug behind round 6's mismatch)")
