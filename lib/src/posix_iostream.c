/* vi:set ts=4 sw=4 expandtab:
 *
 * Copyright 2016, Chris Leishman (http://github.com/cleishm)
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
#include "../../config.h"
#include "posix_iostream.h"
#include "util.h"
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#ifndef _WIN32
#include <unistd.h>
#endif


struct neo4j_posix_iostream {
    neo4j_iostream_t _iostream;
    int fd;
};


static ssize_t neo4j_posix_read(neo4j_iostream_t *self, void *buf, size_t nbyte);
static ssize_t neo4j_posix_readv(neo4j_iostream_t *self,
        const struct iovec *iov, unsigned int iovcnt);
static ssize_t neo4j_posix_write(neo4j_iostream_t *self,
        const void *buf, size_t nbyte);
static ssize_t neo4j_posix_writev(neo4j_iostream_t *self,
        const struct iovec *iov, unsigned int iovcnt);
static int neo4j_posix_flush(neo4j_iostream_t *self);
static int neo4j_posix_close(neo4j_iostream_t *self);


#ifdef _WIN32
#define NEO4J_WIN32_WSABUF_STACK 16

/*
 * readv/writev over a Winsock stream socket via WSARecv/WSASend. Like
 * readv/writev on a blocking socket, a single call may transfer fewer bytes
 * than requested; callers (neo4j_ios_*_all) already loop on short counts.
 * Returns bytes transferred, 0 on orderly peer shutdown (recv), or -1 with
 * errno mapped from WSAGetLastError().
 */
static ssize_t win32_sock_iov(int fd, const struct iovec *iov,
        unsigned int iovcnt, bool is_send)
{
    if (iovcnt == 0)
    {
        return 0;
    }

    WSABUF stackbufs[NEO4J_WIN32_WSABUF_STACK];
    WSABUF *bufs = stackbufs;
    if (iovcnt > NEO4J_WIN32_WSABUF_STACK)
    {
        bufs = malloc(iovcnt * sizeof(WSABUF));
        if (bufs == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
    }

    /* WSABUF lengths are ULONG, and the return value must fit a DWORD, so
     * cap the whole request at INT_MAX bytes (a short transfer is legal). */
    size_t remaining = INT_MAX;
    DWORD nbufs = 0;
    for (unsigned int i = 0; i < iovcnt && remaining > 0; ++i)
    {
        size_t len = minzu(iov[i].iov_len, remaining);
        bufs[nbufs].buf = (CHAR *)iov[i].iov_base;
        bufs[nbufs].len = (ULONG)len;
        remaining -= len;
        ++nbufs;
    }

    DWORD transferred = 0;
    int rc;
    if (is_send)
    {
        rc = WSASend((SOCKET)fd, bufs, nbufs, &transferred, 0, NULL, NULL);
    }
    else
    {
        DWORD flags = 0;
        rc = WSARecv((SOCKET)fd, bufs, nbufs, &transferred, &flags,
                NULL, NULL);
    }

    ssize_t result;
    if (rc == SOCKET_ERROR)
    {
        int wsaerr = WSAGetLastError();
        /* WSAEDISCON: graceful close on a message-oriented transport;
         * report it as EOF, the same as recv() returning 0. */
        if (!is_send && wsaerr == WSAEDISCON)
        {
            result = 0;
        }
        else
        {
            errno = neo4j_win32_wsa_to_errno(wsaerr);
            result = -1;
        }
    }
    else
    {
        result = (ssize_t)transferred;
    }

    if (bufs != stackbufs)
    {
        int errsv = errno;
        free(bufs);
        errno = errsv;
    }
    return result;
}
#endif


neo4j_iostream_t *neo4j_posix_iostream(int fd)
{
    REQUIRE(fd >= 0, NULL);

    struct neo4j_posix_iostream *ios =
            calloc(1, sizeof(struct neo4j_posix_iostream));
    if (ios == NULL)
    {
        return NULL;
    }

    ios->fd = fd;

    neo4j_iostream_t *iostream = &(ios->_iostream);
    iostream->read = neo4j_posix_read;
    iostream->readv = neo4j_posix_readv;
    iostream->write = neo4j_posix_write;
    iostream->writev = neo4j_posix_writev;
    iostream->flush = neo4j_posix_flush;
    iostream->close = neo4j_posix_close;
    return iostream;
}


ssize_t neo4j_posix_read(neo4j_iostream_t *self, void *buf, size_t nbyte)
{
    struct neo4j_posix_iostream *ios = container_of(self,
            struct neo4j_posix_iostream, _iostream);
    if (ios->fd < 0)
    {
        errno = EPIPE;
        return -1;
    }
#ifdef _WIN32
    int n = recv((SOCKET)ios->fd, buf, (int)minzu(nbyte, INT_MAX), 0);
    return (n == SOCKET_ERROR)? neo4j_win32_wsa_fail() : n;
#else
    return read(ios->fd, buf, nbyte);
#endif
}


ssize_t neo4j_posix_readv(neo4j_iostream_t *self,
        const struct iovec *iov, unsigned int iovcnt)
{
    struct neo4j_posix_iostream *ios = container_of(self,
            struct neo4j_posix_iostream, _iostream);
    if (ios->fd < 0)
    {
        errno = EPIPE;
        return -1;
    }
    if (iovcnt > INT_MAX)
    {
        iovcnt = INT_MAX;
    }
#ifdef _WIN32
    return win32_sock_iov(ios->fd, iov, iovcnt, false);
#else
    return readv(ios->fd, iov, iovcnt);
#endif
}


ssize_t neo4j_posix_write(neo4j_iostream_t *self, const void *buf, size_t nbyte)
{
    struct neo4j_posix_iostream *ios = container_of(self,
            struct neo4j_posix_iostream, _iostream);
    if (ios->fd < 0)
    {
        errno = EPIPE;
        return -1;
    }
#ifdef _WIN32
    /* Winsock never raises SIGPIPE, so no MSG_NOSIGNAL is needed. */
    int n = send((SOCKET)ios->fd, buf, (int)minzu(nbyte, INT_MAX), 0);
    return (n == SOCKET_ERROR)? neo4j_win32_wsa_fail() : n;
#elif defined(HAVE_MSG_NOSIGNAL)
    return send(ios->fd, buf, nbyte, MSG_NOSIGNAL);
#else
    return write(ios->fd, buf, nbyte);
#endif
}


ssize_t neo4j_posix_writev(neo4j_iostream_t *self,
        const struct iovec *iov, unsigned int iovcnt)
{
    struct neo4j_posix_iostream *ios = container_of(self,
            struct neo4j_posix_iostream, _iostream);
    if (ios->fd < 0)
    {
        errno = EPIPE;
        return -1;
    }
    if (iovcnt > INT_MAX)
    {
        iovcnt = INT_MAX;
    }
#ifdef _WIN32
    return win32_sock_iov(ios->fd, iov, iovcnt, true);
#elif defined(HAVE_MSG_NOSIGNAL)
    struct msghdr message;
    memset(&message, 0, sizeof(struct msghdr));
    message.msg_iov = (struct iovec *)(uintptr_t)iov;
    message.msg_iovlen = iovcnt;
    return sendmsg(ios->fd, &message, MSG_NOSIGNAL);
#else
    return writev(ios->fd, iov, iovcnt);
#endif
}


int neo4j_posix_flush(neo4j_iostream_t *self)
{
    return 0;
}


int neo4j_posix_close(neo4j_iostream_t *self)
{
    struct neo4j_posix_iostream *ios = container_of(self,
            struct neo4j_posix_iostream, _iostream);
    if (ios->fd < 0)
    {
        errno = EPIPE;
        return -1;
    }
    int fd = ios->fd;
    ios->fd = -1;
    free(ios);
#ifdef _WIN32
    return (closesocket((SOCKET)fd) == SOCKET_ERROR)?
            neo4j_win32_wsa_fail() : 0;
#else
    return close(fd);
#endif
}
