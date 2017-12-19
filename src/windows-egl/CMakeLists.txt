find_package(EGL REQUIRED)

add_definitions(-DBACKEND_WINDOWS_EGL=1 ${EGL_DEFINITIONS})

list(APPEND WPE_PLATFORM_INCLUDE_DIRECTORIES
    "${CMAKE_SOURCE_DIR}/src/windows"
    ${EGL_INCLUDE_DIRS}
)

list(APPEND WPE_PLATFORM_LIBRARIES
    ${EGL_LIBRARIES}
)

list(APPEND WPE_PLATFORM_SOURCES
    src/windows-egl/renderer-backend.cpp
    src/windows-egl/view-backend.cpp
    src/windows/display.cpp
    src/wayland/display.h
)
