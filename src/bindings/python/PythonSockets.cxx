/*
 * Copyright (c)2013-2021 ZeroTier, Inc.
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file in the project's root directory.
 *
 * Change Date: 2026-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2.0 of the Apache License.
 */
/****/

/**
 * @file
 *
 * ZeroTier Socket API (Python)
 */

#include "ZeroTierSockets.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <string.h>

#ifdef ZTS_ENABLE_PYTHON

int zts_py_setblocking(int fd, int block)
{
    int new_flags, cur_flags, err = 0;

    Py_BEGIN_ALLOW_THREADS cur_flags = zts_bsd_fcntl(fd, F_GETFL, 0);

    if (cur_flags < 0) {
        err = ZTS_ERR_SOCKET;
        goto done;
    }

    if (! block) {
        new_flags |= ZTS_O_NONBLOCK;
    }
    else {
        new_flags &= ~ZTS_O_NONBLOCK;
    }

    if (new_flags != cur_flags) {
        err = zts_bsd_fcntl(fd, F_SETFL, new_flags);
    }

done:
    Py_END_ALLOW_THREADS

        return err;
}

int zts_py_getblocking(int fd)
{
    int flags;

    Py_BEGIN_ALLOW_THREADS flags = zts_bsd_fcntl(fd, F_GETFL, 0);
    Py_END_ALLOW_THREADS

        if (flags < 0)
    {
        return ZTS_ERR_SOCKET;
    }
    return flags & ZTS_O_NONBLOCK;
}

static int zts_py_tuple_to_sockaddr(int family, PyObject* addr_obj, struct zts_sockaddr* dst_addr, int* addrlen)
{
    if (family == AF_INET) {
        struct zts_sockaddr_in* addr;
        char* host_str;
        int result, port;
        if (! PyTuple_Check(addr_obj)) {
            return ZTS_ERR_ARG;
        }
        if (! PyArg_ParseTuple(addr_obj, "eti:zts_py_tuple_to_sockaddr", "idna", &host_str, &port)) {
            return ZTS_ERR_ARG;
        }
        addr = (struct zts_sockaddr_in*)dst_addr;
        zts_inet_pton(ZTS_AF_INET, host_str, &(addr->sin_addr.s_addr));
        PyMem_Free(host_str);
        if (port < 0 || port > 0xFFFF) {
            return ZTS_ERR_ARG;
        }
        if (result < 0) {
            return ZTS_ERR_ARG;
        }
        addr->sin_family = AF_INET;
        addr->sin_port = lwip_htons((short)port);
        *addrlen = sizeof *addr;
        return ZTS_ERR_OK;
    }
    if (family == AF_INET6) {
        // TODO
    }
    return ZTS_ERR_ARG;
}

PyObject* zts_py_accept(int fd)
{
    struct zts_sockaddr_in addrbuf = { 0 };
    socklen_t addrlen = sizeof(addrbuf);
    int err = zts_bsd_accept(fd, (struct zts_sockaddr*)&addrbuf, &addrlen);
    char ipstr[ZTS_INET_ADDRSTRLEN] = { 0 };
    zts_inet_ntop(ZTS_AF_INET, &(addrbuf.sin_addr), ipstr, ZTS_INET_ADDRSTRLEN);
    PyObject* t;
    t = PyTuple_New(3);
    PyTuple_SetItem(t, 0, PyLong_FromLong(err));   // New file descriptor
    PyTuple_SetItem(t, 1, PyUnicode_FromString(ipstr));
    PyTuple_SetItem(t, 2, PyLong_FromLong(lwip_ntohs(addrbuf.sin_port)));
    Py_INCREF(t);
    return t;
}

int zts_py_listen(int fd, int backlog)
{
    if (backlog < 0) {
        backlog = 128;
    }
    return zts_bsd_listen(fd, backlog);
}

int zts_py_bind(int fd, int family, int type, PyObject* addr_obj)
{
    struct zts_sockaddr_storage addrbuf;
    int addrlen;
    int err;
    if (zts_py_tuple_to_sockaddr(family, addr_obj, (struct zts_sockaddr*)&addrbuf, &addrlen) != ZTS_ERR_OK) {
        return ZTS_ERR_ARG;
    }
    Py_BEGIN_ALLOW_THREADS err = zts_bsd_bind(fd, (struct zts_sockaddr*)&addrbuf, addrlen);
    Py_END_ALLOW_THREADS return err;
}

int zts_py_connect(int fd, int family, int type, PyObject* addr_obj)
{
    struct zts_sockaddr_storage addrbuf;
    int addrlen;
    int err;
    if (zts_py_tuple_to_sockaddr(family, addr_obj, (struct zts_sockaddr*)&addrbuf, &addrlen) != ZTS_ERR_OK) {
        return ZTS_ERR_ARG;
    }
    Py_BEGIN_ALLOW_THREADS err = zts_bsd_connect(fd, (struct zts_sockaddr*)&addrbuf, addrlen);
    Py_END_ALLOW_THREADS return err;
}

PyObject* zts_py_recv(int fd, int len, int flags)
{
    PyObject *t, *buf;
    int bytes_read;

    buf = PyBytes_FromStringAndSize((char*)0, len);
    if (buf == NULL) {
        return NULL;
    }

    bytes_read = zts_bsd_recv(fd, PyBytes_AS_STRING(buf), len, flags);
    t = PyTuple_New(2);
    PyTuple_SetItem(t, 0, PyLong_FromLong(bytes_read));

    if (bytes_read < 0) {
        Py_DECREF(buf);
        Py_INCREF(Py_None);
        PyTuple_SetItem(t, 1, Py_None);
        Py_INCREF(t);
        return t;
    }

    if (bytes_read != len) {
        _PyBytes_Resize(&buf, bytes_read);
    }

    PyTuple_SetItem(t, 1, buf);
    Py_INCREF(t);
    return t;
}

int zts_py_send(int fd, PyObject* buf, int flags)
{
    Py_buffer output;
    int bytes_sent;

    if (PyObject_GetBuffer(buf, &output, PyBUF_SIMPLE) != 0) {
        return 0;
    }

    bytes_sent = zts_bsd_send(fd, output.buf, output.len, flags);
    PyBuffer_Release(&output);

    return bytes_sent;
}

int zts_py_close(int fd)
{
    int err;
    Py_BEGIN_ALLOW_THREADS err = zts_bsd_close(fd);
    Py_END_ALLOW_THREADS return err;
}

#endif   // ZTS_ENABLE_PYTHON
