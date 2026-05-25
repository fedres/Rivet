cmake_minimum_required(VERSION 3.25)

# ─── Fetch GoogleTest ─────────────────────────────────────────────────────────

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# Prevent GTest from overriding compiler/linker settings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ─── Test helper macro ────────────────────────────────────────────────────────

function(rivet_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE rivet_runtime rivet_pal GTest::gtest_main)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
    gtest_discover_tests(${name})
endfunction()

# ─── Unit tests ───────────────────────────────────────────────────────────────

add_subdirectory(unit)

# ─── Platform tests ───────────────────────────────────────────────────────────

add_subdirectory(platform)
