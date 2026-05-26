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

# Derive canonical target triple for embedding in the binary.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(_RIVET_ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(_RIVET_ARCH "arm64")
else()
    set(_RIVET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(RIVET_PLATFORM_LINUX)
    set(RIVET_TARGET_TRIPLE "${_RIVET_ARCH}-linux-gnu")
elseif(RIVET_PLATFORM_MACOS)
    set(RIVET_TARGET_TRIPLE "${_RIVET_ARCH}-apple-macos")
elseif(RIVET_PLATFORM_WINDOWS)
    set(RIVET_TARGET_TRIPLE "${_RIVET_ARCH}-windows-msvc")
else()
    set(RIVET_TARGET_TRIPLE "${_RIVET_ARCH}-unknown")
endif()

# Git hash (optional — CI sets -DRIVET_GIT_HASH=<hash>; local builds leave it empty).
if(NOT DEFINED RIVET_GIT_HASH)
    set(RIVET_GIT_HASH "")
endif()

# Generate version header.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/runtime/version.hpp
    @ONLY
)

# ─── Optional vendored dependencies ──────────────────────────────────────────
#
# store.cpp uses SQLite via vendor/sqlite/sqlite3.h (guarded by __has_include).
# manifest.cpp uses toml++ via vendor/tomlplusplus/toml.hpp (same guard).
# Pass -DRIVET_VENDOR_FETCH=ON to cmake to auto-download both on first configure.

option(RIVET_VENDOR_FETCH "Download vendored SQLite3 + toml++ if absent" OFF)

if(RIVET_VENDOR_FETCH)
    include(FetchContent)
    cmake_policy(SET CMP0135 NEW)

    # ── toml++ v3.4.0 (header-only) ─────────────────────────────────────────────
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tomlplusplus/toml.hpp")
        FetchContent_Declare(tomlplusplus
            GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
            GIT_TAG        v3.4.0
            GIT_SHALLOW    TRUE
        )
        FetchContent_Populate(tomlplusplus)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tomlplusplus")
        file(COPY "${tomlplusplus_SOURCE_DIR}/toml.hpp"
             DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tomlplusplus")
        message(STATUS "Vendored toml++ v3.4.0 → vendor/tomlplusplus/toml.hpp")
    endif()

    # ── SQLite 3.45.2 amalgamation ───────────────────────────────────────────────
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.h")
        FetchContent_Declare(sqlite3_amal
            URL https://www.sqlite.org/2024/sqlite-amalgamation-3450200.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_Populate(sqlite3_amal)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite")
        file(COPY
            "${sqlite3_amal_SOURCE_DIR}/sqlite3.h"
            "${sqlite3_amal_SOURCE_DIR}/sqlite3.c"
            DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite")
        message(STATUS "Vendored SQLite 3.45.2 → vendor/sqlite/")
    endif()
endif()

# Compile the SQLite amalgamation when it is present in the vendor directory.
# The __has_include guard in store.cpp will automatically pick up the header.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.c")
    set(_RIVET_HAVE_SQLITE TRUE)
else()
    set(_RIVET_HAVE_SQLITE FALSE)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/common
)

target_compile_features(rivet_pal PUBLIC cxx_std_23)

if(RIVET_PLATFORM_WINDOWS)
    target_compile_definitions(rivet_pal PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(rivet_pal PUBLIC ws2_32 crypt32 shlwapi bcrypt)
endif()

if(RIVET_PLATFORM_MACOS)
    target_link_libraries(rivet_pal PUBLIC "-framework CoreFoundation" "-framework Security")
endif()

if(RIVET_PLATFORM_LINUX)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(rivet_pal PUBLIC OpenSSL::SSL OpenSSL::Crypto)
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
target_include_directories(rivet_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}   # for generated runtime/version.hpp
)

if(_RIVET_HAVE_SQLITE)
    target_sources(rivet_runtime PRIVATE vendor/sqlite/sqlite3.c)
    target_compile_definitions(rivet_runtime PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION=1
    )
    if(NOT MSVC)
        set_source_files_properties(vendor/sqlite/sqlite3.c PROPERTIES
            COMPILE_FLAGS "-Wno-all -Wno-extra -Wno-pedantic")
    endif()
endif()

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

install(TARGETS rivet
    RUNTIME DESTINATION bin
)

# Strip release binaries.
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    install(CODE "
        if(CMAKE_STRIP)
            execute_process(COMMAND \${CMAKE_STRIP}
                \"\${CMAKE_INSTALL_PREFIX}/bin/rivet\")
        endif()
    ")
endif()

# ─── Summary ──────────────────────────────────────────────────────────────────

message(STATUS "Rivet ${PROJECT_VERSION}")
message(STATUS "  Platform:   ${CMAKE_SYSTEM_NAME} (${RIVET_PLATFORM_DIR})")
message(STATUS "  Compiler:   ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:      ${RIVET_BUILD_TESTS}")
