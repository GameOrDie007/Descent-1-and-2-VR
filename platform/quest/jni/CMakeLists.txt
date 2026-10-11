# Descent 1 & 2 VR for Quest and Steam Frame (standalone): the native part.
#
# Builds SDL2, sdl12-compat (the SDL 1.2 API the engine is written against,
# running on SDL2) and PhysFS from their release sources, then the engine
# itself as libmain.so, the library SDL's Android activity loads.
#
#   EXT_SRC   folder holding SDL2/, sdl12-compat/ (patched with ../patches/sdl12-compat-android.patch), physfs/ (../patches/physfs-android-group-access.patch)
#             and physfs/ release sources
#   GAME      d1 or d2

cmake_minimum_required(VERSION 3.18)
project(descent_vr_android C)

set(EXT_SRC "E:/Tools/Android/src" CACHE PATH "Third-party release sources")
set(GAME "d1" CACHE STRING "d1 or d2")
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# --- SDL2 ------------------------------------------------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)	# PhysFS has its own
add_subdirectory("${EXT_SRC}/SDL2" SDL2)

# --- sdl12-compat (loads libSDL2.so at run time) ----------------------------
set(SDL2_INCLUDE_DIRS "${EXT_SRC}/SDL2/include")
# its SDL2 finder runs too: hand it the headers (it only needs them, the
# library itself is opened at run time)
set(SDL2_INCLUDE_DIR "${EXT_SRC}/SDL2/include" CACHE PATH "" FORCE)
set(SDL2_LIBRARY SDL2 CACHE STRING "" FORCE)
set(SDL12TESTS OFF CACHE BOOL "" FORCE)
set(SDL12DEVEL OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXT_SRC}/sdl12-compat" sdl12-compat)

# --- PhysFS, static ----------------------------------------------------------
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXT_SRC}/physfs" physfs)

# --- the engine ----------------------------------------------------------------
# What the engine's own CMake finds on the desktop, handed over directly.
set(SDL_INCLUDE_DIR "${EXT_SRC}/sdl12-compat/include/SDL")
set(SDL_LIBRARY SDL)
set(PHYSFS_INCLUDE_DIR "${EXT_SRC}/physfs/src")
set(PHYSFS_LIBRARY physfs-static)
include_directories(${SDL_INCLUDE_DIR} ${PHYSFS_INCLUDE_DIR})

# First milestone: the flat game on the headset.  OpenGL ES 1.x through the
# engine's own GLES path; no sound mixer (plain SDL audio), no PNG.  VR
# through vr/vr_xr_android.c.
set(OPENGL ON CACHE BOOL "" FORCE)
set(OPENGLMERGE OFF CACHE BOOL "" FORCE)
set(SDLMIXER OFF CACHE BOOL "" FORCE)
set(PNG OFF CACHE BOOL "" FORCE)
set(UDP ON CACHE BOOL "" FORCE)	# the engine assumes it; Android has sockets
set(TRACKER ON CACHE BOOL "" FORCE)	# net_udp.c assumes it too
set(EDITOR OFF CACHE BOOL "" FORCE)
set(VR ON CACHE BOOL "" FORCE)
# The Khronos OpenXR loader for Android (from its AAR) and the headers.
set(OPENXR_ANDROID "${EXT_SRC}/openxr")
set(OPENXR_ANDROID_INCLUDE "${OPENXR_ANDROID}/include")
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION "${OPENXR_ANDROID}/aar/jni/${ANDROID_ABI}/libopenxr_loader.so")
set(OPENXR_ANDROID_LOADER openxr_loader)
add_definitions(-DOGLES -D__LINUX__ -DHAVE_STRUCT_TIMESPEC -DHAVE_STRUCT_TIMEVAL)
# PNG replacements for the game's pictures (the HD cockpit gauges), read by png_stb.c
add_definitions(-DHAVE_LIBPNG)
add_subdirectory("${REPO}/${GAME}" ${GAME})

if(GAME STREQUAL "d1")
    set(EXE d1x-redux)
else()
    set(EXE d2x-redux)
endif()
target_sources(${EXE} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/xmodel_none.c" "${CMAKE_CURRENT_LIST_DIR}/messagebox_android.c" "${CMAKE_CURRENT_LIST_DIR}/png_stb.c" "${CMAKE_CURRENT_LIST_DIR}/music_android.c")
target_include_directories(${EXE} PRIVATE "${REPO}/${GAME}/include" "${REPO}/${GAME}/arch/include" "${EXT_SRC}/TinySoundFont" "${EXT_SRC}/stb")

# --- the game picker: SDL2 and GameOrDieXR, no engine --------------------------
# Built with either game (the same library); the app ships one.
# Game Or Die's own, not in the published source.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/picker.c")
    add_library(picker SHARED "${CMAKE_CURRENT_LIST_DIR}/picker.c")
    target_include_directories(picker BEFORE PRIVATE "${EXT_SRC}/SDL2/include" "${REPO}/vr" "${EXT_SRC}/stb")
    target_link_libraries(picker SDL2 GameOrDieXR GLESv1_CM log)
endif()
