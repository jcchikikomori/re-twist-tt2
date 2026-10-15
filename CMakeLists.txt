cmake_minimum_required(VERSION 3.16)
project(tt2
    VERSION 0.2.0
    DESCRIPTION "Text Twist 2 — extracted engine in portable C++17"
    LANGUAGES CXX
)

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
option(TT2_BUILD_TESTS "Build the engine unit tests" ON)
option(TT2_WARN_AS_ERROR "Treat warnings as errors" OFF)
option(TT2_BUILD_UI "Build the SDL2 desktop UI (needs SDL2, SDL2_image, SDL2_mixer)" ON)

# Install layout. The game finds its dictionaries next to itself
# (ui/include/tt2ui/data_paths.hpp): Windows ships a flat folder with
# data/ beside tt2.exe; Unix (and the AppImage) uses the usual prefix tree.
if(WIN32)
    set(TT2_BIN_INSTALL_DIR ".")
    set(TT2_DATA_INSTALL_DIR "data")
else()
    set(TT2_BIN_INSTALL_DIR "bin")
    set(TT2_DATA_INSTALL_DIR "share/tt2/data")
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
add_executable(tt2_engine_app src/main.cpp)
target_link_libraries(tt2_engine_app PRIVATE tt2_engine tt2_platform)
set_target_properties(tt2_engine_app PROPERTIES OUTPUT_NAME tt2_engine)

# ----------------------------------------------------------------------------
# Tools
# ----------------------------------------------------------------------------
add_executable(tt2_tree tools/tree_cli.cpp)
target_link_libraries(tt2_tree PRIVATE tt2_engine tt2_platform)
set_target_properties(tt2_tree PROPERTIES OUTPUT_NAME tt2-tree)

add_executable(tt2_assets tools/assets_cli.cpp)
target_link_libraries(tt2_assets PRIVATE tt2_engine tt2_platform)
set_target_properties(tt2_assets PROPERTIES OUTPUT_NAME tt2-assets)

# ----------------------------------------------------------------------------
# Desktop UI (SDL2). `tt2_ui_logic` is the header-only scene model used by
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
    # pkg-config first (works on every distro), then a plain header/library
    # search as a fallback (e.g. when a Homebrew pkg-config shadows the
    # system one and cannot see /usr/lib/*/pkgconfig).
    set(TT2_SDL_OK OFF)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2>=2.0.10)   # SDL_RenderCopyExF
        pkg_check_modules(SDL2_IMAGE QUIET IMPORTED_TARGET SDL2_image)
        pkg_check_modules(SDL2_MIXER QUIET IMPORTED_TARGET SDL2_mixer)
        if(SDL2_FOUND AND SDL2_IMAGE_FOUND AND SDL2_MIXER_FOUND)
            set(TT2_SDL_LIBS PkgConfig::SDL2 PkgConfig::SDL2_IMAGE PkgConfig::SDL2_MIXER)
            set(TT2_SDL_OK ON)
        endif()
    endif()
    if(NOT TT2_SDL_OK)
        # Homebrew/non-distro CMake builds do not know Debian's multiarch
        # library directory, so name it explicitly.
        set(TT2_SDL_LIB_PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
            /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib)
        find_path(TT2_SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 PATHS /usr/include /usr/local/include)
        find_library(TT2_SDL2_LIB SDL2 PATHS ${TT2_SDL_LIB_PATHS})
        find_library(TT2_SDL2_IMAGE_LIB SDL2_image PATHS ${TT2_SDL_LIB_PATHS})
        find_library(TT2_SDL2_MIXER_LIB SDL2_mixer PATHS ${TT2_SDL_LIB_PATHS})
        if(TT2_SDL2_INCLUDE_DIR AND TT2_SDL2_LIB AND TT2_SDL2_IMAGE_LIB AND TT2_SDL2_MIXER_LIB)
            add_library(tt2_sdl2_deps INTERFACE)
            # Note: use the distro compiler (/usr/bin/g++) with the distro
            # SDL2; a Homebrew gcc ships its own glibc headers and will not
            # find Debian's <SDL2/_real_SDL_config.h>.
            target_include_directories(tt2_sdl2_deps INTERFACE ${TT2_SDL2_INCLUDE_DIR})
            target_link_libraries(tt2_sdl2_deps INTERFACE
                ${TT2_SDL2_LIB} ${TT2_SDL2_IMAGE_LIB} ${TT2_SDL2_MIXER_LIB})
            set(TT2_SDL_LIBS tt2_sdl2_deps)
            set(TT2_SDL_OK ON)
        endif()
    endif()
    if(TT2_SDL_OK)
        message(STATUS "SDL2 found: building the tt2 UI binary")
        # tinyfiledialogs (zlib, third_party/): the native folder picker for
        # importing the original assets. Its own C target, so the project's
        # warning flags (and -Werror) never apply to vendor code.
        enable_language(C)
        add_library(tt2_tinyfd STATIC third_party/tinyfiledialogs/tinyfiledialogs.c)
        target_include_directories(tt2_tinyfd PUBLIC third_party/tinyfiledialogs)
        if(WIN32)
            target_link_libraries(tt2_tinyfd PRIVATE comdlg32 ole32 shell32)
        endif()

        add_executable(tt2_app ui/src/main.cpp ui/src/folder_picker.cpp)
        target_link_libraries(tt2_app PRIVATE tt2_ui_logic tt2_platform tt2_tinyfd ${TT2_SDL_LIBS})
        set_target_properties(tt2_app PROPERTIES OUTPUT_NAME tt2)
        # The source tree's imported-assets/: a developer fallback, read when
        # no per-user import exists (scripts/import-original.sh used to copy there).
        target_compile_definitions(tt2_app PRIVATE
            TT2_IMPORTED_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/imported-assets"
            TT2_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
        install(TARGETS tt2_app RUNTIME DESTINATION ${TT2_BIN_INSTALL_DIR})
        if(UNIX AND NOT APPLE)
            install(FILES packaging/linux/tt2.desktop DESTINATION share/applications)
            install(FILES packaging/linux/tt2.svg DESTINATION share/icons/hicolor/scalable/apps)
        endif()
    else()
        message(STATUS "SDL2/SDL2_image/SDL2_mixer not found: skipping the tt2 UI binary")
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
        engine/tests/test_settings.cpp
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
        ui/tests/test_asset_import.cpp
        ui/tests/test_folder_picker.cpp
        ui/tests/test_import_prompt.cpp
        ui/tests/test_placeholder_assets.cpp
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
install(TARGETS tt2_engine_app
    RUNTIME DESTINATION ${TT2_BIN_INSTALL_DIR}
)
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
