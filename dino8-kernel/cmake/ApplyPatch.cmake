# Applies one patch file to a fetched dependency source tree. Run as the dependency's PATCH_COMMAND:
#   cmake -DPATCH_FILE=<patch> -DPATCH_NAME=<label> -P ApplyPatch.cmake      (working directory = the source tree)
#
# Idempotent and loud:
#   * already applied (the patch reverses cleanly)  -> nothing to do, success
#   * applies cleanly                               -> applied, success
#   * neither                                       -> FATAL_ERROR (the dependency was bumped or the tree is half-patched)
# `git apply` is used (the fetched tree is a git clone, and git is already required to fetch it), with
# --ignore-whitespace so a Windows checkout that converted the sources to CRLF still matches the LF context lines.
if(NOT PATCH_FILE OR NOT EXISTS "${PATCH_FILE}")
  message(FATAL_ERROR "ApplyPatch: PATCH_FILE '${PATCH_FILE}' does not exist")
endif()
if(NOT PATCH_NAME)
  set(PATCH_NAME "${PATCH_FILE}")
endif()
find_package(Git QUIET REQUIRED)
set(_apply ${GIT_EXECUTABLE} apply --ignore-whitespace -p1)

execute_process(COMMAND ${_apply} --reverse --check "${PATCH_FILE}"
                RESULT_VARIABLE _already OUTPUT_QUIET ERROR_QUIET)
if(_already EQUAL 0)
  message(STATUS "${PATCH_NAME}: already applied")
  return()
endif()

execute_process(COMMAND ${_apply} --check "${PATCH_FILE}"
                RESULT_VARIABLE _can ERROR_VARIABLE _err OUTPUT_VARIABLE _out)
if(NOT _can EQUAL 0)
  message(FATAL_ERROR
    "${PATCH_NAME}: the patch no longer applies to the fetched source in ${CMAKE_SOURCE_DIR}\n"
    "  patch: ${PATCH_FILE}\n${_out}${_err}\n"
    "If the pinned version was bumped, rebase the patch (or drop the hunk that upstream fixed). If a previous "
    "configure left the tree half-patched, delete the fetched source directory (…/_deps/opennurbs-src) and reconfigure.")
endif()
execute_process(COMMAND ${_apply} "${PATCH_FILE}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${PATCH_NAME}: git apply failed\n${_err}")
endif()
message(STATUS "${PATCH_NAME}: applied")
