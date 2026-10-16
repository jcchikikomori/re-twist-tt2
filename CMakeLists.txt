cmake_minimum_required(VERSION 3.16)
project(tt2
    VERSION 0.5.0
    DESCRIPTION "Text Twist 2 — extracted engine in portable C++17"
    LANGUAGES CXX
)

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
option(TT2_BUILD_TESTS "Build the engine unit tests" ON)
option(TT2_WARN_AS_ERROR "Treat warnings as errors" OFF)
option(TT2_BUILD_UI "Build the SDL3 desktop UI (needs SDL3, SDL3_image, SDL3_mixer)" ON)
# Build the pinned SDL3 releases from source (static) instead of using the
# system's: CI and the AppImage use it, since few distros ship SDL3 yet.
option(TT2_FETCH_SDL3 "Download and build SDL3, SDL3_image and SDL3_mixer" OFF)
# Android (android/, Gradle's externalNativeBuild) builds only the game, as
# libmain.so for SDLActivity: no command-line tools, no host-run tests.
if(ANDROID)
    set(TT2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

# Install layout. The game finds its dictionaries next to itself
# (ui/include/tt2ui/data_paths.hpp): Windows ships a flat folder with
# data/ beside tt2.exe; Unix (and the AppImage) uses the usual prefix tree.
if(WIN32)
    set(TT2_BIN_INSTALL_DIR ".")
    set(TT2_DATA_INSTALL_DIR "data")
    set(TT2_LICENSE_INSTALL_DIR "licenses")
else()
    set(TT2_BIN_INSTALL_DIR "bin")
    set(TT2_DATA_INSTALL_DIR "share/tt2/data")
    set(TT2_LICENSE_INSTALL_DIR "share/licenses/tt2")
endif()

# ----------------------------------------------------------------------------
# Engine (header-only library) + platform shim (compiled once per target)
# ----------------------------------------------------------------------------
add_library(tt2_engine INTERFACE)
target_include_directories(tt2_engine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
)
target_compile_features(tt2_engine INTERFACE cxx_std_17)
target_compile_options(tt2_engine INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic>
)

if(TT2_WARN_AS_ERROR)
    target_compile_options(tt2_engine INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>)
    # GCC 16 (MSYS2 UCRT64) flags the std::vector<std::string> temporary in
    # test_word_tree.cpp with -Wfree-nonheap-object, a false positive: the
    # same test is clean under ASan/UBSan built with GCC 16. Keep it a warning.
    target_compile_options(tt2_engine INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:-Wno-error=free-nonheap-object>)
endif()

# Platform shim — the engine headers declare but do not define
# `tt2::tt2_detail::millis_default()`. Every executable that uses the
# engine must compile one of these sources (one per platform) so the
# symbol resolves at link time.
add_library(tt2_platform STATIC
    engine/src/platform_posix.cpp
    engine/src/platform_win32.cpp
)
target_link_libraries(tt2_platform PUBLIC tt2_engine)
target_compile_features(tt2_platform PUBLIC cxx_std_17)

# ----------------------------------------------------------------------------
# Main binary (the driver / "play the engine" smoke test)
# ----------------------------------------------------------------------------
if(NOT ANDROID)
    add_executable(tt2_engine_app src/main.cpp)
    target_link_libraries(tt2_engine_app PRIVATE tt2_engine tt2_platform)
    set_target_properties(tt2_engine_app PROPERTIES OUTPUT_NAME tt2_engine)

    # ------------------------------------------------------------------------
    # Tools
    # ------------------------------------------------------------------------
    add_executable(tt2_tree tools/tree_cli.cpp)
    target_link_libraries(tt2_tree PRIVATE tt2_engine tt2_platform)
    set_target_properties(tt2_tree PROPERTIES OUTPUT_NAME tt2-tree)

    add_executable(tt2_assets tools/assets_cli.cpp)
    target_link_libraries(tt2_assets PRIVATE tt2_engine tt2_platform)
    set_target_properties(tt2_assets PROPERTIES OUTPUT_NAME tt2-assets)
endif()

# ----------------------------------------------------------------------------
# Desktop UI (SDL3). `tt2_ui_logic` is the header-only scene model used by
# both the app and the tests; `tt2` is the playable binary.
# ----------------------------------------------------------------------------
add_library(tt2_ui_logic INTERFACE)
target_include_directories(tt2_ui_logic INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ui/include>
)
target_link_libraries(tt2_ui_logic INTERFACE tt2_engine)
# The credits dialog shows the project version.
target_compile_definitions(tt2_ui_logic INTERFACE TT2_VERSION="${PROJECT_VERSION}")

if(TT2_BUILD_UI)
    set(TT2_SDL_OK OFF)
    if(TT2_FETCH_SDL3)
        # The releases pinned in docs/design/improve-ui-ux.md (Tech stack),
        # built as static libraries. SDL still loads X11, Wayland, Vulkan,
        # OpenGL and the audio servers at run time, so none of them is
        # linked in. Images use the built-in stb decoders, audio decodes WAV
        # and Ogg Vorbis (stb_vorbis); every other codec stays off. No codec
        # may be loaded at run time: SDL_image would otherwise pick up a
        # system libpng and dlopen it, a file no package carries (the first
        # SDL3 Windows zip lost its JPEG decoder that way).
        enable_language(C)
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)   # extracted files get the extraction time
        endif()
        include(FetchContent)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        if(ANDROID)
            # SDLActivity loads libSDL3.so itself (getLibraries()), and its
            # JNI entry points are called only from Java, so a static SDL3
            # would have them dropped by the linker. SDL_image and SDL_mixer
            # stay static inside libmain.so.
            set(SDL_SHARED ON CACHE BOOL "" FORCE)
            set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        else()
            set(SDL_SHARED OFF CACHE BOOL "" FORCE)
            set(SDL_STATIC ON CACHE BOOL "" FORCE)
        endif()
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
        set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
        foreach(fmt ANI AVIF GIF JXL LBM PCX PNM QOI SVG TGA TIF WEBP XCF XPM XV)
            set(SDLIMAGE_${fmt} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_DEPS_SHARED OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
        set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
        foreach(codec AIFF VOC AU FLAC GME MOD MP3 MIDI OPUS VORBIS_VORBISFILE VORBIS_TREMOR WAVPACK)
            set(SDLMIXER_${codec} OFF CACHE BOOL "" FORCE)
        endforeach()
        # SYSTEM (CMake 3.25+) keeps vendor header warnings out of our builds.
        set(TT2_FETCH_SYSTEM "")
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
            set(TT2_FETCH_SYSTEM SYSTEM)
        endif()
        FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
            URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
            ${TT2_FETCH_SYSTEM})
        FetchContent_Declare(SDL3_image
            URL https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.8/SDL3_image-3.4.8.tar.gz
            URL_HASH SHA256=e8223b424bc7541cf84ffff5cf7e4f24ec3711c60462cb1bd6dc1d7bd7f25477
            ${TT2_FETCH_SYSTEM})
        FetchContent_Declare(SDL3_mixer
            URL https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-3.2.4.tar.gz
            URL_HASH SHA256=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901
            ${TT2_FETCH_SYSTEM})
        FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_mixer)
        set(TT2_SDL_OK ON)
    else()
        find_package(SDL3 3.2 CONFIG QUIET)
        find_package(SDL3_image CONFIG QUIET)
        find_package(SDL3_mixer CONFIG QUIET)
        if(SDL3_FOUND AND SDL3_image_FOUND AND SDL3_mixer_FOUND)
            set(TT2_SDL_OK ON)
        endif()
    endif()
    set(TT2_SDL_LIBS SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer SDL3::SDL3)
    if(TT2_SDL_OK)
        message(STATUS "SDL3 found: building the tt2 UI binary")
        # tinyfiledialogs (zlib, third_party/): the native folder picker for
        # importing the original assets. Its own C target, so the project's
        # warning flags (and -Werror) never apply to vendor code.
        set(TT2_APP_SOURCES ui/src/main.cpp ui/src/audio.cpp ui/src/display.cpp ui/src/render_backend.cpp)
        if(ANDROID)
            # SDLActivity dlopen()s libmain.so and calls its SDL_main. There
            # is no folder dialog on Android (Phase 3 imports from a ZIP).
            add_library(tt2_app SHARED ${TT2_APP_SOURCES} ui/src/folder_picker_android.cpp)
            set_target_properties(tt2_app PROPERTIES OUTPUT_NAME main)
        else()
            enable_language(C)
            add_library(tt2_tinyfd STATIC third_party/tinyfiledialogs/tinyfiledialogs.c)
            target_include_directories(tt2_tinyfd PUBLIC third_party/tinyfiledialogs)
            if(WIN32)
                target_link_libraries(tt2_tinyfd PRIVATE comdlg32 ole32 shell32)
            endif()

            add_executable(tt2_app ${TT2_APP_SOURCES} ui/src/folder_picker.cpp)
            target_link_libraries(tt2_app PRIVATE tt2_tinyfd)
            set_target_properties(tt2_app PROPERTIES OUTPUT_NAME tt2)
        endif()
        # Threads: the optional texture upscaler runs on std::async workers.
        find_package(Threads REQUIRED)
        target_link_libraries(tt2_app PRIVATE tt2_ui_logic tt2_platform ${TT2_SDL_LIBS} Threads::Threads)
        # The source tree's imported-assets/: a developer fallback, read when
        # no per-user import exists (scripts/import-original.sh used to copy there).
        target_compile_definitions(tt2_app PRIVATE
            TT2_IMPORTED_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/imported-assets"
            TT2_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
        if(NOT ANDROID)   # Gradle packages libmain.so into the APK itself
            install(TARGETS tt2_app RUNTIME DESTINATION ${TT2_BIN_INSTALL_DIR})
        endif()
        if(UNIX AND NOT APPLE AND NOT ANDROID)
            install(FILES packaging/linux/tt2.desktop DESTINATION share/applications)
            install(FILES packaging/linux/tt2.svg DESTINATION share/icons/hicolor/scalable/apps)
        endif()
    else()
        message(STATUS "SDL3/SDL3_image/SDL3_mixer not found: skipping the tt2 UI binary "
            "(-DTT2_FETCH_SDL3=ON builds them from source)")
    endif()
endif()

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
if(TT2_BUILD_TESTS)
    enable_testing()

    add_executable(tt2_tests
        engine/tests/test_main.cpp
        engine/tests/test_random.cpp
        engine/tests/test_word_tree.cpp
        engine/tests/test_bag.cpp
        engine/tests/test_board.cpp
        engine/tests/test_scoring.cpp
        engine/tests/test_round_classic.cpp
        engine/tests/test_round_crossword.cpp
        engine/tests/test_profile.cpp
        engine/tests/test_saved_game.cpp
        engine/tests/test_trophies.cpp
        engine/tests/test_round_lightning.cpp
        engine/tests/test_settings.cpp
        engine/tests/test_improvements.cpp
        engine/tests/test_sha256.cpp
        engine/tests/test_asset_manifest.cpp
        engine/tests/test_utf8_path.cpp
    )
    target_link_libraries(tt2_tests PRIVATE tt2_engine tt2_platform)
    # The committed data/ files the tests check (original-assets.sha256).
    target_compile_definitions(tt2_tests PRIVATE TT2_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

    include(CTest)
    add_test(NAME tt2_unit_tests COMMAND tt2_tests)

    # UI logic tests: headless, no SDL needed.
    add_executable(tt2_ui_tests
        engine/tests/test_main.cpp
        ui/tests/test_welcome_flow.cpp
        ui/tests/test_bitmap_font.cpp
        ui/tests/test_profile_dialogs.cpp
        ui/tests/test_game_scene.cpp
        ui/tests/test_help_dialog.cpp
        ui/tests/test_settings_dialog.cpp
        ui/tests/test_data_paths.cpp
        ui/tests/test_dialog_fade.cpp
        ui/tests/test_tooltip.cpp
        ui/tests/test_trophy_room.cpp
        ui/tests/test_highscore_dialog.cpp
        ui/tests/test_blitz_scene.cpp
        ui/tests/test_asset_import.cpp
        ui/tests/test_folder_picker.cpp
        ui/tests/test_import_prompt.cpp
        ui/tests/test_placeholder_assets.cpp
        ui/tests/test_audio_mix.cpp
        ui/tests/test_improvements_dialog.cpp
        ui/tests/test_render_backend.cpp
        ui/tests/test_frame_clock.cpp
        ui/tests/test_display.cpp
        ui/tests/test_side_fill.cpp
        ui/tests/test_render_options.cpp
        ui/tests/test_upscale.cpp
    )
    target_include_directories(tt2_ui_tests PRIVATE engine/tests)
    target_link_libraries(tt2_ui_tests PRIVATE tt2_ui_logic tt2_platform)
    # The generated placeholder art the tests check (data/placeholder/).
    target_compile_definitions(tt2_ui_tests PRIVATE TT2_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME tt2_ui_unit_tests COMMAND tt2_ui_tests)
endif()

# ----------------------------------------------------------------------------
# Install
# ----------------------------------------------------------------------------
if(TARGET tt2_engine_app)
    install(TARGETS tt2_engine_app
        RUNTIME DESTINATION ${TT2_BIN_INSTALL_DIR}
    )
endif()
# Only the open-license dictionaries. data/ may also hold the user's own
# GameHouse common.tree / full.tree (gitignored); those never get packaged.
# original-assets.sha256 is paths and SHA-256 hashes only, no GameHouse
# bytes: the importer checks the player's own install against it.
install(FILES data/open-common.tree data/open-full.tree data/LICENSE.md
    data/original-assets.sha256
    DESTINATION ${TT2_DATA_INSTALL_DIR}
)
# The generated placeholder art (tools/gen_placeholders.py): open fonts and
# drawn shapes, used for any file the player hasn't imported.
install(DIRECTORY data/placeholder DESTINATION ${TT2_DATA_INSTALL_DIR})
# The notices of everything compiled into the binaries (the fonts' and the
# dictionaries' licences travel inside data/). MIT and BSD notices must ship
# with binaries; the zlib ones only with source, but they come along too.
# packages.yml adds the MinGW runtime DLLs' and the AppImage runtime's.
install(FILES LICENSE DESTINATION ${TT2_LICENSE_INSTALL_DIR} RENAME re-twist-tt2.txt)
if(TARGET tt2_app)
    install(FILES third_party/super-xbr/LICENSE DESTINATION ${TT2_LICENSE_INSTALL_DIR} RENAME super-xbr.txt)
    if(TARGET tt2_tinyfd)
        install(FILES third_party/tinyfiledialogs/LICENSE DESTINATION ${TT2_LICENSE_INSTALL_DIR}
            RENAME tinyfiledialogs.txt)
    endif()
    if(TT2_FETCH_SDL3)
        install(FILES ${sdl3_SOURCE_DIR}/LICENSE.txt DESTINATION ${TT2_LICENSE_INSTALL_DIR} RENAME SDL3.txt)
        install(FILES ${sdl3_image_SOURCE_DIR}/LICENSE.txt DESTINATION ${TT2_LICENSE_INSTALL_DIR}
            RENAME SDL3_image.txt)
        install(FILES ${sdl3_mixer_SOURCE_DIR}/LICENSE.txt DESTINATION ${TT2_LICENSE_INSTALL_DIR}
            RENAME SDL3_mixer.txt)
    endif()
endif()
