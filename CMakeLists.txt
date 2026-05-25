cmake_minimum_required(VERSION 3.25)

project(rivet
    VERSION 0.1.0
    DESCRIPTION "A self-contained, cross-platform C++ build system and runtime"
    LANGUAGES CXX
)

# ─── Global settings ──────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Platform detection ───────────────────────────────────────────────────────

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RIVET_PLATFORM_LINUX TRUE)
    set(RIVET_PLATFORM_DIR "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(RIVET_PLATFORM_MACOS TRUE)
    set(RIVET_PLATFORM_DIR "macos")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(RIVET_PLATFORM_WINDOWS TRUE)
    set(RIVET_PLATFORM_DIR "windows")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ─── Platform Abstraction Layer ───────────────────────────────────────────────

add_library(rivet_pal STATIC
    platform/${RIVET_PLATFORM_DIR}/fs.cpp
    platform/${RIVET_PLATFORM_DIR}/process.cpp
    platform/${RIVET_PLATFORM_DIR}/env.cpp
    platform/${RIVET_PLATFORM_DIR}/terminal.cpp
    platform/${RIVET_PLATFORM_DIR}/net.cpp
    platform/${RIVET_PLATFORM_DIR}/sandbox.cpp
)

target_include_directories(rivet_pal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(rivet_pal PUBLIC cxx_std_23)

if(RIVET_PLATFORM_WINDOWS)
    target_compile_definitions(rivet_pal PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(rivet_pal PUBLIC ws2_32 crypt32 shlwapi)
endif()

if(RIVET_PLATFORM_MACOS)
    target_link_libraries(rivet_pal PUBLIC "-framework CoreFoundation" "-framework Security")
endif()

# ─── Compiler warnings ────────────────────────────────────────────────────────

if(MSVC)
    target_compile_options(rivet_pal PUBLIC
        /W4 /WX
        /wd4100  # unreferenced formal parameter (stubs have plenty)
        /wd4996  # deprecated POSIX names
    )
else()
    target_compile_options(rivet_pal PUBLIC
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter       # stubs have plenty
        -Wno-missing-field-initializers
    )
endif()

# ─── Runtime core ─────────────────────────────────────────────────────────────
# rivet_runtime: all non-main runtime sources, linked by both the binary and tests.

add_library(rivet_runtime STATIC
    runtime/cli/cli.cpp

    # Build IR layer
    runtime/build/graph.cpp
    runtime/build/scheduler.cpp
    runtime/build/executor.cpp

    # Cache layer
    runtime/cache/key.cpp
    runtime/cache/store.cpp

    # Package management
    runtime/package/manifest.cpp
    runtime/package/lockfile.cpp

    # Toolchain
    runtime/toolchain/discovery.cpp
    runtime/toolchain/compile.cpp

    # Daemon (stub)
    runtime/daemon/daemon.cpp

    # Modules (stub)
    runtime/modules/scanner.cpp
)

target_link_libraries(rivet_runtime PUBLIC rivet_pal)
target_include_directories(rivet_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(rivet runtime/main.cpp)
target_link_libraries(rivet PRIVATE rivet_runtime)

# On macOS: set rpath so the binary can find bundled dylibs after installation.
if(RIVET_PLATFORM_MACOS)
    set_target_properties(rivet PROPERTIES
        INSTALL_RPATH "@executable_path/../lib/rivet"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# On Linux: prefer static linking of C++ runtime for portability.
if(RIVET_PLATFORM_LINUX)
    target_link_options(rivet PRIVATE
        -static-libstdc++
        -static-libgcc
    )
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────

option(RIVET_BUILD_TESTS "Build test suite" ON)

if(RIVET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────

install(TARGETS rivet DESTINATION bin)

# ─── Summary ──────────────────────────────────────────────────────────────────

message(STATUS "Rivet ${PROJECT_VERSION}")
message(STATUS "  Platform:   ${CMAKE_SYSTEM_NAME} (${RIVET_PLATFORM_DIR})")
message(STATUS "  Compiler:   ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:      ${RIVET_BUILD_TESTS}")
