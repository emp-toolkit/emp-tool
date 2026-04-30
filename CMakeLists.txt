cmake_minimum_required(VERSION 3.21)
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
include(CheckCSourceRuns)

# ---------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------
option(EMP_TOOL_BUILD_TESTS       "Build emp-tool tests"               ${PROJECT_IS_TOP_LEVEL})
option(EMP_TOOL_INSTALL           "Generate install / export rules"    ${PROJECT_IS_TOP_LEVEL})
option(EMP_TOOL_NATIVE_ARCH       "Build for the host CPU (-march=native) so VAES, VPCLMULQDQ, AVX-512 etc. are used when available. Default ON for performance; turn OFF to produce a portable binary that runs on any AES-NI + PCLMUL + SSSE3 CPU." ON)
option(EMP_TOOL_THREADING         "Use thread-local protocol/circuit execution pointers" OFF)
option(EMP_TOOL_CRYPTO_IN_CIRCUIT "Embed AES-128 / Keccak Bristol circuits as static blobs" OFF)

# ---------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------
# rdseed probe (x86 only)
# ---------------------------------------------------------------------
set(EMP_TOOL_HAS_RDSEED OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i.86)")
    set(_save_flags "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -mrdseed")
    check_c_source_runs("
        #include <x86intrin.h>
        int main(void){ unsigned long long r; return _rdseed64_step(&r) == -1; }"
        EMP_TOOL_RDSEED_RUNS)
    set(CMAKE_REQUIRED_FLAGS "${_save_flags}")
    if(EMP_TOOL_RDSEED_RUNS)
        set(EMP_TOOL_HAS_RDSEED ON)
    endif()
endif()

# ---------------------------------------------------------------------
# Source list
# ---------------------------------------------------------------------
set(emp_tool_sources
    emp-tool/emp-tool.cpp)

# Generated circuit blobs land in the build tree, not the source tree.
if(EMP_TOOL_CRYPTO_IN_CIRCUIT)
    find_program(XXD_EXECUTABLE xxd REQUIRED)
    set(_gen_dir ${CMAKE_CURRENT_BINARY_DIR}/generated/emp-tool/circuits/files/bristol_fashion)
    file(MAKE_DIRECTORY ${_gen_dir})

    function(_emp_embed_circuit blob_path header)
        get_filename_component(_stem "${blob_path}" NAME_WE)
        set(_out "${_gen_dir}/${_stem}.txt.cpp")
        # xxd derives the symbol name from the file path it sees; running it
        # from the source root preserves emp_tool_circuits_files_... symbols
        # already declared in the headers.
        add_custom_command(
            OUTPUT  "${_out}"
            COMMAND ${CMAKE_COMMAND} -E echo "#include \"emp-tool/circuits/${header}\"" > "${_out}"
            COMMAND ${XXD_EXECUTABLE} -i "${blob_path}" >> "${_out}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${blob_path}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Embedding ${blob_path}"
            VERBATIM)
        set(emp_tool_sources ${emp_tool_sources} "${_out}" PARENT_SCOPE)
    endfunction()

    _emp_embed_circuit(emp-tool/circuits/files/bristol_fashion/aes_128.txt   aes_128_ctr.h)
    _emp_embed_circuit(emp-tool/circuits/files/bristol_fashion/Keccak_f.txt  sha3_256.h)
endif()

# ---------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------
add_library(emp-tool ${emp_tool_sources})
add_library(emp-tool::emp-tool ALIAS emp-tool)

set_target_properties(emp-tool PROPERTIES
    VERSION        ${PROJECT_VERSION}
    SOVERSION      ${PROJECT_VERSION_MAJOR}
    CXX_EXTENSIONS OFF)

target_compile_features(emp-tool PUBLIC cxx_std_17)

target_include_directories(emp-tool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(emp-tool PUBLIC
    OpenSSL::Crypto
    Threads::Threads)

target_compile_options(emp-tool PRIVATE
    -Wall -Wno-unused-result
    $<$<CONFIG:Release>:-O3 -funroll-loops>
    $<$<CONFIG:Debug>:-O0 -g>)

# Suppress -Wignored-attributes globally (lib + consumers). Triggered
# by every template instantiated on `block` (== __m128i carries vector
# / alignment / may_alias attributes that template type identity drops).
# Benign here — we never rely on those attributes flowing through the
# template argument — but spammy for downstream test / consumer builds
# unless made PUBLIC.
target_compile_options(emp-tool PUBLIC -Wno-ignored-attributes)

# Architecture floor. Per-function CPU dispatch (VAES / VPCLMULQDQ etc.) is
# handled in source via __attribute__((target(...))). Set EMP_TOOL_NATIVE_ARCH
# to bake -march=native into the build for benchmarks; do NOT ship binaries
# with that on — they will SIGILL on older CPUs.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
    target_compile_options(emp-tool PUBLIC -march=armv8-a+crypto+crc)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i.86)")
    if(EMP_TOOL_NATIVE_ARCH)
        target_compile_options(emp-tool PUBLIC -march=native)
    else()
        target_compile_options(emp-tool PUBLIC -maes -mpclmul -mssse3 -msse4.1)
    endif()
endif()

# Feature defines.
if(EMP_TOOL_HAS_RDSEED)
    target_compile_definitions(emp-tool PUBLIC ENABLE_RDSEED)
    target_compile_options(emp-tool PUBLIC -mrdseed)
endif()
if(EMP_TOOL_THREADING)
    target_compile_definitions(emp-tool PUBLIC THREADING)
endif()
if(EMP_TOOL_CRYPTO_IN_CIRCUIT)
    target_compile_definitions(emp-tool PUBLIC CRYPTO_IN_CIRCUIT)
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
            PATTERN "*.txt")

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
# Tests
# ---------------------------------------------------------------------
if(EMP_TOOL_BUILD_TESTS)
    include(CTest)
    add_subdirectory(test)
endif()
