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
# Applied via FetchContent's PATCH_COMMAND (see CMakeLists.txt) so it lands
# on every fresh clone, matching this project's discipline of never hand-
# editing a vendored dependency's checked-out source in place.

set(_f "${SOURCE_DIR}/src/decode.c")
file(READ "${_f}" _contents)

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
string(FIND "${_contents}" "${_old2}" _pos2)
if(_pos1 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_decode_unknown_bits pattern not found in ${_f} - LibreDWG source may have changed, patch needs updating")
endif()
if(_pos2 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_decode_unknown_rest pattern not found in ${_f} - LibreDWG source may have changed, patch needs updating")
endif()

string(REPLACE "${_old1}" "${_new1}" _contents "${_contents}")
string(REPLACE "${_old2}" "${_new2}" _contents "${_contents}")

# Diagnostic breadcrumbs (see FileExchange.cpp's ImportDwg for the full
# investigation): three independent instrumentation methods (SEH+
# _resetstkoflw, whole-build AddressSanitizer, a process-wide
# SetUnhandledExceptionFilter) all came back with zero signal on the
# Windows-only DWG reopen crash, and a dedicated static trace of the actual
# LINE/CIRCLE/LWPOLYLINE/LAYER decode path found no second reachable
# `long`-width bug. The remaining, most direct diagnostic: print a
# breadcrumb, flushed immediately, right before dwg_decode() starts and
# right before every single object's type-dispatch switch - since the
# crash is 100% deterministic, whichever breadcrumb printed last in the CI
# log is decoding at the moment of death, which pinpoints the real crash
# site precisely (unlike every exception-based method tried so far, this
# does not depend on the crash mechanism being catchable at all).
set(_old3 "dwg_decode (Bit_Chain *restrict dat, Dwg_Data *restrict dwg)
{
  char magic[11];")
set(_new3 "dwg_decode (Bit_Chain *restrict dat, Dwg_Data *restrict dwg)
{
  char magic[11];
  fprintf (stderr, \"DINO8_DWG_TRACE: dwg_decode() entered\\n\");
  fflush (stderr);")

set(_old4 "  restartpos = bit_position (dat); // relative

  /* Check the type of the object
   */
  switch (obj->type)")
set(_new4 "  restartpos = bit_position (dat); // relative
  fprintf (stderr, \"DINO8_DWG_TRACE: object #%lu type=%d size=%u addr=%\" PRIuSIZE \" pos=%\" PRIuSIZE \"\\n\",
           (unsigned long)num, obj->type, obj->size, obj->address, restartpos);
  fflush (stderr);

  /* Check the type of the object
   */
  switch (obj->type)")

string(FIND "${_contents}" "${_old3}" _pos3)
string(FIND "${_contents}" "${_old4}" _pos4)
if(_pos3 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: dwg_decode() entry pattern not found in ${_f} - LibreDWG source may have changed, patch needs updating")
endif()
if(_pos4 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: object-dispatch pattern not found in ${_f} - LibreDWG source may have changed, patch needs updating")
endif()
string(REPLACE "${_old3}" "${_new3}" _contents "${_contents}")
string(REPLACE "${_old4}" "${_new4}" _contents "${_contents}")

file(WRITE "${_f}" "${_contents}")
message(STATUS "patch_libredwg.cmake: fixed platform-dependent ULONG_MAX underflow check and added diagnostic breadcrumbs in ${_f}")

# Round 2 of breadcrumbs: the round-1 breadcrumbs (above) proved the crash
# happens BEFORE any object reaches type-dispatch - "dwg_decode() entered"
# printed, but not one single "object #N ..." line ever did, on either of
# two independent crash reproductions in the same CI run. That means the
# real crash site is somewhere in read_r2007_meta_data
# (src/decode_r2007.c) - the R2007+ file-header/pages-map/sections-map/
# per-section parsing sequence that runs before the object-map walk ever
# starts. This adds one breadcrumb before each major step in that function
# so the next CI run pinpoints exactly which section parser is crashing.
set(_f2 "${SOURCE_DIR}/src/decode_r2007.c")
file(READ "${_f2}" _contents2)

set(_old5 "  read_r2007_init (dwg);
#ifdef USE_TRACING
  probe = getenv (\"LIBREDWG_TRACE\");
  if (probe)
    loglevel = atoi (probe);
#endif
  // @ 0x62
  error = read_file_header (dat, &dwg->fhdr.r2007_file_header);")
set(_new5 "  read_r2007_init (dwg);
#ifdef USE_TRACING
  probe = getenv (\"LIBREDWG_TRACE\");
  if (probe)
    loglevel = atoi (probe);
#endif
  fprintf (stderr, \"DINO8_DWG_TRACE: before read_file_header\\n\"); fflush (stderr);
  // @ 0x62
  error = read_file_header (dat, &dwg->fhdr.r2007_file_header);
  fprintf (stderr, \"DINO8_DWG_TRACE: after read_file_header error=%d\\n\", error); fflush (stderr);")

set(_old6 "  pages_map = read_pages_map (dat, file_header->pages_map_size_comp,
                              file_header->pages_map_size_uncomp,
                              file_header->pages_map_correction);
  if (!pages_map)
    return DWG_ERR_PAGENOTFOUND; // Error already logged")
set(_new6 "  fprintf (stderr, \"DINO8_DWG_TRACE: before read_pages_map\\n\"); fflush (stderr);
  pages_map = read_pages_map (dat, file_header->pages_map_size_comp,
                              file_header->pages_map_size_uncomp,
                              file_header->pages_map_correction);
  fprintf (stderr, \"DINO8_DWG_TRACE: after read_pages_map pages_map=%p\\n\", (void*)pages_map); fflush (stderr);
  if (!pages_map)
    return DWG_ERR_PAGENOTFOUND; // Error already logged")

set(_old7 "  sections_map = read_sections_map (dat, file_header->sections_map_size_comp,
                                    file_header->sections_map_size_uncomp,
                                    file_header->sections_map_correction);
  if (!sections_map)
    goto error;

  error
      = read_2007_section_header (dat, hdl_dat, dwg, sections_map, pages_map);
  if (dwg->header.summaryinfo_address)
    error |= read_2007_section_summary (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_classes (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_handles (dat, hdl_dat, dwg, sections_map,
                                      pages_map);
  error |= read_2007_section_auxheader (dat, dwg, sections_map, pages_map);
  if (dwg->header.thumbnail_address)
    error |= read_2007_section_preview (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_appinfo (dat, dwg, sections_map, pages_map);
  error
      |= read_2007_section_appinfohistory (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_filedeplist (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_security (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_revhistory (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_objfreespace (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_template (dat, dwg, sections_map, pages_map);
  if (dwg->header.vbaproj_address)
    error |= read_2007_section_vbaproject (dat, dwg, sections_map, pages_map);
  // error |= read_2007_section_signature (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_acds (dat, dwg, sections_map, pages_map);
  // read_2007_blocks (dat, hdl_dat, dwg, sections_map, pages_map);")
set(_new7 "  fprintf (stderr, \"DINO8_DWG_TRACE: before read_sections_map\\n\"); fflush (stderr);
  sections_map = read_sections_map (dat, file_header->sections_map_size_comp,
                                    file_header->sections_map_size_uncomp,
                                    file_header->sections_map_correction);
  fprintf (stderr, \"DINO8_DWG_TRACE: after read_sections_map sections_map=%p\\n\", (void*)sections_map); fflush (stderr);
  if (!sections_map)
    goto error;

  fprintf (stderr, \"DINO8_DWG_TRACE: before read_2007_section_header\\n\"); fflush (stderr);
  error
      = read_2007_section_header (dat, hdl_dat, dwg, sections_map, pages_map);
  fprintf (stderr, \"DINO8_DWG_TRACE: after read_2007_section_header error=%d\\n\", error); fflush (stderr);
  if (dwg->header.summaryinfo_address)
    error |= read_2007_section_summary (dat, dwg, sections_map, pages_map);
  fprintf (stderr, \"DINO8_DWG_TRACE: before read_2007_section_classes\\n\"); fflush (stderr);
  error |= read_2007_section_classes (dat, dwg, sections_map, pages_map);
  fprintf (stderr, \"DINO8_DWG_TRACE: after read_2007_section_classes error=%d\\n\", error); fflush (stderr);
  fprintf (stderr, \"DINO8_DWG_TRACE: before read_2007_section_handles\\n\"); fflush (stderr);
  error |= read_2007_section_handles (dat, hdl_dat, dwg, sections_map,
                                      pages_map);
  fprintf (stderr, \"DINO8_DWG_TRACE: after read_2007_section_handles error=%d\\n\", error); fflush (stderr);
  error |= read_2007_section_auxheader (dat, dwg, sections_map, pages_map);
  if (dwg->header.thumbnail_address)
    error |= read_2007_section_preview (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_appinfo (dat, dwg, sections_map, pages_map);
  error
      |= read_2007_section_appinfohistory (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_filedeplist (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_security (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_revhistory (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_objfreespace (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_template (dat, dwg, sections_map, pages_map);
  if (dwg->header.vbaproj_address)
    error |= read_2007_section_vbaproject (dat, dwg, sections_map, pages_map);
  // error |= read_2007_section_signature (dat, dwg, sections_map, pages_map);
  error |= read_2007_section_acds (dat, dwg, sections_map, pages_map);
  fprintf (stderr, \"DINO8_DWG_TRACE: read_r2007_meta_data reached end, error=%d\\n\", error); fflush (stderr);
  // read_2007_blocks (dat, hdl_dat, dwg, sections_map, pages_map);")

string(FIND "${_contents2}" "${_old5}" _pos5)
string(FIND "${_contents2}" "${_old6}" _pos6)
string(FIND "${_contents2}" "${_old7}" _pos7)
if(_pos5 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: read_file_header pattern not found in ${_f2} - LibreDWG source may have changed, patch needs updating")
endif()
if(_pos6 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: read_pages_map pattern not found in ${_f2} - LibreDWG source may have changed, patch needs updating")
endif()
if(_pos7 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: read_r2007_meta_data section sequence pattern not found in ${_f2} - LibreDWG source may have changed, patch needs updating")
endif()
string(REPLACE "${_old5}" "${_new5}" _contents2 "${_contents2}")
string(REPLACE "${_old6}" "${_new6}" _contents2 "${_contents2}")
string(REPLACE "${_old7}" "${_new7}" _contents2 "${_contents2}")
file(WRITE "${_f2}" "${_contents2}")
message(STATUS "patch_libredwg.cmake: added round-2 diagnostic breadcrumbs to read_r2007_meta_data in ${_f2}")

# Round 3: round 2's own breadcrumbs (in decode_r2007.c) never fired even
# locally on a real, successful round trip - proving this app's own default
# DWG export (no Version= override) writes AC1015/AutoCAD 2000 format,
# which decodes via decode_R13_R2000 (this same file, ~line 292), NOT
# decode_R2007. That is almost certainly true on Windows CI too, since it's
# the exact same default export path. These breadcrumbs bracket the three
# real candidate zones inside decode_R13_R2000 that run before the first
# object-map dwg_decode_add_object call: right after the header.spec
# parse, right before the classes-table parsing loop (which reallocs a
# dwg_class array in a loop - a real dynamic-allocation candidate), and
# right before the handles_section: object-map walk itself begins.
set(_old8 "    // clang-format off
    #include \"header.spec\"
    // clang-format on
  }
  if ((error = dwg_sections_init (dwg)))
    return error;")
set(_new8 "    // clang-format off
    #include \"header.spec\"
    // clang-format on
  }
  fprintf (stderr, \"DINO8_DWG_TRACE: R13_R2000 header.spec parsed ok\\n\"); fflush (stderr);
  if ((error = dwg_sections_init (dwg)))
    return error;
  fprintf (stderr, \"DINO8_DWG_TRACE: R13_R2000 dwg_sections_init ok\\n\"); fflush (stderr);")

set(_old9 "  LOG_INSANE (\"endpos: %\" PRIuSIZE, endpos);
  LOG_POS_ (INSANE);

  /* Read the classes
   */
  dwg->layout_type = 0;
  dwg->num_classes = 0;")
set(_new9 "  LOG_INSANE (\"endpos: %\" PRIuSIZE, endpos);
  LOG_POS_ (INSANE);
  fprintf (stderr, \"DINO8_DWG_TRACE: R13_R2000 before classes loop, endpos=%\" PRIuSIZE \" dat->byte=%\" PRIuSIZE \"\\n\", endpos, dat->byte); fflush (stderr);

  /* Read the classes
   */
  dwg->layout_type = 0;
  dwg->num_classes = 0;")

set(_old10 "  /*-------------------------------------------------------------------------
   * Object-map, section 2
   */
handles_section:
  dat->byte = dwg->header.section[SECTION_HANDLES_R13].address;")
set(_new10 "  fprintf (stderr, \"DINO8_DWG_TRACE: R13_R2000 classes loop done, num_classes=%u\\n\", dwg->num_classes); fflush (stderr);
  /*-------------------------------------------------------------------------
   * Object-map, section 2
   */
handles_section:
  fprintf (stderr, \"DINO8_DWG_TRACE: R13_R2000 entering handles_section, error=%d\\n\", error); fflush (stderr);
  dat->byte = dwg->header.section[SECTION_HANDLES_R13].address;")

set(_f3 "${SOURCE_DIR}/src/decode.c")
file(READ "${_f3}" _contents3)
string(FIND "${_contents3}" "${_old8}" _pos8)
string(FIND "${_contents3}" "${_old9}" _pos9)
string(FIND "${_contents3}" "${_old10}" _pos10)
if(_pos8 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: R13_R2000 header.spec pattern not found in ${_f3} - LibreDWG source may have changed, patch needs updating")
endif()
if(_pos9 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: R13_R2000 classes-loop pattern not found in ${_f3} - LibreDWG source may have changed, patch needs updating")
endif()
if(_pos10 EQUAL -1)
  message(FATAL_ERROR "patch_libredwg.cmake: R13_R2000 handles_section pattern not found in ${_f3} - LibreDWG source may have changed, patch needs updating")
endif()
string(REPLACE "${_old8}" "${_new8}" _contents3 "${_contents3}")
string(REPLACE "${_old9}" "${_new9}" _contents3 "${_contents3}")
string(REPLACE "${_old10}" "${_new10}" _contents3 "${_contents3}")
file(WRITE "${_f3}" "${_contents3}")
message(STATUS "patch_libredwg.cmake: added round-3 diagnostic breadcrumbs to decode_R13_R2000 in ${_f3}")
