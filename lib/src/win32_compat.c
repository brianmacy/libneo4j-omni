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
 * Out-of-line Win32 shims (see win32_compat.h). Compiled ONLY by the
 * Windows CMake build; it is not in lib/src/Makefile.am.
 *
 * The WSA -> errno mapping follows the approach of memgraph/mgclient's
 * src/windows/mgsocket.c (Apache-2.0); it is an independent implementation,
 * not a copy.
 */
#ifdef _WIN32

#include "../../config.h"
#include "win32_compat.h"
#include <fcntl.h>


int neo4j_win32_wsa_to_errno(int wsaerr)
{
    switch (wsaerr)
    {
    case 0:                     return 0;
    case WSAEINTR:              return EINTR;
    case WSAEBADF:              return EBADF;
    case WSAEACCES:             return EACCES;
    case WSAEFAULT:             return EFAULT;
    case WSAEINVAL:             return EINVAL;
    case WSAEMFILE:             return EMFILE;
    case WSAEWOULDBLOCK:        return EWOULDBLOCK;
    case WSAEINPROGRESS:        return EINPROGRESS;
    case WSAEALREADY:           return EALREADY;
    case WSAENOTSOCK:           return ENOTSOCK;
    case WSAEDESTADDRREQ:       return EDESTADDRREQ;
    case WSAEMSGSIZE:           return EMSGSIZE;
    case WSAEPROTOTYPE:         return EPROTOTYPE;
    case WSAENOPROTOOPT:        return ENOPROTOOPT;
    case WSAEPROTONOSUPPORT:    return EPROTONOSUPPORT;
    case WSAESOCKTNOSUPPORT:    return ESOCKTNOSUPPORT;
    case WSAEOPNOTSUPP:         return EOPNOTSUPP;
    case WSAEPFNOSUPPORT:       return EPFNOSUPPORT;
    case WSAEAFNOSUPPORT:       return EAFNOSUPPORT;
    case WSAEADDRINUSE:         return EADDRINUSE;
    case WSAEADDRNOTAVAIL:      return EADDRNOTAVAIL;
    case WSAENETDOWN:           return ENETDOWN;
    case WSAENETUNREACH:        return ENETUNREACH;
    case WSAENETRESET:          return ENETRESET;
    case WSAECONNABORTED:       return ECONNABORTED;
    case WSAECONNRESET:         return ECONNRESET;
    case WSAENOBUFS:            return ENOBUFS;
    case WSAEISCONN:            return EISCONN;
    case WSAENOTCONN:           return ENOTCONN;
    /* send() after shutdown: POSIX (with MSG_NOSIGNAL) reports EPIPE */
    case WSAESHUTDOWN:          return EPIPE;
    case WSAETIMEDOUT:          return ETIMEDOUT;
    case WSAECONNREFUSED:       return ECONNREFUSED;
    case WSAELOOP:              return ELOOP;
    case WSAENAMETOOLONG:       return ENAMETOOLONG;
    case WSAEHOSTDOWN:          return EHOSTUNREACH;
    case WSAEHOSTUNREACH:       return EHOSTUNREACH;
    case WSAEDISCON:            return EPIPE;
    case WSANOTINITIALISED:     return EINVAL;
    case WSA_NOT_ENOUGH_MEMORY: return ENOMEM;
    default:                    return EIO;
    }
}


int neo4j_win32_wsa_fail(void)
{
    errno = neo4j_win32_wsa_to_errno(WSAGetLastError());
    return -1;
}


int neo4j_win32_mkstemp(char *tmpl)
{
    size_t len = strlen(tmpl);
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* _mktemp_s cycles a single character through 'a'..'z' (plus the pid),
     * skipping names that already exist; retry the exclusive create if we
     * lose a race for the chosen name. */
    for (int attempt = 0; attempt < 26; ++attempt)
    {
        memcpy(tmpl + len - 6, "XXXXXX", 6);
        if (_mktemp_s(tmpl, len + 1) != 0)
        {
            errno = EEXIST;
            return -1;
        }
        int fd = _open(tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_NOINHERIT,
                _S_IREAD | _S_IWRITE);
        if (fd >= 0)
        {
            return fd;
        }
        if (errno != EEXIST)
        {
            return -1;
        }
    }
    errno = EEXIST;
    return -1;
}


int neo4j_win32_rename(const char *from, const char *to)
{
    if (!MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING))
    {
        switch (GetLastError())
        {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            errno = ENOENT;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            errno = EACCES;
            break;
        default:
            errno = EIO;
            break;
        }
        return -1;
    }
    return 0;
}


int neo4j_win32_socket_init(void)
{
    WSADATA data;
    int err = WSAStartup(MAKEWORD(2, 2), &data);
    if (err != 0)
    {
        /* WSAStartup returns the error; it does not set WSAGetLastError */
        errno = neo4j_win32_wsa_to_errno(err);
        return -1;
    }
    return 0;
}


int neo4j_win32_socket_cleanup(void)
{
    if (WSACleanup() == SOCKET_ERROR)
    {
        return neo4j_win32_wsa_fail();
    }
    return 0;
}

#endif/*_WIN32*/
