/*
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

#include "Pipe.h"
#include "threadname.h"
#include <stdio.h>

const wchar_t * pipeNameFmt = L"\\\\.\\pipe\\wpebackend-%d";

namespace Windows {

#define PIPE_TIMEOUT 5000
#define BUFSIZE 4096

#define USE_MESSAGE_MODE true
#define USE_WAIT_NAMED_PIPE true

    Pipe::Pipe(int pipeID, HANDLE pipe, bool server, const char * threadName, ReadCallback cb, void * ctx)
        : m_server(server)
        , m_pipe_id(pipeID)
        , m_pipe(pipe)
        , m_readThread(INVALID_HANDLE_VALUE)
        , m_readCallback(cb)
        , m_readContext(ctx)
        , m_logOutput(NULL)
        , m_logOutputCtx(NULL)
    {
        if (pipe != INVALID_HANDLE_VALUE) {
            m_overlappedRead = CreateEvent(NULL, FALSE, FALSE, NULL);
            m_overlappedWrite = CreateEvent(NULL, FALSE, FALSE, NULL);
        }
        else {
            m_overlappedRead = INVALID_HANDLE_VALUE;
            m_overlappedWrite = INVALID_HANDLE_VALUE;
        }
        CreateReadThread(threadName);
    }

    Pipe::~Pipe(void)
    {
        Close();
    }

    void Pipe::Close()
    {
        if (m_pipe != INVALID_HANDLE_VALUE) {
            if (m_logOutput) {
                char msg[128];
                int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::Close(), closing pipe %p", m_pipe);
                m_logOutput(msg, len, m_logOutputCtx);
            }
            if (m_server)
                DisconnectNamedPipe(m_pipe);
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
        if (m_overlappedRead != INVALID_HANDLE_VALUE) {
            SetEvent(m_overlappedRead); // if there is a listener thread waiting on a read, simply closing the pipe isn't enough to wake the read operation.  manually signal the event.
            CloseHandle(m_overlappedRead);
            m_overlappedRead = INVALID_HANDLE_VALUE;
        }
        if (m_overlappedWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(m_overlappedWrite);
            m_overlappedWrite = INVALID_HANDLE_VALUE;
        }
        if (m_readThread != INVALID_HANDLE_VALUE) {
            WaitForSingleObject(m_readThread, INFINITE); // wait for pipe thread to close.
            CloseHandle(m_readThread);
            m_readThread = INVALID_HANDLE_VALUE;
        }
    }

    void Pipe::CreateReadThread(const char * threadName)
    {
        m_readThread = CreateThread(NULL, 0, ReceiveThreadStatic, this, 0, nullptr);
        SetThreadName(GetThreadId(m_readThread), threadName);
    }

    /*static*/
    Pipe* Pipe::CreateServer(int pipeID, const char * threadName, ReadCallback callback, void * ctx)
    {
        wchar_t pipeName[64];
        _snwprintf_s(pipeName, _countof(pipeName), _TRUNCATE, pipeNameFmt, pipeID);

        /*if (callback) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::CreateServer(), creating pipe %S", pipeName);
            callback(msg, len, ctx);
        }*/

        SECURITY_ATTRIBUTES securityAttributes;
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.lpSecurityDescriptor = NULL;
        securityAttributes.bInheritHandle = TRUE;

        DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED;
        DWORD pipeMode = PIPE_WAIT;

        if (USE_MESSAGE_MODE)
            pipeMode |= PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE;
        else
            pipeMode |= PIPE_TYPE_BYTE;

        HANDLE pipe = CreateNamedPipeW(pipeName,
            openMode,
            pipeMode,
            1,
            BUFSIZE * sizeof(TCHAR),
            BUFSIZE * sizeof(TCHAR),
            PIPE_TIMEOUT,
            &securityAttributes);

        /*if (callback) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::ConnectClient(), CreateNamedPipe returned pipe handle 0x%08x", (unsigned int)pipe);
            callback(msg, len, ctx);
        }*/
        if (pipe == INVALID_HANDLE_VALUE)
            return nullptr;

        return new Pipe(pipeID, pipe, true, threadName, callback, ctx);
    }

    bool Pipe::WaitForClient(int timeout)
    {
        if (!m_server) {
            if (m_logOutput) {
                char msg[128];
                int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::WaitForClient(), method must be called when pipe is in server mode");
                m_logOutput(msg, len, m_logOutputCtx);
            }
            return false;
        }

        if (!IsValid()) {
            if (m_logOutput) {
                char msg[256];
                int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::WaitForClient(), pipe handle is invalid.  CreateServer must be called before WaitForClient.");
                m_logOutput(msg, len, m_logOutputCtx);
            }
            return false;
        }

        OVERLAPPED op;
        memset(&op, 0, sizeof(op));
        op.hEvent = m_overlappedRead;

        SetLastError(ERROR_SUCCESS);
        DWORD result = ConnectNamedPipe(m_pipe, &op);
        DWORD error = GetLastError();

        if (m_logOutput) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::WaitForClient(), ConnectNamedPipe returned 0x%08x, GetLastError returned 0x%08x", result, error);
            m_logOutput(msg, len, m_logOutputCtx);
        }
        if (error == ERROR_PIPE_CONNECTED) {
            return true;
        }
        else if (error == ERROR_IO_PENDING || error == ERROR_SUCCESS) {
            DWORD dwWait = WaitForSingleObjectEx(
                op.hEvent,          // event object to wait for
                timeout,            // may be INFINITE
                TRUE);              // alertable wait enabled
            if (dwWait == WAIT_OBJECT_0 || dwWait == WAIT_IO_COMPLETION) {
                return true;
            }
            if (m_logOutput) {
                char msg[128];
                int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::WaitForClient(), unexpected result from WaitForSingleObjectEx %d, GetLastError returned 0x%08x", dwWait, GetLastError());
                m_logOutput(msg, len, m_logOutputCtx);
            }
        }

        Close();
        return false;
    }

    /* static */
    Pipe* Pipe::ConnectClient(int pipeID, const char * threadName, ReadCallback cb, void * ctx)
    {
        wchar_t pipeName[64];
        _snwprintf_s(pipeName, _countof(pipeName), _TRUNCATE, pipeNameFmt, pipeID);

        /*if (callback) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::ConnectClient(), connecting as client to %S", pipeName);
            callback(msg, len, ctx);
        }*/

        if (USE_WAIT_NAMED_PIPE) {
            if (!WaitNamedPipeW(pipeName, NMPWAIT_WAIT_FOREVER)) {
                return nullptr;
            }
        }

        HANDLE hPipe = CreateFileW(
            pipeName,       // pipe name
            GENERIC_READ |  // read and write access
            GENERIC_WRITE,
            0,              // no sharing
            NULL,           // default security attributes
            OPEN_EXISTING,  // opens existing pipe
            FILE_FLAG_OVERLAPPED, // overlapped I/O
            NULL);          // no template file

        /*if (callback) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::ConnectClient(), CreateFile returned pipe handle 0x%08x", hPipe);
            callback(msg, len, ctx);
        }*/

        if (USE_MESSAGE_MODE) {
            DWORD dwMode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(hPipe, &dwMode, NULL, NULL)) {
                return nullptr;
            }
        }

        return new Pipe(pipeID, hPipe, false, threadName, cb, ctx);
    }

    void WINAPI CompletedRoutine(DWORD dwErr, DWORD cbBytes, LPOVERLAPPED lpOverLap)
    {
        reinterpret_cast<OVERLAPPED_OP*>(lpOverLap)->Transferred += cbBytes;
        SetEvent(lpOverLap->hEvent);
    }

    bool Pipe::WaitForIOCompletion(const char * method, const OVERLAPPED_OP & op, DWORD & error)
    {
        while (error == ERROR_SUCCESS && op.Transferred < op.Total) {
            DWORD dwWait = WaitForSingleObjectEx(
                op.Overlap.hEvent,
                INFINITE,
                TRUE); // alertable wait enabled

            error = GetLastError();
            if (m_logOutput) {
                char msg[256];
                int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::WaitForIOCompletion (%s), WaitForSingleObjectEx returned %d, GetLastError returned 0x%08x, Transferred=%zd", method, dwWait, error, op.Transferred);
                m_logOutput(msg, len, m_logOutputCtx);
            }

            if (dwWait != WAIT_IO_COMPLETION && dwWait != WAIT_OBJECT_0)
                return false;

            if (!IsValid()) {
                error = ERROR_INVALID_HANDLE;
                return false;
            }
        }
        return error == ERROR_SUCCESS && op.Transferred == op.Total;
    }

    void Pipe::Send(size_t size, char * buffer, DWORD& error)
    {
        error = ERROR_SUCCESS;

        if (size == 0)
            return;

        OVERLAPPED_OP op;
        memset(&op, 0, sizeof(op));
        op.Overlap.hEvent = m_overlappedWrite;
        op.Total = size;

        if (m_logOutput) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::WritePrivate, writing %zd bytes", size);
            m_logOutput(msg, len, m_logOutputCtx);
        }

        SetLastError(ERROR_SUCCESS);
        BOOL bResult = ::WriteFileEx(m_pipe, buffer, (DWORD) size, (LPOVERLAPPED)&op, CompletedRoutine);
        error = GetLastError();

        if (m_logOutput) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::WritePrivate, WriteFileEx returned %s, GetLastError returned 0x%08x", bResult ? "TRUE" : "FALSE", error);
            m_logOutput(msg, len, m_logOutputCtx);
        }

        if (bResult && error == ERROR_SUCCESS)
            WaitForIOCompletion("WriteFileEx", op, error);
    }

    void Pipe::Receive(size_t& size, char * buffer, DWORD& error)
    {
        error = ERROR_SUCCESS;

        if (size == 0)
            return;

        OVERLAPPED_OP op;
        memset(&op, 0, sizeof(op));
        op.Overlap.hEvent = m_overlappedRead;
        op.Total = size;

        if (m_logOutput) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::ReadPrivate, reading %zd bytes", size);
            m_logOutput(msg, len, m_logOutputCtx);
        }

        SetLastError(ERROR_SUCCESS);
        BOOL bResult = ::ReadFileEx(m_pipe, buffer, (DWORD)size, (LPOVERLAPPED)&op, CompletedRoutine);
        error = GetLastError();

        if (m_logOutput) {
            char msg[128];
            int len = _snprintf_s(msg, _countof(msg), _TRUNCATE, "Pipe::ReadPrivate, ReadFileEx returned %s, GetLastError returned 0x%08x", bResult ? "TRUE" : "FALSE", error);
            m_logOutput(msg, len, m_logOutputCtx);
        }

        if (bResult && error == ERROR_SUCCESS)
            WaitForIOCompletion("ReadFileEx", op, error);
    }

    void Pipe::SetDebugCallback(PipeMessageDebugCallback callback, void * ctx)
    {
        m_logOutput = callback;
        m_logOutputCtx = ctx;
    }

    DWORD WINAPI Pipe::ReceiveThreadStatic(void* ctx)
    {
        reinterpret_cast<Pipe*>(ctx)->ReceiveThread();
        return 0;
    }

    void Pipe::ReceiveThread()
    {
        while (IsValid()) {
            if (m_server) {
                WaitForClient(INFINITE);
            }
            while (IsValid()) {
                char message[BUFSIZE];
                size_t read = BUFSIZE;
                DWORD error;
                Receive(read, message, error);
                if (error != ERROR_SUCCESS)
                    break;
                m_readCallback(read, message, m_readContext);
            }
        }
    }

}
