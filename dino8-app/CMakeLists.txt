cmake_minimum_required(VERSION 3.20)
project(Dino8 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Windows: link the C/C++ runtime statically (/MT) into Dino8.exe and every
# static library it is built from. The installer and portable zip ship the
# bare exe plus data files - no vc_redist bootstrapper, no bundled
# vcruntime140.dll/msvcp140.dll - so with the default /MD the exe depends on
# the Visual C++ 2015-2022 redistributable being present on the user's
# machine, which nothing in the install tree guarantees. Everything linked
# into the exe is built here from source under this same policy (OpenNURBS,
# GLFW, ImGui, Manifold, LibreDWG, Lua, FreeType), so there is no
# mixed-runtime object to conflict with. CMP0091 (NEW since 3.15, and this
# project requires 3.20) makes CMAKE_MSVC_RUNTIME_LIBRARY the single source
# of truth for every target below, including the FetchContent'd ones; GLFW's
# own USE_MSVC_RUNTIME_LIBRARY_DLL option is switched off to match rather
# than be left saying "DLL" while the flag actually in effect is /MT.
# The "Dependents check" step in .github/workflows/dino8-app.yml verifies the
# result on every Windows CI run: dumpbin /DEPENDENTS on the installed exe
# must list only OS-provided DLLs.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
endif()

# ClipboardMac.mm (src/platform/) is Objective-C++ - the real NSPasteboard
# shim behind the OS-clipboard image commands (ViewCaptureToClipboard,
# ScreenCaptureToClipboard, CopyRenderWindowToClipboard) on macOS. The
# language must be enabled before any target references that file. OBJC is
# enabled alongside it (not just OBJCXX) so that plain Objective-C .m files
# fetched from third-party dependencies (GLFW's Cocoa backend) keep
# compiling as Objective-C rather than being swept into Objective-C++ by
# CMake's default source-to-language mapping when only OBJCXX is known -
# GLFW 3.4's cocoa_*.m files rely on C-style implicit void*-to-typed-pointer
# conversions (e.g. from calloc-style allocators) that Objective-C++ rejects.
if(APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()

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
#
# DINO8_ENABLE_PYTHON defaults OFF on Windows: the exe would otherwise import
# whatever pythonXY.dll find_package located on the BUILD machine (on GitHub's
# windows-latest runner that is the hosted-toolcache Python 3.14 - see the
# "Python3 3.14.7 found (C:/hostedtoolcache/...)" line in past CI logs), and
# neither that DLL nor its standard library is part of the install tree the
# Inno Setup installer / portable zip are built from - so every shipped
# Windows build launched on a machine without that exact Python failed at
# load time with "python314.dll was not found", before main() ever ran.
# Bundling a CPython runtime properly (pythonXY.dll + a zipped stdlib +
# PyConfig.home pointing at it) is the real fix for Windows parity; until
# that lands, the Windows build honestly reports Python as unavailable
# (RunPythonScript's own fallback message; tests/smoke.sh skips its Python
# checks on that message) instead of shipping a binary that cannot start.
# Pass -DDINO8_ENABLE_PYTHON=ON to a Windows build that ships its own
# python runtime next to the exe.
if(WIN32)
  option(DINO8_ENABLE_PYTHON "Embed CPython (RunPythonScript / dino8 module)" OFF)
else()
  option(DINO8_ENABLE_PYTHON "Embed CPython (RunPythonScript / dino8 module)" ON)
endif()
if(DINO8_ENABLE_PYTHON)
  find_package(Python3 3.8 COMPONENTS Interpreter Development.Embed)
else()
  set(Python3_FOUND OFF)
  message(STATUS "DINO8_ENABLE_PYTHON is OFF - building without embedded Python scripting (RunPythonScript will report that it is unavailable)")
endif()
if(Python3_FOUND)
  message(STATUS "Python3 ${Python3_VERSION} found (${Python3_LIBRARIES}); building embedded Python scripting (dino8 module)")
  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6
  )
  FetchContent_MakeAvailable(pybind11)
elseif(DINO8_ENABLE_PYTHON)
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
  # See cmake/patch_libredwg.cmake for what this fixes and why: a real,
  # platform-dependent (Windows-only) memory-corruption bug in LibreDWG
  # 0.14.8594's own decode.c, root-caused by this project's own multi-round
  # Windows DWG-reopen-crash investigation (see src/io/FileExchange.cpp).
  PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_libredwg.cmake
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
  src/elec/ElecComponents.cpp
  src/commands/cmd_elec.cpp
  src/commands/cmd_standards.cpp
  src/ui/Gumball.cpp
  src/ui/MappingGizmo.cpp
  src/ui/DirectionArrows.cpp
  src/geom/TextOutline.cpp
  src/commands/cmd_annotate.cpp
  src/commands/cmd_annotate2.cpp
  src/commands/cmd_drafting.cpp
  src/commands/cmd_smartblocks.cpp
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
  src/doc/BlockInstances.cpp
  src/spatial/ObjectGrid.cpp
  src/viewport/Camera.cpp
  src/viewport/Viewport.cpp
  src/render/GlRenderer.cpp
  src/render/ImageIO.cpp
  src/platform/Clipboard.cpp
  src/platform/AccessibilityTree.cpp
  src/platform/Accessibility.cpp
  src/render/Bvh.cpp
  src/render/MaterialLibrary.cpp
  src/render/PathTracer.cpp
  src/render/GpuRaytracer.cpp
  src/ui/RenderPanels.cpp
  src/commands/CommandEngine.cpp
  src/commands/CommandCatalog.cpp
  src/commands/cmd_create.cpp
  src/commands/cmd_solids.cpp
  src/commands/cmd_history.cpp
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
  src/io/AciPalette.cpp
  src/io/FileExchange.cpp
  src/io/FileIgesStep.cpp
  src/geom/BrepMesher.cpp
  src/geom/SurfaceIntersect.cpp
  src/geom/BlendSurface.cpp
  src/geom/BrepTrimFace.cpp
  src/commands/cmd_fillet.cpp
  src/util/json_mini.cpp
  src/util/Sha256.cpp
  src/util/Inflate.cpp
  src/drafting/Xlsx.cpp
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
  src/plugins/MarketplaceIndex.cpp
  src/plugins/Marketplace.cpp
  src/plugins/MarketplacePanel.cpp
  src/plugins/PluginReviews.cpp
  src/commands/cmd_flow.cpp
  src/session/Digitizer.cpp
  src/session/Worksession.cpp
  src/session/SheetSet.cpp
  src/commands/cmd_session.cpp
  src/compare/DwgCompare.cpp
  src/commands/cmd_compare.cpp
  src/commands/cmd_sheetset.cpp
)

# Real OS-clipboard image backend for ViewCaptureToClipboard/
# ScreenCaptureToClipboard/CopyRenderWindowToClipboard: exactly one
# platform-specific translation unit per host OS (see src/platform/
# Clipboard.h for why - each is a genuinely different native API).
if(WIN32)
  list(APPEND DINO8_SOURCES src/platform/ClipboardWin32.cpp)
elseif(APPLE)
  list(APPEND DINO8_SOURCES src/platform/ClipboardMac.mm)
  set_source_files_properties(src/platform/ClipboardMac.mm PROPERTIES LANGUAGE OBJCXX)
else()
  list(APPEND DINO8_SOURCES src/platform/ClipboardLinuxX11.cpp)
  # libX11-dev is already a build requirement for GLFW's X11 backend
  # (GLFW_BUILD_X11, above); this just links the same library directly for
  # our own ICCCM CLIPBOARD-selection-owner code.
  find_package(X11 REQUIRED)

  # Real AT-SPI2 accessibility bridge for the command line (see
  # src/platform/AccessibilityLinux.cpp and docs/ACCESSIBILITY.md) - built
  # only when its D-Bus client libraries are actually available. Unlike
  # X11 above, this is NOT a hard build requirement: a build environment
  # without libatspi2.0-dev/libglib2.0-dev (most CI containers, most
  # non-desktop Linux boxes) still gets a working Dino8, just without an
  # AT-SPI-visible command line - Accessibility.cpp's DINO8_HAVE_ATSPI-gated
  # no-op fallback covers that case identically to any other platform.
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(DINO8_ATSPI atspi-2 gio-2.0)
  endif()
  if(DINO8_ATSPI_FOUND)
    list(APPEND DINO8_SOURCES src/platform/AccessibilityLinux.cpp)
  else()
    message(STATUS "atspi-2/gio-2.0 not found (libatspi2.0-dev, libglib2.0-dev) - building without the AT-SPI2 "
                    "accessibility bridge; see dino8-app/docs/ACCESSIBILITY.md")
  endif()
endif()

if(WIN32)
  # resources/dino8.manifest (UTF-8 active code page, long paths, per-monitor
  # DPI) is merged into the exe's embedded manifest by CMake's MSVC link step
  # (mt.exe) because it is listed as a source; see the file's own comment.
  add_executable(Dino8 WIN32 ${DINO8_SOURCES} resources/dino8.rc resources/dino8.manifest)
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
# WIN32_LEAN_AND_MEAN is deliberately NOT defined here: OpenNURBS'
# opennurbs_system.h #defines it unconditionally before its own windows.h
# include, so a project-wide -D produced 95 "C4005 macro redefinition"
# warnings per Windows build (one per TU that includes opennurbs.h). Every
# first-party windows.h include site defines it itself, guarded by #ifndef.
target_compile_definitions(Dino8 PRIVATE
  DINO8_VERSION="${PROJECT_VERSION}"
  GL_SILENCE_DEPRECATION
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
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

# Clipboard backend libraries (see the platform-specific source list above).
# ClipboardWin32.cpp only needs User32 (OpenClipboard/SetClipboardData/...
# and RegisterClipboardFormatW live there; GlobalAlloc/Lock/Unlock/Free are
# Kernel32, already linked by default) - no GDI object is created.
if(WIN32)
  # shell32: ShellExecuteA behind OpenURL/WebBrowser/FileExplorer
  # (commands/cmd_state.cpp).
  target_link_libraries(Dino8 PRIVATE user32 shell32)
elseif(APPLE)
  target_link_libraries(Dino8 PRIVATE "-framework AppKit")
else()
  target_include_directories(Dino8 PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(Dino8 PRIVATE ${X11_LIBRARIES})
  if(DINO8_ATSPI_FOUND)
    target_compile_definitions(Dino8 PRIVATE DINO8_HAVE_ATSPI)
    target_include_directories(Dino8 PRIVATE ${DINO8_ATSPI_INCLUDE_DIRS})
    target_link_directories(Dino8 PRIVATE ${DINO8_ATSPI_LIBRARY_DIRS})
    target_link_libraries(Dino8 PRIVATE ${DINO8_ATSPI_LIBRARIES})
  endif()
endif()

if(MSVC)
  target_compile_options(Dino8 PRIVATE /W3 /EHsc /bigobj)
  # WIN32 subsystem still needs a main(): tell the linker to use it.
  target_link_options(Dino8 PRIVATE /ENTRY:mainCRTStartup)
  # Windows' default thread stack is 1MB, far smaller than Linux's typical
  # 8MB. Several kernel/booleans/mesher code paths recurse proportionally to
  # model complexity and are only ever exercised on Linux's larger stack by
  # the smoke suite, so give the main thread the same headroom here (16MB
  # reserve+commit) rather than let a deep model overflow on Windows only.
  target_link_options(Dino8 PRIVATE /STACK:16777216,16777216)
else()
  target_compile_options(Dino8 PRIVATE -Wall -Wextra)
endif()

# The command catalog (all 1055 Rhino 8 commands with help text) ships next
# to the binary; the app also looks for it relative to the source tree when
# run from a build directory. changelog.md ships alongside it: Panels.cpp's
# Changelog()/DrawWhatsNewWindow() reads data/changelog.md next to the exe
# for the WhatsNew command, using the same search order as commands.json -
# leaving it out of this list meant it was never copied to the build dir or
# installed on any platform, so every packaged build (Windows installer,
# .deb, .dmg) silently showed "The changelog ... could not be found".
set(DINO8_DATA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/commands.json ${CMAKE_CURRENT_SOURCE_DIR}/data/hatchpatterns.pat ${CMAKE_CURRENT_SOURCE_DIR}/data/changelog.md)
add_custom_command(TARGET Dino8 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Dino8>/data
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DINO8_DATA_FILES} $<TARGET_FILE_DIR:Dino8>/data/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n $<TARGET_FILE_DIR:Dino8>/data/i18n
  # The bundled plug-in marketplace reference index (plugin-index/index.json,
  # see plugin-index/SCHEMA.md) ships next to the executable too, at
  # data/plugin-index/index.json - Application::DefaultMarketplaceIndexPath
  # looks for it there first, so PluginMarketplaceIndex/the marketplace
  # panel's "Load Bundled Index" button work with no path or URL typed in.
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Dino8>/data/plugin-index
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/plugin-index/index.json $<TARGET_FILE_DIR:Dino8>/data/plugin-index/
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

  # AutoCAD Colour Index palette (src/io/AciPalette.cpp): table-driven
  # RgbToAci/AciToRgb checks. Exists mainly so the Windows ctest step
  # catches a regression of the LLP64 bug this replaced - a 32-bit `long`
  # sentinel in the nearest-ACI search that sent every colour more than a
  # few units from a palette entry to ACI 7 on MSVC only. No kernel or
  # third-party dependency: the palette code is deliberately standalone.
  add_executable(dino8_test_aci_palette tests/test_aci_palette.cpp src/io/AciPalette.cpp)
  target_include_directories(dino8_test_aci_palette PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME dino8_aci_palette COMMAND dino8_test_aci_palette)

  # Plug-in marketplace index parsing (src/plugins/MarketplaceIndex.cpp) and
  # its SHA-256 download-integrity check (src/util/Sha256.cpp): schema
  # validation, error paths, and the real plugin-index/index.json reference
  # index (passed below as an absolute path, independent of ctest's working
  # directory). Deliberately standalone, like dino8_test_aci_palette - the
  # install half (InstallEntry, src/plugins/Marketplace.cpp) needs a live
  # app::Application and is exercised end to end instead through the real
  # Dino8 binary by tests/plugin_marketplace_script.txt (see tests/smoke.sh).
  add_executable(dino8_test_plugin_marketplace tests/test_plugin_marketplace.cpp
    src/plugins/MarketplaceIndex.cpp src/util/Sha256.cpp src/util/json_mini.cpp)
  target_include_directories(dino8_test_plugin_marketplace PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME dino8_plugin_marketplace COMMAND dino8_test_plugin_marketplace ${CMAKE_CURRENT_SOURCE_DIR}/plugin-index/index.json)

  # Plug-in ratings/reviews (src/plugins/PluginReviews.cpp): JSON parsing/
  # serialization of the local reviews store, schema validation, and
  # PluginReviewStore's add/persist/reload round-trip. Deliberately
  # standalone, like dino8_test_plugin_marketplace above - it only touches a
  # plain file path, never app::Application, so the Marketplace panel's
  # "Rate this plug-in" UI doesn't need to be driven through the real
  # Dino8 binary to prove the storage layer underneath it is correct.
  add_executable(dino8_test_plugin_reviews tests/test_plugin_reviews.cpp src/plugins/PluginReviews.cpp src/util/json_mini.cpp)
  target_include_directories(dino8_test_plugin_reviews PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME dino8_plugin_reviews COMMAND dino8_test_plugin_reviews)

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

  # Numerically verifies BuildBlendSurfaceG2's own claim (geom/BlendSurface.h):
  # that its Continuity=Curvature blend's curvature vector at the boundary
  # actually matches each adjacent surface's own curvature there (a real G2
  # check), and that this differs measurably from the old/G1
  # BuildBlendSurfaceG1 tangent-magnitude-boost approximation, which matches
  # the tangent PLANE only and leaves a real curvature gap. See
  # tests/test_g2_blend.cpp.
  add_executable(dino8_test_g2_blend tests/test_g2_blend.cpp src/geom/BlendSurface.cpp)
  target_include_directories(dino8_test_g2_blend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_test_g2_blend PRIVATE dino8_kernel)
  add_test(NAME dino8_g2_blend COMMAND dino8_test_g2_blend)

  # Verifies BuildBlendSurfaceG1Adaptive/BuildBlendSurfaceG2Adaptive (geom/
  # BlendSurface.h): the adaptive, max_gap-enforcing wrapper that closes
  # PARITY_MAP.md's "Rolling-ball blend surface accuracy on freeform
  # surfaces - enforce max_gap" gap, by doubling the row sample count until
  # two successive resolutions agree to within max_gap (there is no
  # closed-form rolling-ball envelope to check a freeform blend against at
  # all - see MaxSurfaceGap's own doc comment). See
  # tests/test_blend_adaptive_gap.cpp.
  add_executable(dino8_test_blend_adaptive_gap tests/test_blend_adaptive_gap.cpp src/geom/BlendSurface.cpp)
  target_include_directories(dino8_test_blend_adaptive_gap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_test_blend_adaptive_gap PRIVATE dino8_kernel)
  add_test(NAME dino8_blend_adaptive_gap COMMAND dino8_test_blend_adaptive_gap)

  # Verifies the internal accessibility-tree mirror (see
  # src/platform/AccessibilityTree.h and docs/ACCESSIBILITY.md) that
  # AccessibilityLinux.cpp's real AT-SPI2 bridge publishes over D-Bus. Pure
  # C++, no GLFW/ImGui/D-Bus/dino8_kernel dependency at all, so it runs
  # identically on every platform and every CI job regardless of whether
  # AT-SPI2's own client libraries (atspi-2, gio-2.0) were found at
  # configure time - see tests/smoke_accessibility.py for the end-to-end
  # AT-SPI2 counterpart, which does need those.
  add_executable(dino8_test_accessibility_tree tests/test_accessibility_tree.cpp src/platform/AccessibilityTree.cpp)
  target_include_directories(dino8_test_accessibility_tree PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME dino8_accessibility_tree COMMAND dino8_test_accessibility_tree)

  # Key-coverage check for the i18n language tables (data/i18n/*.json):
  # every hand-translated language must define every key en.json defines,
  # except es.json's one documented deliberate gap (panel.imgui_demo, kept
  # missing on purpose so I18nSelfTest/smoke.sh has a real fallback case to
  # exercise). Guards against a language file quietly falling behind as new
  # UI-chrome keys are added to en.json - exactly the kind of silent drift
  # that let fr.json/de.json/es.json fall behind before ja.json (Japanese)
  # was added as the fourth real, hand-translated language. Dependency-free
  # like dino8_test_aci_palette/dino8_test_plugin_marketplace: no live
  # app::Application, just json_mini over the real data files.
  add_executable(dino8_test_i18n_coverage tests/test_i18n_coverage.cpp src/util/json_mini.cpp)
  target_include_directories(dino8_test_i18n_coverage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME dino8_i18n_coverage COMMAND dino8_test_i18n_coverage ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n)

  # Radiance .hdr (RGBE) codec (src/render/ImageIO.cpp's LoadImageHdr/
  # SaveImageHdr): an encode/decode round trip well above the [0,1] range
  # PNG/BMP/PPM are stuck at, a real-world-shaped new-style-RLE scanline
  # decoded by hand, and the malformed-input error paths. Standalone like
  # dino8_test_aci_palette - ImageIO.cpp has no dino8_kernel dependency.
  add_executable(dino8_test_image_hdr tests/test_image_hdr.cpp src/render/ImageIO.cpp src/util/Inflate.cpp)
  target_include_directories(dino8_test_image_hdr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME dino8_image_hdr COMMAND dino8_test_image_hdr)

  # ZIP + minimal OOXML SpreadsheetML reader/writer (src/drafting/Xlsx.cpp):
  # a real, valid .xlsx round trip (special characters, empty cells, several
  # rows/cols), a hand-built "real-world-shaped" entry using zip method 8
  # (deflate) with shared strings - what an actual Excel/LibreOffice/openpyxl
  # save produces, not just this app's own store-method writer - and the
  # malformed/truncated/bad-CRC error paths. Standalone like
  # dino8_test_image_hdr/dino8_test_aci_palette - Xlsx.cpp depends only on
  # util/Inflate.cpp, no dino8_kernel/opennurbs.
  add_executable(dino8_test_xlsx tests/test_xlsx.cpp src/drafting/Xlsx.cpp src/util/Inflate.cpp)
  target_include_directories(dino8_test_xlsx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME dino8_xlsx COMMAND dino8_test_xlsx)

  # Numerically verifies VariableBlendSrf's own claim (cmd_fillet.cpp's
  # VariableBlendSrfCommand): that BuildBlendSurfaceG1/G2's new
  # width_frac_at hook makes the blend's cross-section width genuinely
  # vary along the rail (measured on the constructed surface's own CVs),
  # while the G2 path's real curvature match (see dino8_g2_blend above)
  # still holds independent of that width. See tests/test_variable_blend.cpp.
  add_executable(dino8_test_variable_blend tests/test_variable_blend.cpp src/geom/BlendSurface.cpp)
  target_include_directories(dino8_test_variable_blend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_test_variable_blend PRIVATE dino8_kernel)
  add_test(NAME dino8_variable_blend COMMAND dino8_test_variable_blend)

  # Standalone timing benchmark for Document's diff-based undo/redo history
  # (see tests/undo_bench.cpp and tests/undo_scaling_notes.md) - not an
  # add_test(), it has nothing to assert, just numbers to print.
  add_executable(dino8_undo_bench tests/undo_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                   src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_undo_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_undo_bench PRIVATE dino8_kernel)

  # Timing benchmark + regression guard for Document::Find() vs
  # Document::FindMany() on the "select k of N objects, then transform them"
  # path (see tests/find_bench.cpp) - unlike dino8_undo_bench above, this one
  # does assert (a same-run relative speedup check, robust to machine load),
  # so it's wired into ctest as a real pass/fail test, not just numbers.
  add_executable(dino8_find_bench tests/find_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                   src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_find_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_find_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_find_bench COMMAND dino8_find_bench)

  # Timing benchmark + regression guard for Document::Remove() in a loop vs
  # Document::RemoveMany() on the "select k of N objects, then delete them"
  # path (see tests/delete_bench.cpp) - same style as dino8_find_bench above:
  # asserts a same-run relative speedup, so it's wired into ctest as a real
  # pass/fail test, not just numbers.
  add_executable(dino8_delete_bench tests/delete_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                     src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_delete_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_delete_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_delete_bench COMMAND dino8_delete_bench)

  # Timing benchmark + regression guard for the "Isolate" command's
  # selection-membership test (see tests/isolate_bench.cpp): std::find(ids...)
  # inside a loop over every document object (O(document size * selection
  # size)) vs hashing the selection once into an unordered_set first
  # (O(document size + selection size)). Same style as dino8_find_bench/
  # dino8_delete_bench above: asserts a same-run relative speedup.
  add_executable(dino8_isolate_bench tests/isolate_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                      src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_isolate_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_isolate_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_isolate_bench COMMAND dino8_isolate_bench)

  # Timing benchmark + regression guard for Array/ArrayLinear/ArrayPolar and
  # ApplyXform's Copy=Yes path (cmd_transform.cpp): Document::Find(id) once
  # per (copy x selected id) pair (see tests/array_bench.cpp) vs resolving
  # the selection's object values once via Document::FindMany() and reusing
  # them for every copy. Same style as dino8_find_bench/dino8_isolate_bench
  # above: asserts a same-run relative speedup.
  add_executable(dino8_array_bench tests/array_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                    src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_array_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_array_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_array_bench COMMAND dino8_array_bench)

  # Timing benchmark + regression guard for the layer commands that
  # bulk-retarget a selection - ChangeLayer/ChangeToCurrentLayer/MatchLayer
  # (cmd_layer.cpp): Document::Find(id) once per selected id (see
  # tests/layer_bench.cpp) vs resolving the whole selection in one
  # Document::FindMany() call. Same style as dino8_find_bench/
  # dino8_array_bench above: asserts a same-run relative speedup.
  add_executable(dino8_layer_bench tests/layer_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                    src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_layer_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_layer_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_layer_bench COMMAND dino8_layer_bench)

  # Timing benchmark + regression guard for the bulk property commands that
  # retarget a fixed selection - SetObjectName/SetUserText (cmd_edit.cpp),
  # SetRenderColor (cmd_misc.cpp) and MatchProperties (cmd_remaining.cpp):
  # Document::Find(id) once per selected id (see tests/properties_bench.cpp)
  # vs resolving the whole selection in one Document::FindMany() call. Same
  # style/shared loop shape as dino8_layer_bench above: asserts a same-run
  # relative speedup.
  add_executable(dino8_properties_bench tests/properties_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                         src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_properties_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_properties_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_properties_bench COMMAND dino8_properties_bench)

  # Timing benchmark + regression guard for Document::Ungroup()/
  # RemoveEmptyGroups() (see tests/group_bench.cpp): pruning empty groups
  # used to call GroupMembers(id) - an O(document size) scan - once per
  # *existing* group (O(group count x document size), independent of the
  # ids passed to Ungroup()) vs a single O(document size + group count)
  # pass building a membership count map (the shared PruneEmptyGroups()
  # helper). Same style as dino8_properties_bench/dino8_layer_bench above:
  # asserts a same-run relative speedup.
  add_executable(dino8_group_bench tests/group_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                    src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_group_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_group_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_group_bench COMMAND dino8_group_bench)

  # Timing benchmark + regression guard for Document::BoundingBoxOf() (see
  # tests/bbox_bench.cpp): used to call Find(id) - an O(document size) scan -
  # once per id in the selection (O(selection size x document size)), and
  # backs Gumball::Update()'s per-frame "where's the selection's centre"
  # query, so the quadratic cost ran every rendered frame, not just once per
  # command. Fixed to resolve the whole selection with a single FindMany()
  # call. Same style as dino8_properties_bench/dino8_layer_bench/
  # dino8_group_bench above: asserts a same-run relative speedup.
  add_executable(dino8_bbox_bench tests/bbox_bench.cpp src/doc/Document.cpp src/doc/SceneObject.cpp
                                   src/doc/SubObjectEdit.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_bbox_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_bbox_bench PRIVATE dino8_kernel)
  add_test(NAME dino8_bbox_bench COMMAND dino8_bbox_bench)

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

  # Corrupt-mesh .3dm fixture generator (see tests/mesh_fixture_gen.cpp and
  # smoke.sh's Load3dm mesh-range-check section): writes a mesh with a face
  # indexing past its own vertex array directly through OpenNURBS'
  # ONX_Model/ON_Mesh API, since Dino 8 has no command of its own that can
  # construct such a mesh to round-trip through Save. Links dino8_kernel
  # (rather than opennurbsStatic directly) to reuse its existing
  # link-order workaround for opennurbsStatic/zlib on GNU ld.
  add_executable(mesh_fixture_gen tests/mesh_fixture_gen.cpp)
  target_include_directories(mesh_fixture_gen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(mesh_fixture_gen PRIVATE dino8_kernel)
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
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugin-index/index.json DESTINATION "Dino8.app/Contents/Resources/data/plugin-index" COMPONENT app)
elseif(WIN32)
  install(TARGETS Dino8 RUNTIME DESTINATION . COMPONENT app)
  install(FILES ${DINO8_DATA_FILES} DESTINATION data COMPONENT app)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n DESTINATION data COMPONENT app)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugin-index/index.json DESTINATION data/plugin-index COMPONENT app)
else()
  install(TARGETS Dino8 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
  install(FILES ${DINO8_DATA_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/dino8/data COMPONENT app)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/i18n DESTINATION ${CMAKE_INSTALL_DATADIR}/dino8/data COMPONENT app)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugin-index/index.json DESTINATION ${CMAKE_INSTALL_DATADIR}/dino8/data/plugin-index COMPONENT app)
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
