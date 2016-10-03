# Copyright (c) 2016 Richard Maxwell
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# ------------------------------------------------------------------------------
# sewing library
# ------------------------------------------------------------------------------

# =========================================
# Possible support matrix (from Boost 1.60)
# =========================================

# Arch   | Linux(unix)        | Windows    | MacOS X       | iOS
# =======|====================|============|===============|===============
# arm32  | AAPCS / ELF        | AAPCS / PE | -             | AAPCS / MACH-O
# arm64  | AAPCS / ELF        | -          | -             | AAPCS / MACH-O
# i386   | SYSV  / ELF        | MS / PE    | SYSV / MACH-O | -
# mip1   | O32   / ELF        | -          | -             | -
# ppc32  | SYSV  / ELF,XCOFF  | -          | SYSV / MACH-O | -
# ppc64  | SYSV  / ELF,XCOFF  | -          | SYSV / MACH-O | -
# sparc  | -                  | -          | -             | -
# x86_64 | SYSV,X32 / ELF     | MS / PE    | SYSV / MACH-O | -

# arch names: arm, aarch64, mips64el, mipsel, x86, x86_64

set(PROJECT_NAME_SEWING "sewing")
project(${PROJECT_NAME_SEWING})

cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

# ------------------------------------------------------------------------------

set(sewing_use_cxx_11    OFF CACHE BOOL "Build C++11 instead of C11.")
set(sewing_use_hwloc     OFF CACHE BOOL "Test: Bind threads to cores (hwloc).")
set(sewing_use_valgrind  OFF CACHE BOOL "Build valgrind friendly library.")
set(sewing_guard_page    "0" CACHE STRING "Stack guard page size (bytes).")

if (sewing_use_cxx_11)
    enable_language(CXX)
    set(sewing_language "CXX")
else()
    enable_language(C)
    set(sewing_language "C")
endif()

# ------------------------------------------------------------------------------

if (WIN32)
    enable_language(ASM_MASM)
else()
    enable_language(ASM)
endif()

# ------------------------------------------------------------------------------

if (UNIX)
    Find_Package(Threads REQUIRED)
    if (NOT THREADS_FOUND)
        message(FATAL_ERROR "Threading Library not found!")
    endif()

    set(sewing_dependencies ${CMAKE_THREAD_LIBS_INIT})
endif()

# ------------------------------------------------------------------------------

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(sewing_platform "x86_64")
endif()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
    set(sewing_platform "x86_64")
endif()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "i386")
    set(sewing_platform "i386")
endif()

if (sewing_platform STREQUAL "x86_64")
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "4")
        set(sewing_platform "i386")
    endif()
endif()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        set(sewing_platform "arm64")
    else()
        set(sewing_platform "arm")
    endif()

    set(sewing_os  "aapcs")
endif()

if (WIN32)
    if (sewing_platform STREQUAL "x86_64")
        set(WINXXBITS Win64)
    else()
        set(WINXXBITS Win32)
    endif()

    if (NOT sewing_os)
        set(sewing_os  "ms")
    endif()

    set(sewing_abi "pe")

    if (MSVC)
        set(sewing_asm "masm.asm")
    endif()
elseif(APPLE)
    set(sewing_os  "sysv")
    set(sewing_abi "macho")
    set(sewing_asm "gas.S")
elseif(UNIX)
    if (NOT sewing_os)
        set(sewing_os  "sysv")
    endif()

    set(sewing_abi "elf")
    set(sewing_asm "gas.S")
endif()

# ------------------------------------------------------------------------------

if (WIN32)
    if (sewing_use_cxx_11)
        set(CMAKE_EXE_LINKER_FLAGS "/SAFESEH:NO")
    endif()
endif()

# ------------------------------------------------------------------------------

message("-- System processor: ${CMAKE_SYSTEM_PROCESSOR}")
message("-- Target processor: ${sewing_platform}")
message("-- Target asm      : ${sewing_platform}-${sewing_os}-${sewing_abi}-${sewing_asm}")

# ------------------------------------------------------------------------------

if (NOT sewing_platform)
    message(FATAL_ERROR "Platform not supported.")
endif()

if (NOT sewing_os)
    message(FATAL_ERROR "OS not supported.")
endif()

if (NOT sewing_abi)
    message(FATAL_ERROR "ABI not supported.")
endif()

if (NOT sewing_asm)
    message(FATAL_ERROR "assembler not supported.")
endif()

# ------------------------------------------------------------------------------

set(ASM_SOURCES
    src/asm/make_${sewing_platform}_${sewing_os}_${sewing_abi}_${sewing_asm}
    src/asm/jump_${sewing_platform}_${sewing_os}_${sewing_abi}_${sewing_asm}
    src/fcontext.h)

# ------------------------------------------------------------------------------

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SEWING_SOURCE
    README.md
    include/sewing.h
    src/sewing.c)

# Oh comon cmake, why would you mark a .md file as compilable?
set_source_files_properties("README.md" PROPERTIES HEADER_FILE_ONLY TRUE)

set_source_files_properties(${SEWING_SOURCE}
    PROPERTIES LANGUAGE ${sewing_language})

add_library(
    ${PROJECT_NAME_SEWING}
    STATIC
    ${ASM_SOURCES}
    ${SEWING_SOURCE})

if (sewing_dependencies)
    target_link_libraries(${PROJECT_NAME_SEWING} ${sewing_dependencies})
endif()

if (sewing_use_cxx_11)
    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 11)
    set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
else()
    set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 11)
    set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD_REQUIRED ON)
endif()

if (sewing_use_valgrind)
    add_definitions(-DSEW_VALGRIND=1)
endif()

if (WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

add_definitions(-DSEW_STACK_GUARD=${sewing_guard_page})

# ------------------------------------------------------------------------------

set(PROJECT_NAME "sewing_test")
project(${PROJECT_NAME} C)

set(SOURCES_EXAMPLE
    test/sewing_test.c)

add_executable(
    ${PROJECT_NAME}
    ${SOURCES_EXAMPLE})

target_link_libraries(${PROJECT_NAME} sewing)

set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 99)
set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD_REQUIRED ON)

if (sewing_use_hwloc)
    find_path(HWLOC_PATH hwloc.h)
    find_library(HWLOC_LIB NAMES hwloc libhwloc)

    if (HWLOC_PATH-NOTFOUND)
        message(FATAL_ERROR "Cannot find hwloc.h")
    endif()

    if (HWLOC_LIB-NOTFOUND)
        message(FATAL_ERROR "Cannot find the hwloc library")
    endif()

    add_definitions(-DSEW_DEMO_USE_HWLOC=1)
    include_directories(${HWLOC_PATH})
    target_link_libraries(${PROJECT_NAME} ${HWLOC_LIB})
endif()

enable_testing()
add_test(${PROJECT_NAME} ${PROJECT_NAME})
