# Native Win32 (MSVC) build of libneo4j-client as a STATIC library.
#
# This file is used ONLY on Windows. Linux and macOS keep using the autotools
# build (autogen.sh / configure / make), which this file does not touch.
#
# Output: neo4j-client.lib (MSVC) + include/neo4j-client.h, linking OpenSSL
# through find_package(OpenSSL)'s OpenSSL::SSL / OpenSSL::Crypto targets, so
# it can be absorbed into an MSVC-built DLL.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DOPENSSL_ROOT_DIR=<openssl> -DCMAKE_INSTALL_PREFIX=<prefix>
#   cmake --build build && cmake --install build
#
# Single sources of truth, parsed at configure time (so this file cannot
# drift from the autotools build):
#   * version  <- AC_INIT in configure.ac
#   * sources  <- libneo4j_client_la_SOURCES in lib/src/Makefile.am
cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/configure.ac" _neo4j_configure_ac)
if (NOT _neo4j_configure_ac MATCHES "AC_INIT\\(\\[libneo4j-client\\],[ ]*\\[([0-9]+)\\.([0-9]+)\\.([0-9]+)(~[^]]*)?\\]\\)")
  message(FATAL_ERROR "Could not parse AC_INIT version from configure.ac")
endif ()
set(PACKAGE_MAJOR_VERSION "${CMAKE_MATCH_1}")
set(PACKAGE_MINOR_VERSION "${CMAKE_MATCH_2}")
set(PACKAGE_PATCH_VERSION "${CMAKE_MATCH_3}")
set(PACKAGE_DEVELOPMENT_VERSION "")
set(PACKAGE_STATUS_VERSION 0)
if (CMAKE_MATCH_4)
  # mirrors build-aux/m4/declare_versions.m4: text after '~' is the dev tag
  string(SUBSTRING "${CMAKE_MATCH_4}" 1 -1 PACKAGE_DEVELOPMENT_VERSION)
  set(PACKAGE_STATUS_VERSION 1)
endif ()
set(PACKAGE_VERSION "${PACKAGE_MAJOR_VERSION}.${PACKAGE_MINOR_VERSION}.${PACKAGE_PATCH_VERSION}${CMAKE_MATCH_4}")

project(libneo4j-client
        VERSION "${PACKAGE_MAJOR_VERSION}.${PACKAGE_MINOR_VERSION}.${PACKAGE_PATCH_VERSION}"
        LANGUAGES C)

if (NOT WIN32)
  message(FATAL_ERROR
    "This CMake build is for native Windows only. On Linux/macOS use the "
    "autotools build: ./autogen.sh && ./configure && make")
endif ()

# An in-source autotools run leaves a generated config.h / neo4j-client.h in
# the tree, which would shadow the Windows ones through the sources' relative
# `#include "../../config.h"`. Refuse loudly rather than build against it.
foreach (_stale "${CMAKE_CURRENT_SOURCE_DIR}/config.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/lib/src/neo4j-client.h")
  if (EXISTS "${_stale}")
    message(FATAL_ERROR "Stale autotools output ${_stale} would shadow the "
                        "Windows configuration; remove it (or use a clean checkout).")
  endif ()
endforeach ()

find_package(OpenSSL 1.1 REQUIRED)

# ---- sources: parsed from lib/src/Makefile.am --------------------------------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/lib/src/Makefile.am" _neo4j_makefile_am)
string(REPLACE "\\\n" " " _neo4j_makefile_am "${_neo4j_makefile_am}")  # join continuations
string(REGEX MATCHALL "libneo4j_client_la_SOURCES[ \t]*\\+?=[^\n]*" _neo4j_src_lines
       "${_neo4j_makefile_am}")
set(NEO4J_SOURCES "")
foreach (_line IN LISTS _neo4j_src_lines)
  string(REGEX REPLACE "^libneo4j_client_la_SOURCES[ \t]*\\+?=" "" _line "${_line}")
  separate_arguments(_files UNIX_COMMAND "${_line}")
  foreach (_f IN LISTS _files)
    if (_f MATCHES "\\.c$")
      if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/src/${_f}")
        message(FATAL_ERROR "Makefile.am lists ${_f}, which does not exist")
      endif ()
      list(APPEND NEO4J_SOURCES "lib/src/${_f}")
    endif ()
  endforeach ()
endforeach ()
list(LENGTH NEO4J_SOURCES _neo4j_nsrc)
# The TLS (openssl*.c) block is always included: Windows builds with OpenSSL.
if (NOT "lib/src/openssl.c" IN_LIST NEO4J_SOURCES OR _neo4j_nsrc LESS 20)
  message(FATAL_ERROR "Parsed only ${_neo4j_nsrc} sources from lib/src/Makefile.am "
                      "(expected the full list incl. openssl.c): ${NEO4J_SOURCES}")
endif ()
message(STATUS "libneo4j-client ${PACKAGE_VERSION}: ${_neo4j_nsrc} sources from Makefile.am + win32_compat.c")

# ---- generated headers --------------------------------------------------------
set(_neo4j_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lib/src/neo4j-client.h.in"
               "${_neo4j_gen}/include/neo4j-client.h" @ONLY)
# Every source does `#include "../../config.h"`. Put config_win.h at
# <gen>/cfg/config.h and add <gen>/cfg/_/_ to the include path, so that the
# relative include resolves to it without editing ~30 sources.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lib/src/config_win.h"
               "${_neo4j_gen}/cfg/config.h" COPYONLY)
file(MAKE_DIRECTORY "${_neo4j_gen}/cfg/_/_")

# ---- library ------------------------------------------------------------------
add_library(neo4j-client STATIC ${NEO4J_SOURCES} lib/src/win32_compat.c)
set_target_properties(neo4j-client PROPERTIES
  OUTPUT_NAME neo4j-client
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
target_include_directories(neo4j-client
  PUBLIC  "$<BUILD_INTERFACE:${_neo4j_gen}/include>" "$<INSTALL_INTERFACE:include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib/src" "${_neo4j_gen}/cfg/_/_")
target_compile_definitions(neo4j-client PRIVATE
  "NEO4J_CFG_PACKAGE_VERSION=\"${PACKAGE_VERSION}\""
  WIN32_LEAN_AND_MEAN
  NOMINMAX                          # util.h defines min()/max() functions
  _CRT_SECURE_NO_WARNINGS           # upstream uses strcpy/sprintf-family APIs
  _CRT_NONSTDC_NO_WARNINGS          # upstream uses POSIX names (strdup, ...)
  _WINSOCK_DEPRECATED_NO_WARNINGS)  # keep MSVC's Winsock deprecation noise out of logs
if (MSVC)
  target_compile_options(neo4j-client PRIVATE
    /utf-8
    # C4068 "unknown pragma": upstream's `#pragma GCC diagnostic` lines are
    # GCC/clang-only and intentionally ignored by MSVC.
    /wd4068)
endif ()
target_link_libraries(neo4j-client PUBLIC OpenSSL::SSL OpenSSL::Crypto ws2_32)

install(TARGETS neo4j-client ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES "${_neo4j_gen}/include/neo4j-client.h" DESTINATION include)
