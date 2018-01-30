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

#include "display.h"
#include "threadname.h"
#include <windowsx.h>

// https://www.khronos.org/registry/EGL/sdk/docs/man/html/eglGetError.xhtml
const char * eglErrorString(EGLint error) {
    switch (error) {
    case EGL_SUCCESS: return "EGL_SUCCESS"; // The last function succeeded without error.
    case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED"; // EGL is not initialized, or could not be initialized, for the specified EGL display connection.
    case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS"; // EGL cannot access a requested resource(for example a context is bound in another thread).
    case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC"; // EGL failed to allocate resources for the requested operation.
    case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE"; // An unrecognized attribute or attribute value was passed in the attribute list.
    case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT"; // An EGLContext argument does not name a valid EGL rendering context.
    case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG"; // An EGLConfig argument does not name a valid EGL frame buffer configuration.
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE"; // The current surface of the calling thread is a window, pixel buffer or pixmap that is no longer valid.
    case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY"; // An EGLDisplay argument does not name a valid EGL display connection.
    case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE"; // An EGLSurface argument does not name a valid surface(window, pixel buffer or pixmap) configured for GL rendering.
    case EGL_BAD_MATCH: return "EGL_BAD_MATCH"; // Arguments are inconsistent(for example, a valid context requires buffers not supplied by a valid surface).
    case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER"; // One or more argument values are invalid.
    case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP"; // A NativePixmapType argument does not refer to a valid native pixmap.
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW"; // A NativeWindowType argument does not refer to a valid native window.
    case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST"; // A power management event has occurred. The application must destroy all contexts and reinitialise OpenGL ES state and objects to continue rendering.
    default: return "???";
    }
}

namespace Windows {

Display& Display::singleton()
{
    static Display display;
    return display;
}

void Display::MessageThread()
{
    static bool classRegistered = false;
    static LPCSTR szWindowClass = TEXT("WPEBackendDisplay");
    if (!classRegistered) {
        classRegistered = true;
        WNDCLASSEX wcex;
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.hIconSm = 0; //LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL))
        wcex.lpszMenuName = 0;
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProcStatic;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = sizeof(this); // browser window pointer
        wcex.hInstance = 0;
        wcex.hIcon = 0; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINLAUNCHER));
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = 0;
        wcex.lpszClassName = szWindowClass;
        RegisterClassEx(&wcex);
    }

    const bool showWindowFrame = true;
    m_windowStyles = showWindowFrame ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    int width = 0;
    int height = 0;

    m_hwnd = CreateWindow(szWindowClass, TEXT("WPE Display"),
        m_windowStyles,
        CW_USEDEFAULT, 0,
        width, height,
        NULL, NULL, NULL, NULL);
    if (!m_hwnd) {
        fprintf(stderr, "CreateWindow failed.  Error=0x%08x (%s, line %d)\n", GetLastError(), __FILE__, __LINE__);
        return;
    }
    SetWindowLongPtr(m_hwnd, 0, (LONG_PTR)this);

    m_hdc = GetDC(m_hwnd);

    SetEvent(m_windowInitializedEvent);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    printf("WPEBackend-rdk: display.cpp: message loop thread ended\n");
    // Now what?  I don't believe WPEBackend provides a clean way to terminate the app...
    // We need to send a message to the UI process and tell it to tear down.
}

DWORD WINAPI Display::MessageThreadStatic(void * ctx)
{
    SetThreadName(::GetCurrentThreadId(), "WPEBackend-UI");
    reinterpret_cast<Display*>(ctx)->MessageThread();
    return 0;
}

Display::Display()
{
    auto expandVersion = [](int major, int minor) {
        return major * 10 + minor;
    };

    m_windowInitializedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    h_messageLoopThread = CreateThread(NULL, 0, MessageThreadStatic, this, 0, NULL);

    m_display = eglGetDisplay(m_hdc);

    if (m_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetDisplay failed (%s, line %d)\n", __FILE__, __LINE__);
        return;
    }
    EGLint major, minor;
    if (!eglInitialize(m_display, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed.  error=%d (%s) (%s, line %d)\n", eglGetError(), eglErrorString(eglGetError()), __FILE__, __LINE__);
        return;
    }

    printf("WPEBackend Display initialization, EGL version %d.%d (%s):\n", major, minor, __FILE__);
    printf("  EGL_CLIENT_APIS = %s\n", eglQueryString(m_display, EGL_CLIENT_APIS));
    printf("  EGL_EXTENSIONS  = %s\n", eglQueryString(m_display, EGL_EXTENSIONS));
    printf("  EGL_VERSION     = %s\n", eglQueryString(m_display, EGL_VERSION));
}

Display::~Display()
{
    releaseResources();
}

void Display::releaseResources()
{
    if (m_display != EGL_NO_DISPLAY) {
        eglTerminate(m_display);
    }
}

void Display::printEGLConfiguration(EGLConfig & config)
{
    struct attribute_to_print {
        EGLint attribute;
        const char * name;
    };
    static attribute_to_print to_print[] = {
        { EGL_CONFIG_ID, "EGL_CONFIG_ID" },
        { EGL_CONFORMANT, "EGL_CONFORMANT" },
        { EGL_BUFFER_SIZE, "EGL_BUFFER_SIZE" },
        { EGL_RED_SIZE, "EGL_RED_SIZE" },
        { EGL_GREEN_SIZE, "EGL_GREEN_SIZE" },
        { EGL_BLUE_SIZE, "EGL_BLUE_SIZE" },
        { EGL_LUMINANCE_SIZE, "EGL_LUMINANCE_SIZE" },
        { EGL_ALPHA_SIZE, "EGL_ALPHA_SIZE" },
        { EGL_NATIVE_RENDERABLE, "EGL_NATIVE_RENDERABLE" },
        { EGL_RENDERABLE_TYPE, "EGL_RENDERABLE_TYPE" },
    };
    for (int j = 0; j < sizeof(to_print) / sizeof(attribute_to_print); j++) {
        EGLint value;
        eglGetConfigAttrib(m_display, config, to_print[j].attribute, &value);
        printf("  %30s = %d\n", to_print[j].name, value);
    }
}

void Display::printEGLConfigurations()
{
    EGLint num_config = 0;
    if (!eglGetConfigs(m_display, nullptr, sizeof(EGLConfig), &num_config)) {
        fprintf(stderr, "eglGetConfigs failed.  error=%d (%s) (%s, line %d)\n", eglGetError(), eglErrorString(eglGetError()), __FILE__, __LINE__);
        return;
    }
    EGLConfig * configs = new EGLConfig[num_config];
    if (!eglGetConfigs(m_display, configs, sizeof(EGLConfig), &num_config)) {
        fprintf(stderr, "eglGetConfigs failed.  error=%d (%s) (%s, line %d)\n", eglGetError(), eglErrorString(eglGetError()), __FILE__, __LINE__);
        return;
    }

    printf("WPEBackend Display initialization, EGL configurations %d:\n", num_config);
    for (int i = 0; i < num_config; i++) {
        printf("Config %d:\n", i);
        printEGLConfiguration(configs[i]);
    }
    delete[] configs;
}

EventDispatcher& EventDispatcher::singleton()
{
    static EventDispatcher event;
    return event;
}

void EventDispatcher::sendEvent( wpe_input_axis_event& event )
{
    if ( m_ipc != nullptr )
    {
        IPC::Message message;
        message.messageCode = MsgType::AXIS;
        memcpy( message.messageData, &event, sizeof(event) );
        m_ipc->sendMessage(IPC::Message::data(message), IPC::Message::size);
    }
}

void EventDispatcher::sendEvent( wpe_input_pointer_event& event )
{
    if ( m_ipc != nullptr )
    {
        IPC::Message message;
        message.messageCode = MsgType::POINTER;
        memcpy( message.messageData, &event, sizeof(event) );
        m_ipc->sendMessage(IPC::Message::data(message), IPC::Message::size);
    }
}

void EventDispatcher::sendEvent( wpe_input_touch_event& event )
{
    if ( m_ipc != nullptr )
    {
        IPC::Message message;
        message.messageCode = MsgType::TOUCH;
        memcpy( message.messageData, &event, sizeof(event) );
        m_ipc->sendMessage(IPC::Message::data(message), IPC::Message::size);
    }
}

void EventDispatcher::sendEvent( wpe_input_keyboard_event& event )
{
    if ( m_ipc != nullptr )
    {
        IPC::Message message;
        message.messageCode = MsgType::KEYBOARD;
        memcpy( message.messageData, &event, sizeof(event) );
        m_ipc->sendMessage(IPC::Message::data(message), IPC::Message::size);
    }
}

void EventDispatcher::sendEvent( wpe_input_touch_event_raw& event )
{
    if ( m_ipc != nullptr )
    {
        IPC::Message message;
        message.messageCode = MsgType::TOUCHSIMPLE;
        memcpy( message.messageData, &event, sizeof(event) );
        m_ipc->sendMessage(IPC::Message::data(message), IPC::Message::size);
    }
}

void EventDispatcher::sendEvent( const SIZE & newSize )
{
    if (m_ipc != nullptr)
    {
        IPC::Message message;
        message.messageCode = MsgType::RESIZE;
        memcpy(message.messageData, &newSize, sizeof(newSize));
        m_ipc->sendMessage(IPC::Message::data(message), IPC::Message::size);
    }
}

void EventDispatcher::sendQuitMessage(void)
{
    if (m_ipc != nullptr)
    {
        IPC::Message message;
        message.messageCode = MsgType::QUIT;
        m_ipc->sendMessage(IPC::Message::data(message), IPC::Message::size);
    }
}

void EventDispatcher::setIPC( IPC::Client& ipcClient )
{
    m_ipc = &ipcClient;
}

bool testMode = false;

static POINT positionForEvent(HWND hWnd, LPARAM lParam)
{
    POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
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

LRESULT CALLBACK Display::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LONG_PTR longPtr = GetWindowLongPtr(hWnd, 0);
    Display* display = reinterpret_cast<Display*>(longPtr);
    LRESULT result = 0;

    if (display) {
        result = display->WndProc(hWnd, message, wParam, lParam);
    }
    else {
        result = CallWindowProc(DefWindowProc, hWnd, message, wParam, lParam);
    }
    return result;
}

LRESULT Display::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    bool emulateTouch = false;
    bool handled = true;
    LRESULT lResult = 0;

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
        POINT p = positionForEvent(hWnd, lParam);

        if (emulateTouch) {
            int32_t id = 0;
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
            // https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_pointer
            wpe_input_pointer_event_type messageType = message == WM_MOUSEMOVE ? wpe_input_pointer_event_type_motion : wpe_input_pointer_event_type_button;

            uint32_t button = 0;
            const uint32_t state_pressed = 1;
            const uint32_t state_released = 0;

            if (wParam & MK_LBUTTON)
                button = 1;
            else if (wParam & MK_MBUTTON)
                button = 3;
            else if (wParam & MK_RBUTTON)
                button = 2;

            uint32_t state = button ? state_pressed : state_released;
            struct wpe_input_pointer_event event = { messageType, (uint32_t)GetMessageTime(), p.x, p.y, button, state };
            EventDispatcher::singleton().sendEvent(event);
        }
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
    case WM_SIZE:
    {
        SIZE newSize{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        EventDispatcher::singleton().sendEvent(newSize);
    } break;
    case WM_CLOSE:
    {
        // https://msdn.microsoft.com/en-us/library/windows/desktop/ff381396(v=vs.85).aspx
        EventDispatcher::singleton().sendQuitMessage();
        // once we round trip, let's then call DestroyWindow, which will call WM_DESTROY, then we'll PostQuitMessage and exit.
    } break;
    case WM_DESTROY:
    {
        PostQuitMessage(0);
    } break;
    default:
    {
        handled = false;
    }
    }

    if (!handled) {
        lResult = DefWindowProc(hWnd, message, wParam, lParam);
    }

    // Let the client know whether we consider this message handled.
    return (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP) ? !handled : lResult;
}

} // namespace Windows
