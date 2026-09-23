/* vi:set ts=4 sw=4 expandtab:
 *
 * Copyright 2026, Senzing, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Hand-maintained equivalent of the autotools-generated config.h, for the
 * native Win32 CMake build ONLY (see CMakeLists.txt, which copies this file
 * to <build>/neo4j_cfg/config.h so that every source's
 * `#include "../../config.h"` resolves to it).
 *
 * Every macro below is one that lib/src actually tests (grep HAVE_ / #ifdef
 * in lib/src). Macros for features Windows lacks are deliberately left
 * undefined, so the sources take their fallback branch:
 *   HAVE_PTHREADS        -> thread.h Win32 branch (SRWLOCK / INIT_ONCE)
 *   HAVE_STDATOMIC_H     -> atomic.h Interlocked branch (MSVC)
 *   HAVE_ENDIAN_H, HAVE_HTOBE64, HAVE_BE64TOH -> bswap_64 (win32_compat.h)
 *   HAVE_LANGINFO_CODESET-> render.c forces ASCII-art borders
 *   HAVE_MSG_NOSIGNAL, HAVE_SO_NOSIGPIPE -> Winsock never raises SIGPIPE
 *   HAVE_MEMSET_S        -> util.h maps to memset
 *   STRERROR_R_CHAR_P    -> XSI strerror_r, mapped to strerror_s
 */
#ifndef NEO4J_CONFIG_WIN_H
#define NEO4J_CONFIG_WIN_H

#ifndef _WIN32
#error "config_win.h is for the native Win32 build only"
#endif

#define PACKAGE "libneo4j-client"
#define PACKAGE_NAME "libneo4j-client"
#define PACKAGE_TARNAME "libneo4j-client"
#define PACKAGE_VERSION NEO4J_CFG_PACKAGE_VERSION
#define PACKAGE_STRING "libneo4j-client " NEO4J_CFG_PACKAGE_VERSION
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""
#define VERSION NEO4J_CFG_PACKAGE_VERSION

#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STDIO_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDBOOL_H 1
#define HAVE__BOOL 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1

/* getaddrinfo AI_ADDRCONFIG is supported by Winsock (Vista+) */
#define HAVE_AI_ADDRCONFIG 1

/* x86-64 / ARM64 Windows: little-endian; bswap_64 -> _byteswap_uint64 */
#define HAVE_BSWAP_64 1

/* MSVC and MinGW both support thread-local storage in a static library. */
#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL __thread
#endif

/* TLS via OpenSSL 1.1+ / 3.x (found by find_package(OpenSSL)). */
#define HAVE_OPENSSL 1
#define HAVE_TLS 1
#define HAVE_BIO_METH_NEW 1
#define HAVE_ASN1_STRING_GET0_DATA 1
/* HAVE_CRYPTO_SET_LOCKING_CALLBACK: OpenSSL < 1.1 only; left undefined. */

/* Pull in the POSIX shims ahead of every other header in every TU. */
#include "win32_compat.h"

#endif/*NEO4J_CONFIG_WIN_H*/
