/*
* Copyright (C) 2015, 2016 Igalia S.L.
* Copyright (C) 2015, 2016 Metrological
* Copyright (C) 2016 SoftAtHome
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

#include <EGL/egl.h>

#include <wpe/renderer-backend-egl.h>

#include "display.h"
#include "ipc.h"
#include "ipc-windowsegl.h"
#include <windowsx.h>

using namespace Windows;

namespace WindowsEGL {

    struct Backend {
        Backend();
        ~Backend();

        Windows::Display& display;
    };

    Backend::Backend()
        : display(Windows::Display::singleton())
    {
    }

    Backend::~Backend()
    {
    }

    struct EGLTarget : public IPC::Client::Handler {
        EGLTarget(struct wpe_renderer_backend_egl_target*, int);
        virtual ~EGLTarget();

        void initialize(Backend& backend, uint32_t width, uint32_t height);
        HWND hwnd() { return m_hwnd; }

        IPC::Client ipcClient;

    private:
        // IPC::Client::Handler
        void handleMessage(char* data, size_t size) override;

        struct wpe_renderer_backend_egl_target* target;

        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        EGLSurface m_surface{ nullptr };

#ifdef WIN32
        HWND m_hwnd{ nullptr };
#endif

        Backend* m_backend{ nullptr };
    };

    EGLTarget::EGLTarget(struct wpe_renderer_backend_egl_target* target, int hostFd)
        : target(target)
    {
        ipcClient.initialize(*this, hostFd);
        Windows::EventDispatcher::singleton().setIPC(ipcClient);
    }

    void EGLTarget::initialize(Backend& backend, uint32_t width, uint32_t height)
    {
        m_backend = &backend;

        static bool classRegistered = false;
        static LPCSTR szWindowClass = TEXT("WPEBackendRenderer");
        if (!classRegistered) {
            classRegistered = true;
            WNDCLASSEX wcex;
            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.hIconSm = 0; //LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL))
            wcex.lpszMenuName = 0;
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProcStatic;
            wcex.cbClsExtra = 0;
            wcex.cbWndExtra = 4; // 4 bytes for the browser window pointer
            wcex.hInstance = 0;
            wcex.hIcon = 0; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINLAUNCHER));
            wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
            wcex.hbrBackground = 0;
            wcex.lpszClassName = szWindowClass;
            RegisterClassEx(&wcex);
        }

        const bool showWindowFrame = true;
        const bool makeVisible = true;
        DWORD styles = showWindowFrame ? WS_OVERLAPPEDWINDOW : WS_POPUP;

        m_hwnd = CreateWindow(szWindowClass, TEXT("WPE Display"),
            styles,
            CW_USEDEFAULT, 0,
            width, height,
            NULL, NULL, NULL, NULL);
        if (!m_hwnd) {
            fprintf(stderr, "CreateWindow failed.  Error=0x%08x (%s, line %d)\n", GetLastError(), __FILE__, __LINE__);
            return;
        }
        SetWindowLongPtr(m_hwnd, 0, (LONG_PTR)this);

        if (makeVisible)
            ShowWindow(m_hwnd, SW_SHOW);
    }

    EGLTarget::~EGLTarget()
    {
        ipcClient.deinitialize();
        if (m_surface)
            eglDestroySurface(m_backend->display.eglDisplay(), m_surface);
        m_surface = nullptr;
    }

    void EGLTarget::handleMessage(char* data, size_t size)
    {
        if (size != IPC::Message::size)
            return;

        auto& message = IPC::Message::cast(data);
        switch (message.messageCode) {
        case IPC::WindowsEGL::FrameComplete::code:
        {
            wpe_renderer_backend_egl_target_dispatch_frame_complete(target);
            break;
        }
        default:
            fprintf(stderr, "EGLTarget: unhandled message\n");
        };
    }

    bool testMode = false;

    static POINT positionForEvent(HWND hWnd, LPARAM lParam)
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &point);
        return point;
    }

    static int horizontalScrollChars()
    {
        static ULONG scrollChars;
        if (!scrollChars && !SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0, &scrollChars, 0))
            scrollChars = 1;
        return scrollChars;
    }

    static int verticalScrollLines()
    {
        static ULONG scrollLines;
        if (!scrollLines && !SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0))
            scrollLines = 3;
        return scrollLines;
    }

    uint8_t getModifiers()
    {
        static const unsigned short HIGH_BIT_MASK_SHORT = 0x8000;
        if (testMode)
            return 0;
        return
            (GetKeyState(VK_SHIFT) & HIGH_BIT_MASK_SHORT ? wpe_input_keyboard_modifier_shift : 0) |
            (GetKeyState(VK_CONTROL) & HIGH_BIT_MASK_SHORT ? wpe_input_keyboard_modifier_control : 0) |
            (GetKeyState(VK_MENU) & HIGH_BIT_MASK_SHORT ? wpe_input_keyboard_modifier_alt : 0);
    }

    LRESULT CALLBACK EGLTarget::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        LONG_PTR longPtr = GetWindowLongPtr(hWnd, 0);
        EGLTarget* target = reinterpret_cast<EGLTarget*>(longPtr);

        if (target) {
            return target->WndProc(hWnd, message, wParam, lParam);
        }

        return CallWindowProc(DefWindowProc, hWnd, message, wParam, lParam);
    }


    LRESULT EGLTarget::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        bool emulateTouch = false;

        switch (message) {
            // POINTER
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        {
            // https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_pointer
            wpe_input_pointer_event_type messageType = message == WM_MOUSEMOVE ? wpe_input_pointer_event_type_motion : wpe_input_pointer_event_type_button;

            // From linux/input-event-codes.h
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

            uint32_t button = 0;
            const uint32_t state_pressed = 1;
            const uint32_t state_released = 0;

            if (wParam & MK_LBUTTON)
                button = BTN_LEFT;
            else if (wParam & MK_MBUTTON)
                button = BTN_MIDDLE;
            else if (wParam & MK_RBUTTON)
                button = BTN_RIGHT;

            uint32_t state = button ? state_pressed : state_released;
            POINT p = positionForEvent(hWnd, lParam);

            if (emulateTouch) {
                int32_t id = 0; //?
                struct wpe_input_touch_event_raw event_touch_simple;
                if (message == WM_LBUTTONDOWN)
                    event_touch_simple = { wpe_input_touch_event_type_down, (uint32_t)GetMessageTime(), id, p.x, p.y };
                else if (message == WM_LBUTTONUP)
                    event_touch_simple = { wpe_input_touch_event_type_up, (uint32_t)GetMessageTime(), id, p.x, p.y };
                else if (message == WM_MOUSEMOVE)
                    event_touch_simple = { wpe_input_touch_event_type_motion, (uint32_t)GetMessageTime(), id, p.x, p.y };
                else
                    break;
                EventDispatcher::singleton().sendEvent(event_touch_simple);
            }
            else {
                struct wpe_input_pointer_event event = { messageType, (uint32_t)GetMessageTime(), p.x, p.y, button, state };
                EventDispatcher::singleton().sendEvent(event);
            }

            // wpe_view_backend_dispatch_pointer_event is invoked if pointer.target.first is true; this can only be true for
            // bcm-nexus, bcm-nexus-wayland as they're the only backends that call registerInputClient.
        } break;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {
            // See https://github.com/WebKit/webkit/blob/master/Source/WebCore/platform/win/WheelEventWin.cpp
            POINT p = positionForEvent(hWnd, lParam);
            const uint32_t vertical_scroll = 0;
            const uint32_t horizontal_scroll = 1;
            uint32_t axis = message == WM_MOUSEHWHEEL || (wParam & MK_SHIFT) ? horizontal_scroll : vertical_scroll;

            static const float cScrollbarPixelsPerLine = 100.0f / 3.0f;
            float delta = GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<float>(WHEEL_DELTA);

            if (message == WM_MOUSEHWHEEL) {
                // Windows is <-- -/+ -->, WebKit wants <-- +/- -->, so we negate
                // |delta| after saving the original value on the wheel tick member.
                delta = -delta;
            }
            int pixelsToScroll = 0;
            if (axis == vertical_scroll) {
                int verticalMultiplier = verticalScrollLines();
                if (verticalMultiplier == WHEEL_PAGESCROLL) {
                    RECT rect;
                    GetClientRect(hWnd, &rect);
                    delta *= (rect.bottom - rect.top);
                }
                else {
                    delta *= static_cast<float>(verticalMultiplier) * cScrollbarPixelsPerLine;
                }
            }
            else {
                delta *= static_cast<float>(horizontalScrollChars()) * cScrollbarPixelsPerLine;
            }
            struct wpe_input_axis_event event = { wpe_input_axis_event_type_motion, (uint32_t)GetMessageTime(), p.x, p.y, axis, (int32_t)delta };
            EventDispatcher::singleton().sendEvent(event);
        } break;
        case WM_KEYDOWN:
        {
            // Does WPE expect char messages to be delivered with key_down?
            // That could be tricky on Windows since typical event loops call TranslateMessage which injects a WM_CHAR message.
            // We could make a a PeekMessage call to remove the WM_CHAR message and handle it here.  We'd set unicode to the wParam of the WM_CHAR message.
            uint32_t keyCode = (uint32_t)wParam;
            uint32_t unicode = 0;
            bool pressed = true;
            struct wpe_input_keyboard_event event = { (uint32_t)GetMessageTime(), keyCode, unicode, pressed, getModifiers() };
            EventDispatcher::singleton().sendEvent(event);
        } break;
        case WM_KEYUP:
        {
            uint32_t keyCode = (uint32_t)wParam;
            uint32_t unicode = 0;
            bool pressed = false;
            struct wpe_input_keyboard_event event = { (uint32_t)GetMessageTime(), keyCode, unicode, pressed, getModifiers() };
            EventDispatcher::singleton().sendEvent(event);
        } break;
        case WM_CHAR:
        {
            uint32_t keyCode = 0;
            uint32_t unicode = (uint32_t)wParam;
            bool pressed = true;
            struct wpe_input_keyboard_event event = { (uint32_t)GetMessageTime(), keyCode, unicode, pressed, getModifiers() };
            EventDispatcher::singleton().sendEvent(event);
        } break;
        }
        return 0;
    }

} // namespace WindowsEGL

extern "C" {

    struct wpe_renderer_backend_egl_interface windows_egl_renderer_backend_egl_interface = {
        // create
        [](int) -> void*
    {
        return new WindowsEGL::Backend;
    },
        // destroy
        [](void* data)
    {
        auto* backend = static_cast<WindowsEGL::Backend*>(data);
        delete backend;
    },
        // get_native_display
        [](void* data) -> EGLNativeDisplayType
    {
        auto& backend = *static_cast<WindowsEGL::Backend*>(data);
        return backend.display.eglNativeDisplay();
    },
    };

    struct wpe_renderer_backend_egl_target_interface windows_egl_renderer_backend_egl_target_interface = {
        // create
        [](struct wpe_renderer_backend_egl_target* target, int host_fd) -> void*
    {
        return new WindowsEGL::EGLTarget(target, host_fd);
    },
        // destroy
        [](void* data)
    {
        auto* target = static_cast<WindowsEGL::EGLTarget*>(data);
        delete target;
    },
        // initialize
        [](void* data, void* backend_data, uint32_t width, uint32_t height)
    {
        auto& target = *static_cast<WindowsEGL::EGLTarget*>(data);
        auto& backend = *static_cast<WindowsEGL::Backend*>(backend_data);
        target.initialize(backend, width, height);
    },
        // get_native_window
        [](void* data) -> EGLNativeWindowType
    {
        auto& target = *static_cast<WindowsEGL::EGLTarget*>(data);
        return target.hwnd();
        //return target.m_window;
    },
        // resize
        [](void* data, uint32_t width, uint32_t height)
    {
    },
        // frame_will_render
        [](void* data)
    {
    },
        // frame_rendered
        [](void* data)
    {
        auto& target = *static_cast<WindowsEGL::EGLTarget*>(data);

        //wl_display *display = target.m_backend->display.display();
        //if (display)
        //    wl_display_flush(display);

        // should we call eglMakeCurrent()?
        // or perhaps eglWaitClient()?

        IPC::Message message;
        IPC::WindowsEGL::BufferCommit::construct(message);
        target.ipcClient.sendMessage(IPC::Message::data(message), IPC::Message::size);
    },
    };

    struct wpe_renderer_backend_egl_offscreen_target_interface windows_egl_renderer_backend_egl_offscreen_target_interface = {
        // create
        []() -> void*
    {
        return nullptr;
    },
        // destroy
        [](void* data)
    {
    },
        // initialize
        [](void* data, void* backend_data)
    {
    },
        // get_native_window
        [](void* data) -> EGLNativeWindowType
    {
        return nullptr;
    },
    };

}
