/* Minimal D3D9 workload used to verify the DXMT Lock/Unlock path. */

#define COBJMACROS
#include <windows.h>
#include <d3d9.h>

static LRESULT CALLBACK smoke_window_proc(HWND window, UINT message,
                                          WPARAM wparam, LPARAM lparam)
{
    (void)window;
    (void)wparam;
    (void)lparam;
    return DefWindowProcA(window, message, wparam, lparam);
}

int main(void)
{
    WNDCLASSA window_class = {0};
    HWND window;
    IDirect3D9 *d3d = NULL;
    IDirect3DDevice9 *device = NULL;
    IDirect3DVertexBuffer9 *vertex_buffer = NULL;
    D3DPRESENT_PARAMETERS present = {0};
    HRESULT result;
    void *mapped = NULL;

    window_class.lpfnWndProc = smoke_window_proc;
    window_class.hInstance = GetModuleHandleA(NULL);
    window_class.lpszClassName = "MadeiraD3D9Smoke";
    if (!RegisterClassA(&window_class)) return 61;
    window = CreateWindowExA(0, window_class.lpszClassName, "Madeira D3D9 smoke",
                             WS_POPUP, 0, 0, 64, 64, NULL, NULL,
                             window_class.hInstance, NULL);
    if (!window) return 62;

    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        DestroyWindow(window);
        return 63;
    }

    present.Windowed = TRUE;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.hDeviceWindow = window;
    present.BackBufferFormat = D3DFMT_A8R8G8B8;
    present.BackBufferWidth = 64;
    present.BackBufferHeight = 64;
    present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    result = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                     window, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                     &present, &device);
    if (FAILED(result)) {
        IDirect3D9_Release(d3d);
        DestroyWindow(window);
        return 64;
    }

    result = IDirect3DDevice9_CreateVertexBuffer(
        device, 64, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
        D3DPOOL_DEFAULT, &vertex_buffer, NULL);
    if (FAILED(result) || !vertex_buffer) {
        IDirect3DDevice9_Release(device);
        IDirect3D9_Release(d3d);
        DestroyWindow(window);
        return 65;
    }
    result = IDirect3DVertexBuffer9_Lock(vertex_buffer, 0, 64, &mapped,
                                         D3DLOCK_DISCARD);
    if (FAILED(result) || !mapped) {
        IDirect3DVertexBuffer9_Release(vertex_buffer);
        IDirect3DDevice9_Release(device);
        IDirect3D9_Release(d3d);
        DestroyWindow(window);
        return 66;
    }
    ((DWORD *)mapped)[0] = 0x4d415039; /* "MAP9" */
    IDirect3DVertexBuffer9_Unlock(vertex_buffer);

    IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET,
                           D3DCOLOR_XRGB(16, 32, 64), 1.0f, 0);
    IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
    IDirect3DVertexBuffer9_Release(vertex_buffer);
    IDirect3DDevice9_Release(device);
    IDirect3D9_Release(d3d);
    DestroyWindow(window);
    return 49;
}
