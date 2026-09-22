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
