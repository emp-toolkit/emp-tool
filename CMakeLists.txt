cmake_minimum_required(VERSION 3.25)
project(emp-tool VERSION 1.0.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. Use a separate build directory:\n"
        "  cmake -B build && cmake --build build")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# emp-tool's runtime and SIMD layers are intentionally POSIX-only today. Fail
# during configuration instead of reaching obscure socket/intrinsic errors.
if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
    message(FATAL_ERROR
        "emp-tool supports Linux and macOS only (got ${CMAKE_SYSTEM_NAME})")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64)$")
    message(FATAL_ERROR
        "emp-tool supports x86-64 and arm64 only (got ${CMAKE_SYSTEM_PROCESSOR})")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR
        "emp-tool supports GCC, Clang, and AppleClang (got ${CMAKE_CXX_COMPILER_ID})")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    message(FATAL_ERROR "emp-tool requires GCC 10 or newer")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
    message(FATAL_ERROR "emp-tool requires Clang 12 or newer")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR "emp-tool requires AppleClang 14 or newer")
endif()

# ---------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------
option(EMP_TOOL_BUILD_TESTS       "Build emp-tool tests"               ${PROJECT_IS_TOP_LEVEL})
option(EMP_TOOL_BUILD_BENCHMARKS  "Build emp-tool benchmarks"          OFF)
option(EMP_TOOL_BUILD_TOOLS       "Build emp-tool command-line tools"   ${PROJECT_IS_TOP_LEVEL})
option(EMP_TOOL_INSTALL           "Generate install / export rules"    ${PROJECT_IS_TOP_LEVEL})
option(EMP_TOOL_NATIVE_ARCH       "Build for the host CPU (-march=native/-mcpu=native). OFF produces a portable baseline binary." ON)

# Hash backend for emp::Hash (runtime/crypto/hash.h). sha256 = OpenSSL (default);
# blake3 = vendored BLAKE3 (third_party/blake3), ~3-4x faster per byte. Selecting
# blake3 changes the Fiat-Shamir / commitment transcript, so it is a protocol-wide
# choice inherited by every consumer that links this emp-tool build.
set(EMP_HASH_BACKEND "sha256" CACHE STRING "Default emp::Hash backend: sha256 or blake3")
set_property(CACHE EMP_HASH_BACKEND PROPERTY STRINGS sha256 blake3)
if(NOT EMP_HASH_BACKEND MATCHES "^(sha256|blake3)$")
    message(FATAL_ERROR
        "EMP_HASH_BACKEND must be sha256 or blake3 (got '${EMP_HASH_BACKEND}')")
endif()
option(EMP_WITH_BLAKE3 "Build the vendored BLAKE3 backend so HashT<HashOption::blake3> is available (auto-on when EMP_HASH_BACKEND=blake3 or EMP_FS_HASH=blake3)." OFF)

# Fiat-Shamir transcript hash (io_channel.h enable_fs default), selectable
# independently of EMP_HASH_BACKEND: "default" follows it; "sha256"/"blake3"
# pin the FS transcript while commitments/RO/garbling keep using emp::Hash.
# Propagates PUBLIC to every consumer — an AGREEMENT-class
# choice: both parties' stacks must be built with the same value.
set(EMP_FS_HASH "default" CACHE STRING
    "Fiat-Shamir transcript hash: default (= EMP_HASH_BACKEND), sha256, or blake3")
set_property(CACHE EMP_FS_HASH PROPERTY STRINGS default sha256 blake3)
if(NOT EMP_FS_HASH MATCHES "^(default|sha256|blake3)$")
    message(FATAL_ERROR "EMP_FS_HASH must be default, sha256, or blake3 (got '${EMP_FS_HASH}')")
endif()

# ---------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------
find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------
# Source list
# ---------------------------------------------------------------------
set(emp_tool_sources
    emp-tool/ir/assets.cpp
    emp-tool/ir/builtins.cpp
    emp-tool/runtime/crypto/ec.cpp)

# ---------------------------------------------------------------------
# Default search roots for shipped circuit assets. ir/assets.cpp owns the actual
# resolution policy: EMP_CIRCUIT_DIR override, then build-tree assets, then the
# install-tree location beside the installed public headers.
# ---------------------------------------------------------------------
set(EMP_CIRCUIT_ASSET_DIR_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/emp-tool/ir/files")
set(EMP_CIRCUIT_ASSET_DIR_INSTALL "${CMAKE_INSTALL_FULL_INCLUDEDIR}/emp-tool/ir/files")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/circuit_asset_dirs.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/emp-tool/ir/circuit_asset_dirs.h
    @ONLY)

# ---------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------
add_library(emp-tool ${emp_tool_sources})
add_library(emp-tool::emp-tool ALIAS emp-tool)

set_target_properties(emp-tool PROPERTIES
    VERSION        ${PROJECT_VERSION}
    SOVERSION      ${PROJECT_VERSION_MAJOR}
    CXX_EXTENSIONS OFF)

# The canonical BooleanContext surface (ir/context/context.h, circuits/typed.h)
# requires C++20 (concepts, std::regular, std::span), so emp-tool's public
# contract is C++20 — consumers that include those headers must build as C++20.
target_compile_features(emp-tool PUBLIC cxx_std_20)

target_include_directories(emp-tool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(emp-tool PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads)

# -Werror=switch backs the op-dispatch contract (ir/visit.h): every switch over
# circuit::Op is defaultless, so a new Op fails the build at each dispatch site.
target_compile_options(emp-tool PRIVATE
    -Wall -Wno-unused-result -Werror=switch
    $<$<CONFIG:Release>:-O3 -funroll-loops>
    $<$<CONFIG:Debug>:-O0 -g>)

# Suppress -Wignored-attributes globally (lib + consumers). Triggered
# by every template instantiated on `block` (== __m128i carries vector
# / alignment / may_alias attributes that template type identity drops).
# Benign here — we never rely on those attributes flowing through the
# template argument — but spammy for downstream test / consumer builds
# unless made PUBLIC.
target_compile_options(emp-tool PUBLIC -Wno-ignored-attributes)

# Architecture floor. SIMD tiers (VAES / VPCLMULQDQ / AVX-512 etc.) are
# selected at compile time from compiler feature macros; source-level
# target(...) attributes only make those selected implementations legal to
# compile. Set EMP_TOOL_NATIVE_ARCH to bake the host CPU's ISA into the build
# (-march=native on x86, -mcpu=native on arm64) for benchmarks or
# build-on-target deployments; do NOT ship those binaries to older CPUs,
# since they may SIGILL.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
    if(EMP_TOOL_NATIVE_ARCH)
        target_compile_options(emp-tool PUBLIC -mcpu=native)
    else()
        target_compile_options(emp-tool PUBLIC -march=armv8-a+crypto+crc)
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
    if(EMP_TOOL_NATIVE_ARCH)
        target_compile_options(emp-tool PUBLIC -march=native)
    else()
        target_compile_options(emp-tool PUBLIC -maes -mpclmul -mssse3 -msse4.2)
    endif()
endif()

# ---------------------------------------------------------------------
# Optional BLAKE3 hash backend (vendored, third_party/blake3). Compiled straight
# into libemp-tool with per-file ISA flags, so the objects plus the PUBLIC
# include dir + EMP_WITH_BLAKE3 define propagate through emp-tool's exported
# target — consumers inherit BLAKE3 with no extra dependency to resolve.
# Vendored C is built with -w (its warnings are not ours to fix).
# ---------------------------------------------------------------------
if(EMP_HASH_BACKEND STREQUAL "blake3" OR EMP_FS_HASH STREQUAL "blake3")
    set(EMP_WITH_BLAKE3 ON)
endif()
if(EMP_WITH_BLAKE3)
    set(_b3 ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blake3)
    set(_b3_core ${_b3}/blake3.c ${_b3}/blake3_dispatch.c ${_b3}/blake3_portable.c)
    target_sources(emp-tool PRIVATE ${_b3_core})
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i.86)")
        target_sources(emp-tool PRIVATE
            ${_b3}/blake3_sse2.c ${_b3}/blake3_sse41.c ${_b3}/blake3_avx2.c ${_b3}/blake3_avx512.c)
        set_source_files_properties(${_b3}/blake3_sse2.c   PROPERTIES COMPILE_OPTIONS "-msse2;-w")
        set_source_files_properties(${_b3}/blake3_sse41.c  PROPERTIES COMPILE_OPTIONS "-msse4.1;-w")
        set_source_files_properties(${_b3}/blake3_avx2.c   PROPERTIES COMPILE_OPTIONS "-mavx2;-w")
        set_source_files_properties(${_b3}/blake3_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512vl;-w")
        set_source_files_properties(${_b3_core}            PROPERTIES COMPILE_OPTIONS "-w")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
        target_sources(emp-tool PRIVATE ${_b3}/blake3_neon.c)
        set_source_files_properties(${_b3_core} ${_b3}/blake3_neon.c PROPERTIES COMPILE_OPTIONS "-w")
        target_compile_definitions(emp-tool PRIVATE BLAKE3_USE_NEON=1)
    else()
        set_source_files_properties(${_b3_core} PROPERTIES COMPILE_OPTIONS "-w")
        target_compile_definitions(emp-tool PRIVATE
            BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2 BLAKE3_NO_AVX512)
    endif()
    target_include_directories(emp-tool PUBLIC
        $<BUILD_INTERFACE:${_b3}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/emp-tool/third_party/blake3>)
    target_compile_definitions(emp-tool PUBLIC EMP_WITH_BLAKE3)
    if(EMP_HASH_BACKEND STREQUAL "blake3")
        target_compile_definitions(emp-tool PUBLIC EMP_HASH_DEFAULT=::emp::HashOption::blake3)
    endif()
endif()
if(EMP_FS_HASH STREQUAL "blake3")
    target_compile_definitions(emp-tool PUBLIC EMP_FS_HASH_DEFAULT=::emp::HashOption::blake3)
elseif(EMP_FS_HASH STREQUAL "sha256")
    # Explicit pin (only meaningful when EMP_HASH_BACKEND=blake3 would
    # otherwise drag the FS default along with it).
    target_compile_definitions(emp-tool PUBLIC EMP_FS_HASH_DEFAULT=::emp::HashOption::sha256)
endif()

# ---------------------------------------------------------------------
# Install + export
# ---------------------------------------------------------------------
set(_cmake_install_dir ${CMAKE_INSTALL_LIBDIR}/cmake/emp-tool)

# Always generate the build-tree config files, even when install rules
# are off, so build-tree consumers (find_package via -Demp-tool_DIR=<build>)
# get a working *-config.cmake. The template uses a path relative to the
# config file itself, so the same generated file works for both
# build-tree and install-tree layouts.
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emp-tool-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/emp-tool-config.cmake
    INSTALL_DESTINATION ${_cmake_install_dir})

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/emp-tool-config-version.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

if(EMP_TOOL_INSTALL)
    install(TARGETS emp-tool
        EXPORT  emp-toolTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(DIRECTORY emp-tool
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
            PATTERN "*.empbc")   # native circuit assets

    install(FILES emp-tool/ir/files/README.md
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/emp-tool/ir/files)

    install(DIRECTORY emp-tool/ir/files/manifests
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/emp-tool/ir/files
        FILES_MATCHING PATTERN "*.json")

    install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/emp-tool)

    # Vendored BLAKE3 public header (only its API is needed by consumers; the
    # .c backends are already compiled into libemp-tool).
    if(EMP_WITH_BLAKE3)
        install(FILES third_party/blake3/blake3.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/emp-tool/third_party/blake3)
        install(FILES
            third_party/blake3/LICENSE_A2
            third_party/blake3/LICENSE_A2LLVM
            third_party/blake3/LICENSE_CC0
            third_party/blake3/VENDOR.md
            DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/emp-tool/blake3)
    endif()

    install(EXPORT emp-toolTargets
        NAMESPACE   emp-tool::
        DESTINATION ${_cmake_install_dir}
        FILE        emp-toolTargets.cmake)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/emp-tool-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/emp-tool-config-version.cmake
        DESTINATION ${_cmake_install_dir})
endif()

# Build-tree export so siblings can consume without `make install`. Use
# the export(TARGETS ...) form (not export(EXPORT ...)) so it works
# regardless of whether install rules registered an export set.
export(TARGETS emp-tool
       NAMESPACE emp-tool::
       FILE      ${CMAKE_CURRENT_BINARY_DIR}/emp-toolTargets.cmake)

# ---------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------
if(EMP_TOOL_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------
if(EMP_TOOL_BUILD_TESTS)
    include(CTest)
    add_subdirectory(test)
endif()

if(EMP_TOOL_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
