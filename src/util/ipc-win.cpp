/*
 * Copyright (C) 2015, 2016 Igalia S.L.
 * Copyright (C) 2015, 2016 Metrological
 * Copyright (C) 2017 HP Development Company, L.P.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ipc.h"

#include <cstdio>
#include <string>
#include <stdlib.h>

namespace IPC {

Host::Host()
    : m_handler(nullptr)
    , m_pipe(nullptr)
    , m_readThreadHandle(INVALID_HANDLE_VALUE)
{
}

void Host::initialize(Handler& handler)
{
    m_handler = &handler;
    LARGE_INTEGER performanceCount;
    QueryPerformanceCounter(&performanceCount);
    srand(performanceCount.LowPart);
    while (!m_pipe) {
        int id = rand();
        m_pipe = Windows::Pipe::CreateServer(id, "WPEBackend-recv-server", socketCallback, this);
    }
}

void Host::deinitialize()
{
    if (m_pipe) {
        delete m_pipe;
        m_pipe = nullptr;
    }

    m_handler = nullptr;
}

int Host::releaseClientFD()
{
    if (m_pipe) {
        return m_pipe->ID();
    }
    return -1;
}

void Host::sendMessage(char* data, size_t size)
{
    if (!m_pipe || !m_pipe->IsValid())
        return;
    DWORD error;
    m_pipe->Send(size, data, error);
}

void Host::socketCallback(size_t len, char * buffer, void * data)
{
    auto& host = *static_cast<Host*>(data);

    if (len == Message::size)
        host.m_handler->handleMessage(buffer, Message::size);
}

Client::Client() = default;

void Client::initialize(Handler& handler, int id)
{
    m_handler = &handler;

    m_pipe = Windows::Pipe::ConnectClient(id, "WPEBackend-recv-client", socketCallback, this);
}

void Client::deinitialize()
{
    if (m_pipe) {
        delete m_pipe;
        m_pipe = nullptr;
    }

    m_handler = nullptr;
}

/*
void Client::readSynchronously()
{
    if (g_socket_condition_wait(m_socket, G_IO_IN, nullptr, nullptr))
        socketCallback(m_socket, G_IO_IN, this);
}*/

void Client::socketCallback(size_t len, char * buffer, void * data)
{
    auto& host = *static_cast<Client*>(data);

    if (len == Message::size)
        host.m_handler->handleMessage(buffer, Message::size);
}

/*
void Client::sendFd(int fd)
{
    GSocketControlMessage* fdMessage = g_unix_fd_message_new();
    if (!g_unix_fd_message_append_fd(G_UNIX_FD_MESSAGE(fdMessage), fd, nullptr)) {
        g_object_unref(fdMessage);
        return;
    }

    if (g_socket_send_message(m_socket, nullptr, nullptr, 0, &fdMessage, 1, 0, nullptr, nullptr) == -1) {
        g_object_unref(fdMessage);
        return;
    }

    g_object_unref(fdMessage);
}*/

void Client::sendMessage(char* data, size_t size)
{
    if (m_pipe) {
        DWORD error;
        m_pipe->Send(size, data, error);
    }
}

} // namespace IPC
