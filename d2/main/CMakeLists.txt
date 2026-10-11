if(ANDROID)
    # Android: the game is the libmain.so that SDL's Java activity loads.
    add_library(d2x-redux SHARED)
    set_target_properties(d2x-redux PROPERTIES OUTPUT_NAME main_d2)
else()
    add_executable(d2x-redux)
endif()
target_sources(d2x-redux PRIVATE
    ai.c
    ai2.c
    aipath.c
    automap.c
    bm.c
    cntrlcen.c
    collide.c
    config.c
    console.c
    controls.c
    credits.c
    digiobj.c
    dumpmine.c
    effects.c
    endlevel.c
    escort.c
    fireball.c
    fuelcen.c
    fvi.c
    game.c
    gamecntl.c
    gamefont.c
    gamemine.c
    gamepal.c
    gamerend.c
    gamesave.c
    gameseg.c
    gameseq.c
    gauges.c
    hostage.c
    hud.c
    inferno.c
    kconfig.c
    kmatrix.c
    laser.c
    lighting.c
    menu.c
    mglobal.c
    mission.c
    morph.c
    movie.c
    multi.c
    multibot.c
    newdemo.c
    newmenu.c
    object.c
    paging.c
    physics.c
    piggy.c
    player.c
    playsave.c
    polyobj.c
    powerup.c
    render.c
    robot.c
    scores.c
    segment.c
    slew.c
    songs.c
    state.c
    switch.c
    terrain.c
    texmerge.c
    text.c
    titles.c
    vclip.c
    wall.c
    weapon.c
    )

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/arch/include)

target_compile_definitions(d2x-redux PUBLIC
    DXX_VERSION_MAJORi=${PROJECT_VERSION_MAJOR}
    DXX_VERSION_MINORi=${PROJECT_VERSION_MINOR}
    DXX_VERSION_MICROi=$<IF:$<BOOL:${PROJECT_VERSION_MICRO}>,${PROJECT_VERSION_MICRO},0>
    )

include(CheckTypeSize)
check_type_size("ssize_t" SIZEOF_SSIZE_T LANGUAGE C)
if(NOT "${SIZEOF_SSIZE_T}" STREQUAL "")
    target_compile_definitions(d2x-redux PUBLIC HAVE_SSIZE_T=1)
endif()

if(OPENGL)
    if(ANDROID)
        target_link_libraries(d2x-redux PRIVATE arch_ogl)
    else()
        target_link_libraries(d2x-redux PRIVATE arch_ogl xmodel)
    endif()
endif()

if(SDLMIXER)
    target_link_libraries(d2x-redux PUBLIC ${SDL_MIXER_LIBRARIES})
endif()

if(EDITOR)
    target_sources(d2x-redux PRIVATE bmread.c)
    target_link_libraries(d2x-redux PRIVATE editor ui)
endif()

if(UDP)
    target_sources(d2x-redux PRIVATE net_udp.c)
endif()

if(WIN32)
    target_link_libraries(d2x-redux PUBLIC glu32 winmm ws2_32 dbghelp)
    target_link_libraries(d2x-redux PRIVATE arch_win32)
    if(VR)
        # A Windows program, not a console one: no console window opens with
        # the game or stays after it.  The entry point is our
        # own (vr/vr_winmain.c): SDLmain's WinMain aborted at once.
        set_target_properties(d2x-redux PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    target_sources(d2x-redux PRIVATE
        ${PROJECT_SOURCE_DIR}/arch/win32/d2x-rebirth.ico
        ${PROJECT_SOURCE_DIR}/arch/win32/d2x-rebirth.rc
        )
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:msvcrt.lib")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # The manifest has to be included through the .rc for compatibility with MSYS. However
        # MSVC will automatically generate a manifest using this method - which will cause a
        # collision - so we need to tell it not to.
        target_link_options(d2x-redux PRIVATE "/MANIFEST:NO")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            # Enable Hot Reload for debug builds
            target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>)
            target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/ZI>)
        endif()
    endif()

    install(FILES
        ${PROJECT_BINARY_DIR}/main/SDL.dll
        ${PROJECT_BINARY_DIR}/main/physfs.dll
        DESTINATION .)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        install(FILES ${PROJECT_BINARY_DIR}/main/zlibd1.dll DESTINATION .)
    else()
        install(FILES ${PROJECT_BINARY_DIR}/main/zlib1.dll DESTINATION .)
    endif()
    if(SDLMIXER)
        install(FILES ${PROJECT_BINARY_DIR}/main/SDL_mixer.dll DESTINATION .)
    endif()
    if(OPENGL)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            install(FILES ${PROJECT_BINARY_DIR}/main/glew32d.dll DESTINATION .)
        else()
            install(FILES ${PROJECT_BINARY_DIR}/main/glew32.dll DESTINATION .)
        endif()
    endif()
    if(PNG)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            install(FILES ${PROJECT_BINARY_DIR}/main/libpng16d.dll DESTINATION .)
        else()
            install(FILES ${PROJECT_BINARY_DIR}/main/libpng16.dll DESTINATION .)
        endif()
    endif()
endif()

if(APPLE)
    target_link_libraries(d2x-redux PRIVATE arch_cocoa)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(d2x-redux PRIVATE arch_x11)
    list(APPEND EXTRA_LIBRARIES "X11")
endif()

if(NOT WIN32)
    list(APPEND EXTRA_LIBRARIES "m")
endif()

if(ANDROID)
    # SDL (sdl12-compat) and PhysFS come from platform/quest/jni; GL is the
    # system's OpenGL ES 1.x.
    set(OPENGL_LIBRARY GLESv1_CM)
    list(APPEND EXTRA_LIBRARIES EGL log android dl)
else()
find_package(SDL 1.2 REQUIRED)
find_package(PhysFS REQUIRED)
if(OPENGL)
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    list(APPEND EXTRA_LIBRARIES GLEW::GLEW)
endif()
endif()
if(PNG)
    find_package(PNG REQUIRED)
endif()
target_include_directories(d2x-redux PUBLIC ${SDL_INCLUDE_DIR} ${PHYSFS_INCLUDE_DIR})
target_link_libraries(d2x-redux PRIVATE 2d 3d arch_sdl iff libmve maths mem misc texmap)
target_link_libraries(d2x-redux PUBLIC ${OPENGL_LIBRARY} ${SDL_LIBRARY} ${SDL_MIXER_LIBRARY} ${PHYSFS_LIBRARY} ${PNG_LIBRARY} ${EXTRA_LIBRARIES})
target_sources(d2x-redux PRIVATE vers_id.c)
if(VR AND ANDROID)
    # Standalone headsets: the Android layer and the Khronos Android loader
    # (OPENXR_ANDROID_* come from platform/quest/jni).
    target_sources(d2x-redux PRIVATE ${PROJECT_SOURCE_DIR}/../vr/vr_descent.c ${PROJECT_SOURCE_DIR}/../vr/vr_options.c ${PROJECT_SOURCE_DIR}/../vr/vr_cockpit.c)
    target_include_directories(d2x-redux PRIVATE ${PROJECT_SOURCE_DIR}/../vr ${PROJECT_SOURCE_DIR}/main)
    # GameOrDieXR for Android: built from its source where that is present;
    # the published source links the prebuilt one in vr/lib/android.
    if(EXISTS ${PROJECT_SOURCE_DIR}/../vr/vr_xr_android.c)
        # The Game Tome book too (gt_book.c as on PC, on GLES 1.1 and POSIX
        # through vr/android_compat, which only this library sees).
        add_library(GameOrDieXR SHARED ${PROJECT_SOURCE_DIR}/../vr/vr_xr_android.c ${PROJECT_SOURCE_DIR}/../vr/vr_handmap.c
            ${PROJECT_SOURCE_DIR}/../vr/vr_book_android.c ${PROJECT_SOURCE_DIR}/../vr/gt_book.c ${PROJECT_SOURCE_DIR}/../vr/android_compat/gt_gles1.c)
        target_compile_definitions(GameOrDieXR PRIVATE VRXR_BUILD GTB_GL_HEADER="gt_gles1.h")
        target_include_directories(GameOrDieXR PRIVATE ${PROJECT_SOURCE_DIR}/../vr ${OPENXR_ANDROID_INCLUDE} ${PROJECT_SOURCE_DIR}/../vr/android_compat)
        target_link_libraries(GameOrDieXR PRIVATE ${OPENXR_ANDROID_LOADER} EGL GLESv3 GLESv1_CM OpenSLES log dl)
    else()
        add_library(GameOrDieXR SHARED IMPORTED GLOBAL)
        set_target_properties(GameOrDieXR PROPERTIES
            IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/../vr/lib/android/${ANDROID_ABI}/libGameOrDieXR.so)
    endif()
    target_link_libraries(d2x-redux PRIVATE GameOrDieXR)
elseif(VR)
    # Our OpenXR layer (vr/, shared by both games) and Descent 2's side of it.
    find_package(OpenXR CONFIG REQUIRED)
    target_sources(d2x-redux PRIVATE ${PROJECT_SOURCE_DIR}/../vr/vr_descent.c ${PROJECT_SOURCE_DIR}/../vr/vr_options.c ${PROJECT_SOURCE_DIR}/../vr/vr_cockpit.c ${PROJECT_SOURCE_DIR}/../vr/vr_winmain.c)
    target_include_directories(d2x-redux PRIVATE ${PROJECT_SOURCE_DIR}/../vr ${PROJECT_SOURCE_DIR}/main)	# the shared glue reads this game's headers
    # GameOrDieXR: Game Or Die's own closed VR library (the OpenXR layer, the
    # steady view, the hand mapping), a DLL beside the game.  Built from its
    # source where that is present; the published source links the prebuilt
    # one in vr/lib (it ships in every release).
    if(EXISTS ${PROJECT_SOURCE_DIR}/../vr/vr_xr.c)
        add_library(GameOrDieXR SHARED ${PROJECT_SOURCE_DIR}/../vr/vr_xr.c ${PROJECT_SOURCE_DIR}/../vr/vr_handmap.c ${PROJECT_SOURCE_DIR}/../vr/vr_book.c ${PROJECT_SOURCE_DIR}/../vr/gt_book.c)
        target_compile_definitions(GameOrDieXR PRIVATE VRXR_BUILD)
        target_include_directories(GameOrDieXR PRIVATE ${PROJECT_SOURCE_DIR}/../vr)
        target_link_libraries(GameOrDieXR PRIVATE OpenXR::openxr_loader GLEW::GLEW ${OPENGL_LIBRARY} winmm)
    else()
        add_library(GameOrDieXR SHARED IMPORTED)
        set_target_properties(GameOrDieXR PROPERTIES
            IMPORTED_IMPLIB ${PROJECT_SOURCE_DIR}/../vr/lib/GameOrDieXR.lib
            IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/../vr/lib/GameOrDieXR.dll)
        file(COPY ${PROJECT_SOURCE_DIR}/../vr/lib/GameOrDieXR.dll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    target_link_libraries(d2x-redux PRIVATE GameOrDieXR)
endif()
install(TARGETS d2x-redux RUNTIME DESTINATION .)
