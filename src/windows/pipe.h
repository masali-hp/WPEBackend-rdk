#pragma once
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

#include <windows.h>

namespace Windows {

    typedef void(*PipeMessageDebugCallback)(const char *, int len, void * ctx);
    void SetPipeMessageDebugCallback(PipeMessageDebugCallback, void * ctx = NULL);

    typedef struct
    {
        OVERLAPPED Overlap;
        size_t Transferred;
        size_t Total;
    } OVERLAPPED_OP;

    class Pipe
    {
    public:
        typedef void(*ReadCallback)(size_t size, char *, void *);
        Pipe() = delete;
        ~Pipe(void);

        Pipe(Pipe &source) = delete;
        Pipe& Pipe::operator= (Pipe &cSource) = delete;

        static Pipe* CreateServer(int pipeId, const char * threadName, ReadCallback cb, void * ctx);
        static Pipe* ConnectClient(int pipeId, const char * threadName, ReadCallback cb, void * ctx);

        int ID() { return m_pipe_id; }
        bool IsValid() { return m_pipe != INVALID_HANDLE_VALUE; }
        bool WaitForClient(int timeout);

        void Send(size_t size, char *, DWORD& error);

        void Close();
        void SetDebugCallback(PipeMessageDebugCallback callback, void * ctx);

    private:
        Pipe(int pipeId, HANDLE pipe, bool server, const char * threadName, ReadCallback cb, void * ctx);
        void CreateReadThread(const char * threadName);

        static DWORD WINAPI ReceiveThreadStatic(void*);
        void ReceiveThread();
        void Receive(size_t &size, char *, DWORD& error);
        bool WaitForIOCompletion(const char * method, const OVERLAPPED_OP & op, DWORD & error, bool waitForAllData);

        bool m_server;
        int m_pipe_id;
        HANDLE m_pipe;
        HANDLE m_overlappedRead;
        HANDLE m_overlappedWrite;

        HANDLE m_readThread;
        ReadCallback m_readCallback;
        void * m_readContext;

        PipeMessageDebugCallback m_logOutput;
        void * m_logOutputCtx;
    };

}
