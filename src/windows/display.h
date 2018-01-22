/*
 * Copyright (C) 2015, 2016 Igalia S.L.
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

#ifndef wpe_view_backend_windows_display_h
#define wpe_view_backend_windows_display_h

#include <array>
#include <unordered_map>
#include <utility>
#include <wpe/input.h>
#include "ipc.h"
#include <EGL/egl.h>

namespace Windows {

class EventDispatcher
{
public:
    static EventDispatcher& singleton();
    void sendEvent( wpe_input_axis_event& event );
    void sendEvent( wpe_input_pointer_event& event );
    void sendEvent( wpe_input_touch_event& event );
    void sendEvent( wpe_input_keyboard_event& event );
    void sendEvent( wpe_input_touch_event_raw& event );
    void setIPC( IPC::Client& ipcClient );
    enum MsgType
    {
	AXIS = 0x30, // mouse wheel
	POINTER,
	TOUCH,
	TOUCHSIMPLE,
	KEYBOARD
    };
private:
    EventDispatcher() {};
    ~EventDispatcher() {};
    IPC::Client * m_ipc;
};

class Display {
public:
    static Display& singleton();

    HWND nativeWindow() { return m_hwnd; }
    EGLDisplay eglDisplay() { return m_display; }
    EGLConfig eglConfig() { return m_config; }
    EGLNativeDisplayType eglNativeDisplay() { return m_hdc; }

private:
    Display();
    ~Display();

    void releaseResources();
    void printEGLConfigurations();
    void printEGLConfiguration(EGLConfig &);
    LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void MessageThread();
    static DWORD WINAPI MessageThreadStatic(void *);

    HANDLE m_windowInitializedEvent;
    HANDLE h_messageLoopThread;
    HWND m_hwnd;
    HDC m_hdc;
    EGLDisplay m_display;
    EGLConfig m_config;
};

} // namespace Wayland

#endif // wpe_view_backend_wayland_display_h
