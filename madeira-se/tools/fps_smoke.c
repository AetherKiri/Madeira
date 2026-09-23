/* A deterministic Win32/OpenGL workload for Madeira-SE frame-rate tests. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>

typedef BOOL (WINAPI *swap_interval_proc)(int interval);

static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
    (void)wparam;
    (void)lparam;
    if (message == WM_CLOSE || message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static HGLRC create_context(HWND window)
{
    PIXELFORMATDESCRIPTOR descriptor = {0};
    HDC dc = GetDC(window);
    int format;
    HGLRC context;

    if (!dc) return NULL;
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    format = ChoosePixelFormat(dc, &descriptor);
    if (!format || !SetPixelFormat(dc, format, &descriptor)) {
        ReleaseDC(window, dc);
        return NULL;
    }
    context = wglCreateContext(dc);
    ReleaseDC(window, dc);
    return context;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                   int show_command)
{
    static const wchar_t class_name[] = L"MadeiraSEFpsSmoke";
    WNDCLASSW window_class = {0};
    HWND window;
    HDC dc;
    HGLRC context;
    MSG message;
    ULONGLONG deadline;
    unsigned frame = 0;

    (void)previous;
    (void)command_line;
    (void)show_command;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    if (!RegisterClassW(&window_class)) return 41;
    window = CreateWindowExW(0, class_name, L"Madeira-SE FPS smoke",
                             WS_OVERLAPPEDWINDOW, 0, 0, 640, 360,
                             NULL, NULL, instance, NULL);
    if (!window) return 42;
    dc = GetDC(window);
    context = create_context(window);
    if (!context || !wglMakeCurrent(dc, context)) {
        if (context) wglDeleteContext(context);
        ReleaseDC(window, dc);
        DestroyWindow(window);
        return 43;
    }

    /* Keep the default display-sync behavior, but allow a raw throughput run
     * to remove the compositor's refresh-rate ceiling. */
    if (getenv("MADEIRA_SE_FPS_VSYNC") != NULL &&
        strcmp(getenv("MADEIRA_SE_FPS_VSYNC"), "0") == 0) {
        swap_interval_proc swap_interval =
            (swap_interval_proc)wglGetProcAddress("wglSwapIntervalEXT");
        if (swap_interval) swap_interval(0);
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    /* Keep the payload bounded so the harness can compare complete runs. */
    deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) goto done;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        glViewport(0, 0, 640, 360);
        glClearColor((GLfloat)((frame & 31u) / 31.0f), 0.12f, 0.22f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!SwapBuffers(dc)) goto done;
        ++frame;
    }

done:
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    return frame > 0 ? 0 : 44;
}
