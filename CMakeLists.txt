cmake_minimum_required(VERSION 3.25)
project(TheBlackWall VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BW_SYSTEM_SDL "Link against an SDL3 already installed on the system" OFF)

# ---------------------------------------------------------------------------
# SDL3 (zlib license). Statically linked so the screensaver is a single file.
# ---------------------------------------------------------------------------
if(BW_SYSTEM_SDL)
  find_package(SDL3 REQUIRED CONFIG)
else()
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
  # Only what a screensaver needs: video + OpenGL + events + timers.
  foreach(sub AUDIO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI SENSOR DIALOG POWER OPENGLES)
    set(SDL_${sub} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(SDL_OPENGL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

# ---------------------------------------------------------------------------
# Shaders are embedded as C byte arrays at build time (see cmake/embed.cmake).
# ---------------------------------------------------------------------------
set(BW_SHADERS
  src/shaders/wall.vert src/shaders/wall.frag
  src/shaders/floor.vert src/shaders/floor.frag
  src/shaders/post.vert src/shaders/ghost.frag
  src/shaders/blur.frag src/shaders/blit.frag src/shaders/glitch.frag
  src/shaders/debris.vert src/shaders/city.vert src/shaders/man.vert
  src/shaders/mask.vert src/shaders/mask.frag src/shaders/ember.vert)
set(BW_SHADER_ABS)
foreach(s ${BW_SHADERS})
  list(APPEND BW_SHADER_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${s})
endforeach()
set(BW_SHADERS_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen/shaders_gen.c)
add_custom_command(
  OUTPUT  ${BW_SHADERS_GEN}
  COMMAND ${CMAKE_COMMAND} -DOUT=${BW_SHADERS_GEN} "-DINPUTS=${BW_SHADER_ABS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${BW_SHADER_ABS} cmake/embed.cmake
  COMMENT "Embedding GLSL shaders"
  # INPUTS is a ;-separated list: VERBATIM quotes it, or a Unix shell would
  # take each ; as the end of a command.
  VERBATIM)

# ---------------------------------------------------------------------------
# The figures: vertices of the models in res/models become embedded tables
# of quantised points (tools/gltf2points.c, using cgltf - MIT).
# Keep this list in sync with BW_MODELS in src/models.h (name and order).
#   name : file : standing height in world units
# ---------------------------------------------------------------------------
add_executable(gltf2points tools/gltf2points.c)
target_include_directories(gltf2points PRIVATE third_party)
if(NOT WIN32)
  target_link_libraries(gltf2points PRIVATE m)
endif()
# Figures are normalised to Johnny's height (1.80), except the giants:
# Brendan 3 m, Alt 15 m (she dominates). Skippy is scaled up to human height.
set(BW_MODEL_LIST
  "johny:johny.glb:1.80"
  "alt:Alt.glb:15.00"
  "songbird:Songbird.glb:1.80"
  "brendan:Brendan.glb:3.00"
  "skippy:Skippy.glb:1.80"
  "david:David.glb:1.80"
  "jackie:Jackie.glb:1.80")
set(BW_MODEL_POINTS 40000 CACHE STRING "Maximum number of points kept per figure model")
set(BW_MODEL_GEN)
foreach(entry ${BW_MODEL_LIST})
  string(REPLACE ":" ";" parts "${entry}")
  list(GET parts 0 mname)
  list(GET parts 1 mfile)
  list(GET parts 2 mheight)
  set(mout ${CMAKE_CURRENT_BINARY_DIR}/gen/model_${mname}.c)
  add_custom_command(
    OUTPUT  ${mout}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen
    COMMAND $<TARGET_FILE:gltf2points> ${CMAKE_CURRENT_SOURCE_DIR}/res/models/${mfile}
            ${mout} ${BW_MODEL_POINTS} ${mname} ${mheight}
    DEPENDS gltf2points ${CMAKE_CURRENT_SOURCE_DIR}/res/models/${mfile}
    COMMENT "Converting figure model ${mname}"
    VERBATIM)
  list(APPEND BW_MODEL_GEN ${mout})
endforeach()

# ---------------------------------------------------------------------------
# The screensaver executable
# ---------------------------------------------------------------------------
set(BW_SOURCES
  src/main.c src/app.c src/settings.c src/stats.c src/sim.c
  src/render.c src/gl_loader.c src/models.c
  ${BW_SHADERS_GEN} ${BW_MODEL_GEN})
if(WIN32)
  list(APPEND BW_SOURCES src/platform_win32.c res/win32/theblackwall.rc)
else()
  list(APPEND BW_SOURCES src/platform_linux.c)
endif()

add_executable(TheBlackWall ${BW_SOURCES})
target_include_directories(TheBlackWall PRIVATE src third_party res/win32)
target_link_libraries(TheBlackWall PRIVATE SDL3::SDL3)
target_compile_definitions(TheBlackWall PRIVATE
  BW_VERSION="${PROJECT_VERSION}" $<$<CONFIG:Debug>:BW_DEBUG=1>)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(TheBlackWall PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter)
endif()

if(WIN32)
  # A Windows screensaver is a GUI-subsystem exe with the .scr extension.
  set_target_properties(TheBlackWall PROPERTIES WIN32_EXECUTABLE TRUE SUFFIX ".scr")
  target_link_libraries(TheBlackWall PRIVATE comctl32 comdlg32 iphlpapi opengl32 gdi32 user32 advapi32 shell32)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    # Fully static CRT so the .scr can be dropped anywhere on its own.
    # Stripped, with the standard hardening flags set explicitly: antivirus
    # heuristics score unstripped MinGW binaries and missing DEP/ASLR flags
    # as suspicious.
    target_link_options(TheBlackWall PRIVATE -static -static-libgcc
      $<$<CONFIG:Release>:-s>
      -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)
  endif()
  target_compile_definitions(TheBlackWall PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
else()
  target_link_libraries(TheBlackWall PRIVATE m)
  # Xlib directly, for drawing inside XScreenSaver's window (platform_linux.c).
  find_package(X11 REQUIRED)
  # The GL 1.1 entry points come from libGL, as they come from opengl32 on
  # Windows (see gl_loader.h). libGL is also what SDL loads for GLX, so both
  # dispatch to the same context.
  set(OpenGL_GL_PREFERENCE LEGACY)
  find_package(OpenGL REQUIRED)
  target_link_libraries(TheBlackWall PRIVATE X11::X11 OpenGL::GL)
  set_target_properties(TheBlackWall PROPERTIES OUTPUT_NAME theblackwall)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(TheBlackWall PRIVATE $<$<CONFIG:Release>:-s>)
  endif()
  include(GNUInstallDirs)
  # XScreenSaver looks for hacks in its hack directory and their settings
  # pages in its config directory. Distributions disagree on the first
  # (Debian, Ubuntu, Fedora: libexec/xscreensaver; Arch: lib/xscreensaver);
  # tools/install-linux.sh finds the right one and passes it in.
  set(BW_XSS_HACKDIR "${CMAKE_INSTALL_LIBEXECDIR}/xscreensaver" CACHE PATH "XScreenSaver hack directory")
  set(BW_XSS_CONFDIR "${CMAKE_INSTALL_DATADIR}/xscreensaver/config" CACHE PATH "XScreenSaver config directory")
  install(TARGETS TheBlackWall RUNTIME DESTINATION ${BW_XSS_HACKDIR})
  install(FILES res/linux/theblackwall.xml DESTINATION ${BW_XSS_CONFDIR})
endif()
