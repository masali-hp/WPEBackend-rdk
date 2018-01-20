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

Display::Display()
{
    auto expandVersion = [](int major, int minor) {
        return major * 10 + minor;
    };

    m_hdc = GetDC(NULL);
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

void EventDispatcher::setIPC( IPC::Client& ipcClient )
{
    m_ipc = &ipcClient;
}


} // namespace Windows
