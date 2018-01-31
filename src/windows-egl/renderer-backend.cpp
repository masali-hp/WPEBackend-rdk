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
        void resize(uint32_t width, uint32_t height);
        HWND hwnd() { return m_backend->display.nativeWindow(); }

        IPC::Client ipcClient;

    private:
        // IPC::Client::Handler
        void handleMessage(char* data, size_t size) override;

        struct wpe_renderer_backend_egl_target* target;

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
        resize(width, height);
        ShowWindow(hwnd(), SW_SHOW);
    }

    void EGLTarget::resize(uint32_t width, uint32_t height)
    {
        RECT clientRect;
        GetClientRect(hwnd(), &clientRect);
        if (clientRect.right - clientRect.left == width &&
            clientRect.bottom - clientRect.top == height)
            return;

        RECT windowRect;
        GetWindowRect(hwnd(), &windowRect);
        windowRect.right = windowRect.left + width;
        windowRect.bottom = windowRect.top + height;
        BOOL bMenu = FALSE;
        AdjustWindowRect(&windowRect, m_backend->display.windowStyles(), bMenu);
        SetWindowPos(hwnd(), NULL, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, 0);
    }

    EGLTarget::~EGLTarget()
    {
        ipcClient.deinitialize();
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
        case IPC::WindowsEGL::SetSizeAndStyle::code:
        {
            auto& sizeMessage = IPC::WindowsEGL::SetSizeAndStyle::cast(message);
            wpe_renderer_backend_egl_target_resize(target, sizeMessage.width, sizeMessage.height);
            break;
        }
        default:
            fprintf(stderr, "EGLTarget: unhandled message\n");
        };
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
    },
        // resize
        [](void* data, uint32_t width, uint32_t height)
    {
        auto& target = *static_cast<WindowsEGL::EGLTarget*>(data);
        target.resize(width, height);
    },
        // frame_will_render
        [](void* data)
    {
    },
        // frame_rendered
        [](void* data)
    {
        auto& target = *static_cast<WindowsEGL::EGLTarget*>(data);
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
