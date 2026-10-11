cmake_minimum_required(VERSION 3.16)
project(tt2
    VERSION 0.1.0
    DESCRIPTION "Text Twist 2 — extracted engine in portable C++17"
    LANGUAGES CXX
)

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
option(TT2_BUILD_TESTS "Build the engine unit tests" ON)
option(TT2_WARN_AS_ERROR "Treat warnings as errors" OFF)

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
    )
    target_link_libraries(tt2_tests PRIVATE tt2_engine tt2_platform)

    include(CTest)
    add_test(NAME tt2_unit_tests COMMAND tt2_tests)
endif()

# ----------------------------------------------------------------------------
# Install (rough sketch; refined when a real UI is added)
# ----------------------------------------------------------------------------
install(TARGETS tt2_engine_app
    RUNTIME DESTINATION bin
)
install(DIRECTORY data/ DESTINATION share/games/texttwist2/data
    OPTIONAL
)
