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
//#include <vector>
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

LRESULT Display::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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
        uint32_t keyCode = (uint32_t) wParam;
        uint32_t unicode = 0;
        bool pressed = true;
        struct wpe_input_keyboard_event event = { (uint32_t)GetMessageTime(), keyCode, unicode, pressed, getModifiers() };
        EventDispatcher::singleton().sendEvent(event);
    } break;
    case WM_KEYUP:
    {
        uint32_t keyCode = (uint32_t) wParam;
        uint32_t unicode = 0;
        bool pressed = false;
        struct wpe_input_keyboard_event event = { (uint32_t)GetMessageTime(), keyCode, unicode, pressed, getModifiers() };
        EventDispatcher::singleton().sendEvent(event);
    } break;
    case WM_CHAR:
    {
        uint32_t keyCode = 0;
        uint32_t unicode = (uint32_t) wParam;
        bool pressed = true;
        struct wpe_input_keyboard_event event = { (uint32_t)GetMessageTime(), keyCode, unicode, pressed, getModifiers() };
        EventDispatcher::singleton().sendEvent(event);
    } break;
    }
    return 0;
}

Display& Display::singleton()
{
    static Display display;
    return display;
}

Display::Display()
{
    auto expandVersion = [](int major, int minor) {
        return major * 10 + minor;
    };

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
        wcex.cbWndExtra = 4; // 4 bytes for the browser window pointer
        wcex.hInstance = 0;
        wcex.hIcon = 0; // LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINLAUNCHER));
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = 0;
        wcex.lpszClassName = szWindowClass;
        RegisterClassEx(&wcex);
    }

    bool showWindowFrame = true;
    int width = 800;
    int height = 600;

    m_hwnd = CreateWindow(szWindowClass, TEXT("WPE Display"),
        showWindowFrame ? WS_OVERLAPPEDWINDOW : WS_POPUP,
        CW_USEDEFAULT, 0,
        width, height,
        NULL, NULL, NULL, NULL);
    if (!m_hwnd) {
        fprintf(stderr, "CreateWindow failed.  Error=0x%08x (%s, line %d)\n", GetLastError(), __FILE__, __LINE__);
        return;
    }
    SetWindowLongPtr(m_hwnd, 0, (LONG_PTR)this);

    m_hdc = GetDC(m_hwnd);

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

    //https://www.khronos.org/registry/EGL/sdk/docs/man/html/eglChooseConfig.xhtml
    EGLint esBit = (expandVersion(major, minor) >= expandVersion(1, 5))
        ? EGL_OPENGL_ES3_BIT
        : EGL_OPENGL_ES2_BIT;

    std::vector<EGLint> attribList =
    {
        // Choose RGBA8888
        EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        // EGL1.5 spec Section 2.2 says that depth, multisample and stencil buffer depths
        // must match for contexts to be compatible.
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_SAMPLE_BUFFERS, 0,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_CONFIG_CAVEAT, EGL_NONE,
        EGL_CONFORMANT, esBit,
        EGL_RENDERABLE_TYPE, esBit,
    };

    attribList.push_back(EGL_NONE);
    EGLint num_config = 0;
    if (!eglChooseConfig(m_display, attribList.data(), &m_config, 1, &num_config) || num_config == 0) {
        fprintf(stderr, "Failed to find an EGL config matching requirements!  (%s, %d)\n", __FILE__, __LINE__);
        printf("EGL configurations:\n");
        printEGLConfigurations();
        releaseResources();
        return;
    }

    printf("EGL configuration:\n");
    printEGLConfiguration(m_config);
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

LRESULT CALLBACK Display::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LONG_PTR longPtr = GetWindowLongPtr(hWnd, 0);
    Display* display = reinterpret_cast<Display*>(longPtr);

    if (display) {
        return display->WndProc(hWnd, message, wParam, lParam);
    }

    return CallWindowProc(DefWindowProc, hWnd, message, wParam, lParam);
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

void EventDispatcher::setIPC( IPC::Client& ipcClient )
{
    m_ipc = &ipcClient;
}


} // namespace Windows
