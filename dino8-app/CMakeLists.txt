cmake_minimum_required(VERSION 3.20)
project(Dino8 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Geometry kernel (same repo). The app never builds the kernel's own test
# suite or demo programs - CI runs those from the kernel directory directly.
# ---------------------------------------------------------------------------
set(DINO8_KERNEL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DINO8_KERNEL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../dino8-kernel ${CMAKE_BINARY_DIR}/dino8-kernel)

# ---------------------------------------------------------------------------
# Third-party UI stack: GLFW (window/input) + Dear ImGui docking branch.
# Both pinned to exact tags so builds are reproducible.
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
# Wayland needs wayland-scanner at build time; X11 (with XWayland) covers Linux desktops.
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.9b-docking
)
FetchContent_MakeAvailable(imgui)

# Lua 5.4 - the scripting engine behind RunScript / the Script Editor (see
# src/script/LuaEngine.*). Rhino ships RhinoScript and Python; a small,
# offline installer can't carry CPython, so Dino 8 embeds Lua instead: it's
# ~30 plain-C files (no platform-specific code, builds identically on
# Windows/macOS/Linux) and gets the same "type rs.AddBox(...) and go"
# scripting experience via the rs.* module LuaEngine registers.
FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG        v5.4.7
)
FetchContent_MakeAvailable(lua)
# Upstream ships no CMakeLists (it's built via a plain Makefile); list the
# interpreter's library sources ourselves (lua.c/luac.c are the standalone
# executables' own main()s and don't belong in the embedded library).
file(GLOB DINO8_LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
# lua.c/luac.c are the standalone executables' own main()s; onelua.c is the
# alternate amalgamated single-file build (it #includes every other .c
# itself, including lua.c's main) - compiling it alongside the individual
# files duplicates every symbol.
list(REMOVE_ITEM DINO8_LUA_SOURCES ${lua_SOURCE_DIR}/lua.c ${lua_SOURCE_DIR}/luac.c ${lua_SOURCE_DIR}/onelua.c)
add_library(dino8_lua STATIC ${DINO8_LUA_SOURCES})
target_include_directories(dino8_lua PUBLIC ${lua_SOURCE_DIR})
target_compile_definitions(dino8_lua PRIVATE LUA_USE_C89)  # no platform-specific dlopen/posix path assumptions
if(UNIX AND NOT APPLE)
  target_link_libraries(dino8_lua PUBLIC m)
endif()
if(MSVC)
  target_compile_options(dino8_lua PRIVATE /w)
else()
  target_compile_options(dino8_lua PRIVATE -w)
endif()

# Python 3 scripting (src/script/PythonEngine.*) - optional, unlike Lua.
# Rhino ships CPython, but CPython itself is not a `file(GLOB *.c)`-friendly
# source tree the way Lua is (its own build is autotools/configure on Unix
# and a bespoke MSBuild solution on Windows), so Dino 8 does not vendor it.
# Instead this looks for a Python 3 development install already on the build
# machine (python3-dev on Debian/Ubuntu, the Python.org/Homebrew framework on
# macOS, the official installer's "Download debug binaries"/embeddable
# package + import library on Windows) and links against it directly. When
# it's missing, DINO8_HAVE_PYTHON simply stays unset: PythonEngine.cpp still
# compiles (its embedding code is under #ifdef DINO8_HAVE_PYTHON) and
# RunPythonScript falls back to its old honest "not available" message, so
# the build never hard-fails for lack of a Python dev environment.
find_package(Python3 3.8 COMPONENTS Interpreter Development.Embed)
if(Python3_FOUND)
  message(STATUS "Python3 ${Python3_VERSION} found (${Python3_LIBRARIES}); building embedded Python scripting (dino8 module)")
  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6
  )
  FetchContent_MakeAvailable(pybind11)
else()
  message(WARNING "Python3 development headers/libs not found (need python3-dev or equivalent) - Dino 8 will build without embedded Python scripting; RunPythonScript will report that it is unavailable")
endif()

# GNU LibreDWG (https://www.gnu.org/software/libredwg/) - the real DWG
# read/write backend for Import/Export/Open/Save (see src/io/FileExchange.cpp's
# ImportDwg/ExportDwg). LibreDWG is a genuine, actively-maintained GPLv3
# implementation of Autodesk's proprietary DWG format, built the same way
# FreeCAD's and other free CAD tools' DWG support is built - not a
# reverse-engineered format spec written for this project. See
# docs/INTEROP_LIMITATIONS.md and THIRD_PARTY_LICENSES.md for what linking
# GPLv3 code here means for the rest of this codebase's licensing.
#
# LIBREDWG_LIBONLY skips its own command-line programs/tests/docs (we only
# want the library); DISABLE_JSON skips its optional JSON reader/writer,
# which otherwise needs the jsmn git submodule FetchContent does not fetch
# on its own; DISABLE_WERROR keeps a stray upstream warning from becoming a
# hard error under whatever host compiler builds this; ENABLE_LTO is turned
# off because it is a link-time optimisation of LibreDWG's own build, not
# something this project's build needs to inherit.
FetchContent_Declare(
  libredwg
  GIT_REPOSITORY https://github.com/LibreDWG/libredwg.git
  GIT_TAG        0.14.8594
)
set(LIBREDWG_LIBONLY ON CACHE BOOL "" FORCE)
set(LIBREDWG_DISABLE_JSON ON CACHE BOOL "" FORCE)
set(LIBREDWG_DISABLE_WRITE OFF CACHE BOOL "" FORCE)
set(DISABLE_WERROR ON CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libredwg)
# Only the library target matters here; its own CMakeLists always names the
# static target "redwg" on non-MSVC hosts (see its top-of-file `set(redwg
# redwg)` / `set(redwg libredwg)`).
if(MSVC)
  set(DINO8_LIBREDWG_TARGET libredwg)
else()
  set(DINO8_LIBREDWG_TARGET redwg)
endif()
# LibreDWG's src/dwg_spec_shared.h calls the GNU/BSD extension memmem()
# unconditionally, with no fallback and no HAVE_MEMMEM guard, assuming its
# declaration is always visible from <string.h>. That's true on Linux glibc
# (whose headers expose it by default via _DEFAULT_SOURCE, which recent
# glibc versions define implicitly) but not on macOS's libc: the function
# has shipped in libSystem since 10.7, but its prototype in <string.h> is
# gated behind _DARWIN_C_SOURCE, which is not defined by default in a plain
# CMake build the way autotools' AC_USE_SYSTEM_EXTENSIONS/gnulib step would
# set it. This is exactly what broke the "macOS (build, .dmg)" CI job on
# commit 9e97996 ("call to undeclared function 'memmem'" - only visible
# after the -Wno-cast-align cleanup above shrank the log enough to actually
# see it). Defining both feature-test macros for LibreDWG's own target only
# makes the same libc function's existing declaration visible on both
# platforms, matching what its autotools build already gets for free.
if(NOT MSVC)
  target_compile_definitions(${DINO8_LIBREDWG_TARGET} PRIVATE _GNU_SOURCE _DARWIN_C_SOURCE)
endif()
# LibreDWG's own generated dwg2.spec code intentionally casts BITCODE_TV
# (char*) to BITCODE_TU (unsigned short*) hundreds of times in its TRACE
# logging macros - a deliberate, known-safe alignment-widening cast in
# vendored third-party code we don't control, not a bug in anything this
# project owns. Left enabled, this single warning class dominates a full
# rebuild's compiler output (800+ occurrences), which has already once
# pushed a real, unrelated compile error further back in a CI job's log
# than this project's own log-fetching tooling could see, making that
# failure much harder to diagnose than it needed to be.
if(NOT MSVC)
  target_compile_options(${DINO8_LIBREDWG_TARGET} PRIVATE -Wno-cast-align)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# The application
# ---------------------------------------------------------------------------
set(DINO8_SOURCES
  src/main.cpp
  src/gl/gl_loader.cpp
  src/app/Application.cpp
  src/app/Settings.cpp
  src/i18n/I18n.cpp
  src/input/SpaceMouse.cpp
  src/sketch/Constraints.cpp
  src/commands/cmd_constraints.cpp
  src/arch/ArchComponents.cpp
  src/commands/cmd_arch.cpp
  src/ui/Gumball.cpp
  src/geom/TextOutline.cpp
  src/commands/cmd_annotate.cpp
  src/commands/cmd_annotate2.cpp
  src/commands/cmd_drafting.cpp
  src/commands/cmd_drafting2.cpp
  src/drafting/HatchLibrary.cpp
  src/drafting/Table.cpp
  src/drafting/Gdt.cpp
  src/drafting/SectionView.cpp
  src/commands/cmd_curves2.cpp
  src/commands/cmd_srfedit.cpp
  src/ui/Theme.cpp
  src/ui/Icons.cpp
  src/ui/Panels.cpp
  src/ui/MenuBar.cpp
  src/ui/Toolbars.cpp
  src/doc/Document.cpp
  src/doc/SceneObject.cpp
  src/doc/SubObjectEdit.cpp
  src/spatial/ObjectGrid.cpp
  src/viewport/Camera.cpp
  src/viewport/Viewport.cpp
  src/render/GlRenderer.cpp
  src/render/ImageIO.cpp
  src/render/Bvh.cpp
  src/render/MaterialLibrary.cpp
  src/render/PathTracer.cpp
  src/render/GpuRaytracer.cpp
  src/ui/RenderPanels.cpp
  src/commands/CommandEngine.cpp
  src/commands/CommandCatalog.cpp
  src/commands/cmd_create.cpp
  src/commands/cmd_solids.cpp
  src/commands/cmd_transform.cpp
  src/commands/cmd_edit.cpp
  src/commands/cmd_curveedit.cpp
  src/commands/cmd_remaining.cpp
  src/commands/cmd_surface.cpp
  src/commands/cmd_meshtools.cpp
  src/commands/cmd_subd.cpp
  src/commands/cmd_remesh.cpp
  src/geom/Remesh.cpp
  src/commands/cmd_solidtools.cpp
  src/commands/cmd_boolean.cpp
  src/commands/cmd_analyze.cpp
  src/commands/cmd_select.cpp
  src/commands/cmd_select2.cpp
  src/commands/cmd_state.cpp
  src/commands/cmd_view.cpp
  src/commands/cmd_viewtools.cpp
  src/commands/cmd_layer.cpp
  src/commands/cmd_file.cpp
  src/commands/cmd_misc.cpp
  src/commands/cmd_render.cpp
  src/script/LuaEngine.cpp
  src/script/PythonEngine.cpp
  src/commands/cmd_raytrace.cpp
  src/commands/cmd_exchange2.cpp
  src/io/File3dm.cpp
  src/io/FileExchange.cpp
  src/io/FileIgesStep.cpp
  src/geom/BrepMesher.cpp
  src/geom/SurfaceIntersect.cpp
  src/commands/cmd_fillet.cpp
  src/util/json_mini.cpp
  src/flow/FlowData.cpp
  src/flow/FlowGraph.cpp
  src/flow/FlowNodesCore.cpp
  src/flow/FlowNodesVector.cpp
  src/flow/FlowNodesCurve.cpp
  src/flow/FlowNodesSurface.cpp
  src/flow/FlowNodesSolver.cpp
  src/flow/FlowNodesOutput.cpp
  src/flow/FlowEditor.cpp
  src/flow/FlowPreview.cpp
  src/plugins/PluginManager.cpp
  src/plugins/PluginPanel.cpp
  src/commands/cmd_flow.cpp
  src/session/Digitizer.cpp
  src/session/Worksession.cpp
  src/commands/cmd_session.cpp
)

if(WIN32)
  add_executable(Dino8 WIN32 ${DINO8_SOURCES} resources/dino8.rc)
elseif(APPLE)
  add_executable(Dino8 MACOSX_BUNDLE ${DINO8_SOURCES})
  set_target_properties(Dino8 PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Dino 8"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.dino8.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
else()
  add_executable(Dino8 ${DINO8_SOURCES})
endif()

# FreeType (bundled with OpenNURBS) for text outlines.
FetchContent_GetProperties(opennurbs)
target_include_directories(Dino8 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include ${opennurbs_SOURCE_DIR}/freetype263/include ${libredwg_SOURCE_DIR}/include)
target_compile_definitions(Dino8 PRIVATE
  DINO8_VERSION="${PROJECT_VERSION}"
  GL_SILENCE_DEPRECATION
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
  $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
  $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
)
# OpenNURBS only builds its bundled FreeType on Linux/Android; build it
# ourselves elsewhere (macOS, Windows) so TextObject works everywhere.
if(NOT TARGET opennurbs_public_freetype)
  add_subdirectory(${opennurbs_SOURCE_DIR}/freetype263 ${CMAKE_BINARY_DIR}/freetype263 EXCLUDE_FROM_ALL)
  if(MSVC)
    target_compile_options(opennurbs_public_freetype PRIVATE /w)
  endif()
endif()
target_link_libraries(Dino8 PRIVATE dino8_kernel imgui glfw OpenGL::GL opennurbs_public_freetype dino8_lua ${DINO8_LIBREDWG_TARGET} ${CMAKE_DL_LIBS})
if(Python3_FOUND)
  target_compile_definitions(Dino8 PRIVATE DINO8_HAVE_PYTHON)
  target_link_libraries(Dino8 PRIVATE pybind11::embed)
endif()

if(MSVC)
  target_compile_options(Dino8 PRIVATE /W3 /EHsc /bigobj)
  # WIN32 subsystem still needs a main(): tell the linker to use it.
  target_link_options(Dino8 PRIVATE /ENTRY:mainCRTStartup)
else()
  target_compile_options(Dino8 PRIVATE -Wall -Wextra)
endif()

# The command catalog (all 1055 Rhino 8 commands with help text) ships next
# to the binary; the app also looks for it relative to the source tree when
# run from a build directory.
set(DINO8_DATA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/commands.json ${CMAKE_CURRENT_SOURCE_DIR}/data/hatchpatterns.pat)
add_custom_command(TARGET Dino8 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Dino8>/data
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DINO8_DATA_FILES} $<TARGET_FILE_DIR:Dino8>/data/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n $<TARGET_FILE_DIR:Dino8>/data/i18n
  COMMENT "Copying Dino8 data files next to the executable")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(DINO8_BUILD_SAMPLE_PLUGIN "Build the HelloDino sample plug-in (plugins/sample)" ON)
if(DINO8_BUILD_SAMPLE_PLUGIN)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/plugins/sample ${CMAKE_BINARY_DIR}/plugins/sample)
endif()

# Further plugin SDK examples (see docs/PLUGIN_SDK.md): each is a real,
# independently-built shared library demonstrating a different capability
# beyond HelloDino - mesh generation, curve generation, and multi-object
# selection/analysis I/O.
option(DINO8_BUILD_EXAMPLE_PLUGINS "Build the MeshTools/CurveTools/AnalysisTools example plug-ins (plugins/*)" ON)
if(DINO8_BUILD_EXAMPLE_PLUGINS)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/plugins/mesh_tools ${CMAKE_BINARY_DIR}/plugins/mesh_tools)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/plugins/curve_tools ${CMAKE_BINARY_DIR}/plugins/curve_tools)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/plugins/analysis_tools ${CMAKE_BINARY_DIR}/plugins/analysis_tools)
endif()

option(DINO8_BUILD_TESTS "Build Dino 8 unit tests" ON)
if(DINO8_BUILD_TESTS)
  enable_testing()
  add_executable(dino8_test_brep_mesher tests/test_brep_mesher.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_test_brep_mesher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_test_brep_mesher PRIVATE dino8_kernel)
  add_test(NAME dino8_brep_mesher COMMAND dino8_test_brep_mesher)

  # Property-based/randomized geometry fuzz test: random-but-valid NURBS
  # curves/surfaces, random primitive-mesh boolean pairs, and random
  # surface/surface intersections (the fillet family's core primitive),
  # checked against invariants (no crash/hang, no NaN/Inf, manifold where
  # claimed, positive primitive volume) that must hold for ANY valid input,
  # not just the fixed known-good shapes test_brep_mesher.cpp covers. Fixed
  # default seed for CI determinism; DINO8_FUZZ_SEED/argv[1] overrides it to
  # reproduce a specific failure. See tests/fuzz_qa_notes.md for any findings
  # documented rather than fixed.
  add_executable(dino8_test_fuzz_geometry tests/test_fuzz_geometry.cpp src/geom/BrepMesher.cpp src/geom/SurfaceIntersect.cpp)
  target_include_directories(dino8_test_fuzz_geometry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_test_fuzz_geometry PRIVATE dino8_kernel)
  add_test(NAME dino8_fuzz_geometry COMMAND dino8_test_fuzz_geometry)

  # Standalone timing benchmark for Document's diff-based undo/redo history
  # (see tests/undo_bench.cpp and tests/undo_scaling_notes.md) - not an
  # add_test(), it has nothing to assert, just numbers to print.
  add_executable(dino8_undo_bench tests/undo_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                   src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_undo_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_undo_bench PRIVATE dino8_kernel)

  # DWG round-trip fixture generator (see tests/dwg_fixture_gen.c and
  # smoke.sh's DWG section): builds a small real DWG file directly through
  # LibreDWG's public dwg_add_* API - the same API LibreDWG's own
  # test/unit-testing/add_test.c uses - containing a BLOCK_HEADER + INSERT
  # (block instance) that Dino 8 cannot itself produce on export (Dino 8's
  # own object model has no live block/instance entity type; a block
  # instance placed in-app is stored as already-flattened geometry, so
  # Dino8-authored exports never contain a real DWG/DXF INSERT to read
  # back). This program exists purely to give ImportDwg's INSERT-flattening
  # code (FileExchange.cpp's WalkDwgEntities) a real INSERT to prove it
  # against, independent of what Dino 8's own exporter happens to produce.
  add_executable(dwg_fixture_gen tests/dwg_fixture_gen.c)
  target_include_directories(dwg_fixture_gen PRIVATE ${libredwg_SOURCE_DIR}/include)
  target_link_libraries(dwg_fixture_gen PRIVATE ${DINO8_LIBREDWG_TARGET})
  if(UNIX AND NOT APPLE)
    # A plain C executable (unlike Dino8, which implicitly pulls in libm
    # through libstdc++) needs libm linked explicitly for LibreDWG's own
    # floor/atan2/sincos/trunc calls.
    target_link_libraries(dwg_fixture_gen PRIVATE m)
  endif()
endif()

# ---------------------------------------------------------------------------
# Install + packaging
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
# Everything Dino 8 ships is in the "app" component; the dependencies pulled
# in by FetchContent (OpenNURBS, Manifold, GLFW) carry their own install
# rules, which we never want in the package.
if(APPLE)
  install(TARGETS Dino8 BUNDLE DESTINATION . COMPONENT app)
  install(FILES ${DINO8_DATA_FILES} DESTINATION "Dino8.app/Contents/Resources/data" COMPONENT app)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n DESTINATION "Dino8.app/Contents/Resources/data" COMPONENT app)
elseif(WIN32)
  install(TARGETS Dino8 RUNTIME DESTINATION . COMPONENT app)
  install(FILES ${DINO8_DATA_FILES} DESTINATION data COMPONENT app)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n DESTINATION data COMPONENT app)
else()
  install(TARGETS Dino8 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
  install(FILES ${DINO8_DATA_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/dino8/data COMPONENT app)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n DESTINATION ${CMAKE_INSTALL_DATADIR}/dino8/data COMPONENT app)
  install(FILES packaging/linux/dino8.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT app)
  install(FILES resources/dino8.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME dino8.png COMPONENT app OPTIONAL)
endif()

set(CPACK_COMPONENTS_ALL app)
# Install only the app component when packaging (dependency subprojects
# would otherwise try to install libraries that are never built).
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};Dino8;app;/")
set(CPACK_COMPONENT_APP_DISPLAY_NAME "Dino 8")
set(CPACK_MONOLITHIC_INSTALL OFF)
set(CPACK_DEB_COMPONENT_INSTALL OFF)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_PACKAGE_NAME "Dino8")
set(CPACK_PACKAGE_VENDOR "Dino 8 Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Dino 8 - free NURBS/SubD/mesh 3D modeler")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Dino8")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
else()
  set(CPACK_GENERATOR "DEB;TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dino 8 Project")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgl1, libx11-6")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()
include(CPack)
