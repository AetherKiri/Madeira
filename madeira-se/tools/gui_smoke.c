/* A tiny Win32 GUI workload used by the standalone Wine/TCTI regression. */

#include <windows.h>

#include <stdlib.h>

static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lparam)->lpCreateParams);
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                   int show_command)
{
    static const wchar_t class_name[] = L"MadeiraSEGuiSmoke";
    WNDCLASSW window_class = {0};
    char size_value[64];
    char *separator;
    long expected_width;
    long expected_height;
    DWORD window_style = WS_OVERLAPPEDWINDOW;
    RECT window_rect;
    RECT client_rect;
    HMENU menu;
    HWND window;
    MSG message;

    (void)previous;
    (void)command_line;
    (void)show_command;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    if (!RegisterClassW(&window_class)) return 41;
    if (!(menu = CreateMenu())) return 46;
    if (!AppendMenuW(menu, MF_STRING, 1, L"System")) return 47;
    expected_width = 800;
    expected_height = 450;
    if (GetEnvironmentVariableA("MADEIRA_SE_WINDOW_SIZE", size_value,
                                sizeof(size_value))) {
        expected_width = strtol(size_value, &separator, 10);
        if (separator == size_value || (*separator != 'x' && *separator != 'X'))
            return 48;
        expected_height = strtol(separator + 1, &separator, 10);
        if (*separator || expected_width <= 0 || expected_height <= 0)
            return 48;
    }
    window_rect.left = 0;
    window_rect.top = 0;
    window_rect.right = expected_width;
    window_rect.bottom = expected_height;
    if (!AdjustWindowRectEx(&window_rect, window_style, TRUE, 0)) return 48;
    window = CreateWindowExW(0, class_name, L"Madeira-SE GUI smoke",
                             window_style, 0, 0,
                             window_rect.right - window_rect.left,
                             window_rect.bottom - window_rect.top,
                             NULL, menu, instance, (void *)0x1234);
    if (!window) return 42;
    if (!GetClientRect(window, &client_rect) ||
        client_rect.right - client_rect.left != expected_width ||
        client_rect.bottom - client_rect.top != expected_height)
        return 49;
    if (!SetWindowTextW(window, L"Madeira-SE title update")) return 45;
    ShowWindow(window, SW_HIDE);
    /* The standalone regression must not depend on explorer's message
     * pump.  Tear down the window on the same thread and post the quit
     * sentinel explicitly; real applications keep their normal message
     * loop, while this test remains deterministic in a headless prefix. */
    if (!DestroyWindow(window)) return 43;
    /* Wine may release a window-owned menu as part of DestroyWindow. */
    (void)DestroyMenu(menu);
    PostQuitMessage(0);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return message.message == WM_QUIT ? 43 : 44;
}
