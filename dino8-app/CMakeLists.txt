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
  src/ui/Gumball.cpp
  src/geom/TextOutline.cpp
  src/commands/cmd_annotate.cpp
  src/commands/cmd_drafting.cpp
  src/commands/cmd_curves2.cpp
  src/commands/cmd_srfedit.cpp
  src/ui/Theme.cpp
  src/ui/Panels.cpp
  src/ui/MenuBar.cpp
  src/ui/Toolbars.cpp
  src/doc/Document.cpp
  src/doc/SceneObject.cpp
  src/viewport/Camera.cpp
  src/viewport/Viewport.cpp
  src/render/GlRenderer.cpp
  src/commands/CommandEngine.cpp
  src/commands/CommandCatalog.cpp
  src/commands/cmd_create.cpp
  src/commands/cmd_solids.cpp
  src/commands/cmd_transform.cpp
  src/commands/cmd_edit.cpp
  src/commands/cmd_curveedit.cpp
  src/commands/cmd_surface.cpp
  src/commands/cmd_meshtools.cpp
  src/commands/cmd_boolean.cpp
  src/commands/cmd_analyze.cpp
  src/commands/cmd_select.cpp
  src/commands/cmd_view.cpp
  src/commands/cmd_layer.cpp
  src/commands/cmd_file.cpp
  src/commands/cmd_misc.cpp
  src/io/File3dm.cpp
  src/io/FileExchange.cpp
  src/geom/BrepMesher.cpp
  src/util/json_mini.cpp
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
target_include_directories(Dino8 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${opennurbs_SOURCE_DIR}/freetype263/include)
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
target_link_libraries(Dino8 PRIVATE dino8_kernel imgui glfw OpenGL::GL opennurbs_public_freetype)

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
set(DINO8_DATA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/commands.json)
add_custom_command(TARGET Dino8 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Dino8>/data
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DINO8_DATA_FILES} $<TARGET_FILE_DIR:Dino8>/data/
  COMMENT "Copying Dino8 data files next to the executable")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(DINO8_BUILD_TESTS "Build Dino 8 unit tests" ON)
if(DINO8_BUILD_TESTS)
  enable_testing()
  add_executable(dino8_test_brep_mesher tests/test_brep_mesher.cpp src/geom/BrepMesher.cpp)
  target_include_directories(dino8_test_brep_mesher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(dino8_test_brep_mesher PRIVATE dino8_kernel)
  add_test(NAME dino8_brep_mesher COMMAND dino8_test_brep_mesher)
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
elseif(WIN32)
  install(TARGETS Dino8 RUNTIME DESTINATION . COMPONENT app)
  install(FILES ${DINO8_DATA_FILES} DESTINATION data COMPONENT app)
else()
  install(TARGETS Dino8 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
  install(FILES ${DINO8_DATA_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/dino8/data COMPONENT app)
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
