# Crystal Caves Episode 1 - native desktop build (Windows/macOS/Linux).
#
# The freestanding game core (../wasm/{render,game,text,shell}/*.c and
# ../wasm/sound.c, the same source lists as the wasm Makefile) is compiled as
# a static library with -DCC_HOST_LIBC (it then uses the platform libc for
# memset/memcpy instead of its own freestanding copies in render/runtime.c).
# The portable assets from ../out/portable are embedded into the executable
# by ../tools/embed_assets.py, so the result is a single self-contained binary.
#
#   cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build native/build
#   native/build/caves            # the game (SDL3)
#   native/build/cc_digest ...    # golden-master digest harness (no SDL)
#
# SDL3 is found via find_package (Homebrew/apt/vcpkg); otherwise it is built
# from source with FetchContent.
cmake_minimum_required(VERSION 3.20)
project(crystal_caves_native C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(WASM ${REPO}/wasm)
set(PORTABLE ${REPO}/out/portable)

# ------------------------------------------------------------ game core
# Same wildcard source lists as wasm/Makefile (SRC), so the native build
# tracks the wasm build's file set automatically.
file(GLOB CORE_SRC CONFIGURE_DEPENDS
  ${WASM}/render/*.c ${WASM}/game/*.c ${WASM}/text/*.c ${WASM}/shell/*.c
  ${WASM}/sound.c)

# Embedded assets (generated from out/portable at build time).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(EMBED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(EMBED_C ${EMBED_DIR}/assets_embed.c)
set(EMBED_H ${EMBED_DIR}/assets_embed.h)
file(MAKE_DIRECTORY ${EMBED_DIR})
set(PORTABLE_DEPS)
foreach(f gfx.bin levels.bin cells.bin level_overlay.bin screens.bin
          mni_f1.bin mni_f2.bin mni_spl.bin palette.json sound.json)
  list(APPEND PORTABLE_DEPS ${PORTABLE}/${f})
endforeach()
add_custom_command(
  OUTPUT ${EMBED_C} ${EMBED_H}
  COMMAND ${Python3_EXECUTABLE} ${REPO}/tools/embed_assets.py ${EMBED_C} ${EMBED_H}
  DEPENDS ${PORTABLE_DEPS} ${REPO}/tools/embed_assets.py
  COMMENT "Embedding portable assets into assets_embed.c")

add_library(ccore STATIC ${CORE_SRC} ${EMBED_C})
target_include_directories(ccore PUBLIC
  ${WASM} ${WASM}/render ${WASM}/game ${WASM}/text ${WASM}/shell ${EMBED_DIR})
target_compile_definitions(ccore PRIVATE CC_HOST_LIBC)
target_compile_options(ccore PRIVATE -ffreestanding -fno-builtin -O2)
if(MSVC)
  target_compile_options(ccore PRIVATE /W3)
else()
  target_compile_options(ccore PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-unused-function)
endif()

# ------------------------------------------------------------------ SDL3
find_package(SDL3 QUIET CONFIG)
if(NOT SDL3_FOUND)
  message(STATUS "SDL3 not found via find_package; building from source (FetchContent)")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.16
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

# -------------------------------------------------------------- targets
add_executable(caves host.c keymap.c persist.c synth.c)
target_link_libraries(caves PRIVATE ccore SDL3::SDL3)

add_executable(cc_digest digest.c sha256.c)
target_link_libraries(cc_digest PRIVATE ccore)

# ------------------------------------------------------------- Episode 2
# The same core compiled against the generated Episode 2 data headers and
# Episode 2 embedded assets.  Skipped when the data has not
# been generated (make -C wasm ep2 && make -C wasm ep2-assets).
set(PORTABLE_EP2 ${REPO}/out/portable_ep2)
set(EP2_DATA ${WASM}/ep2_data)
if(EXISTS ${EP2_DATA}/startup_data.h AND EXISTS ${EP2_DATA}/objects_data.h
   AND EXISTS ${EP2_DATA}/cells_data.h AND EXISTS ${EP2_DATA}/episode_data.h
   AND EXISTS ${PORTABLE_EP2}/levels.bin AND EXISTS ${PORTABLE_EP2}/screens.bin)
  set(EMBED2_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_ep2)
  set(EMBED2_C ${EMBED2_DIR}/assets_embed.c)
  set(EMBED2_H ${EMBED2_DIR}/assets_embed.h)
  file(MAKE_DIRECTORY ${EMBED2_DIR})
  set(EP2_PORTABLE_DEPS)
  foreach(f gfx.bin levels.bin cells.bin level_overlay.bin screens.bin
            mni_f1.bin mni_f2.bin mni_spl.bin palette.json sound.json)
    list(APPEND EP2_PORTABLE_DEPS ${PORTABLE_EP2}/${f})
  endforeach()
  add_custom_command(
    OUTPUT ${EMBED2_C} ${EMBED2_H}
    COMMAND ${Python3_EXECUTABLE} ${REPO}/tools/embed_assets.py
            ${EMBED2_C} ${EMBED2_H} ${PORTABLE_EP2}
    DEPENDS ${EP2_PORTABLE_DEPS} ${REPO}/tools/embed_assets.py
    COMMENT "Embedding Episode 2 portable assets")
  add_library(ccore_ep2 STATIC ${CORE_SRC} ${EMBED2_C})
  target_include_directories(ccore_ep2 PUBLIC
    ${EP2_DATA} ${WASM} ${WASM}/render ${WASM}/game ${WASM}/text
    ${WASM}/shell ${EMBED2_DIR})
  target_compile_definitions(ccore_ep2 PRIVATE CC_HOST_LIBC CC_EPISODE=2)
  target_compile_options(ccore_ep2 PRIVATE -ffreestanding -fno-builtin -O2)
  if(MSVC)
    target_compile_options(ccore_ep2 PRIVATE /W3)
  else()
    target_compile_options(ccore_ep2 PRIVATE -Wall -Wextra
      -Wno-unused-parameter -Wno-unused-function)
  endif()
  add_executable(caves_ep2 host.c keymap.c persist.c synth.c)
  target_compile_definitions(caves_ep2 PRIVATE CC_EPISODE=2)
  target_link_libraries(caves_ep2 PRIVATE ccore_ep2 SDL3::SDL3)
else()
  message(STATUS "Episode 2 data not found; caves_ep2 target skipped")
endif()
