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
 * Internal POSIX-compatibility shims for native Win32 builds (MSVC, and
 * clang-cl / MinGW as a secondary target).
 *
 * This header is ONLY ever included under `#ifdef _WIN32`; it is never seen
 * by the autotools (Linux / macOS) build. It is NOT installed: the public
 * header carries only the two types consumers need (`ssize_t` and
 * `struct iovec`), so a C++ consumer is not polluted with `strndup` et al.
 *
 * Socket handles: Winsock `SOCKET` is a UINT_PTR, but this library threads
 * sockets through the existing `int fd` API unchanged. Windows kernel handles
 * are guaranteed to fit in 32 bits (they are sign-extendable for 32/64-bit
 * interop), which is the same assumption OpenSSL's socket BIO makes.
 * `INVALID_SOCKET` is mapped to -1 at the single creation site (network.c).
 */
#ifndef NEO4J_WIN32_COMPAT_H
#define NEO4J_WIN32_COMPAT_H

#ifndef _WIN32
#error "win32_compat.h must only be included on _WIN32"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
/* util.h defines static inline min()/max(); windows.h must not macro them. */
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* ssize_t and struct iovec come from the public header. */
#include "neo4j-client.h"

#ifndef SSIZE_MAX
#define SSIZE_MAX INTPTR_MAX
#endif

/* errno values the UCRT <errno.h> does not define. The values are the
 * matching WSA codes, which are far outside the CRT's 1..~140 range, so
 * they cannot collide with a CRT errno. */
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
#endif
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT WSAEPFNOSUPPORT
#endif

/* C11 `thread_local` normally comes from <threads.h>, which connection.c,
 * result_stream.c and transaction.c include only on POSIX. */
#ifndef thread_local
#ifdef _MSC_VER
#define thread_local __declspec(thread)
#else
#define thread_local _Thread_local
#endif
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

/* x86-64 and ARM64 Windows are little-endian (config_win.h sets
 * HAVE_BSWAP_64, which util.h uses for htobe64 / be64toh). */
#define bswap_64(x) _byteswap_uint64(x)

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

/* strerror_r (XSI, int-returning) -> strerror_s: both return 0 on success. */
#define strerror_r(errnum, buf, buflen) strerror_s((buf), (buflen), (errnum))

#define flockfile _lock_file
#define funlockfile _unlock_file

#define fdopen _fdopen
#define unlink _unlink


static inline struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    return (gmtime_s(result, timep) == 0)? result : NULL;
}


static inline struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return (localtime_s(result, timep) == 0)? result : NULL;
}


static inline char *neo4j_win32_strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *dup = malloc(len + 1);
    if (dup == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(dup, s, len);
    dup[len] = '\0';
    return dup;
}
#define strndup neo4j_win32_strndup


static inline int neo4j_win32_vasprintf(char **strp, const char *fmt,
        va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int len = _vscprintf(fmt, ap2);
    va_end(ap2);
    if (len < 0)
    {
        *strp = NULL;
        return -1;
    }
    char *buf = malloc((size_t)len + 1);
    if (buf == NULL)
    {
        *strp = NULL;
        errno = ENOMEM;
        return -1;
    }
    int written = vsnprintf(buf, (size_t)len + 1, fmt, ap);
    if (written < 0)
    {
        free(buf);
        *strp = NULL;
        return -1;
    }
    *strp = buf;
    return written;
}
#define vasprintf neo4j_win32_vasprintf


static inline int neo4j_win32_asprintf(char **strp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = neo4j_win32_vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}
#define asprintf neo4j_win32_asprintf


/**
 * Map a Winsock error code (WSAGetLastError()) to the closest errno value,
 * so callers that inspect errno behave as they do on POSIX.
 *
 * @internal
 */
int neo4j_win32_wsa_to_errno(int wsaerr);

/**
 * Set errno from WSAGetLastError(). Always returns -1, so a failing socket
 * call can be written `return neo4j_win32_wsa_fail();`.
 *
 * @internal
 */
int neo4j_win32_wsa_fail(void);

/**
 * mkstemp(3) replacement: replaces the trailing "XXXXXX" of `tmpl` and
 * creates the file exclusively. Returns an fd, or -1 (errno set).
 *
 * @internal
 */
int neo4j_win32_mkstemp(char *tmpl);
#define mkstemp neo4j_win32_mkstemp

/**
 * rename(2) replacement with POSIX semantics (atomically replaces an
 * existing destination), via MoveFileExA(MOVEFILE_REPLACE_EXISTING).
 *
 * @internal
 */
int neo4j_win32_rename(const char *from, const char *to);

/**
 * Initialise / tear down Winsock. Called from neo4j_client_init/cleanup.
 *
 * @internal
 */
int neo4j_win32_socket_init(void);
int neo4j_win32_socket_cleanup(void);

#endif/*NEO4J_WIN32_COMPAT_H*/
