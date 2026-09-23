/* Minimal D3D11 workload used to verify that DXMT reaches native Metal. */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

int main(void)
{
    static const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D11_TEXTURE2D_DESC texture_desc = {0};
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *texture = NULL;
    ID3D11RenderTargetView *view = NULL;
    D3D_FEATURE_LEVEL selected;
    const FLOAT color[4] = {0.125f, 0.25f, 0.5f, 1.0f};
    HRESULT result;

    fprintf(stderr, "d3d11-smoke: before D3D11CreateDevice\n");
    fflush(stderr);
    result = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                               requested, ARRAYSIZE(requested),
                               D3D11_SDK_VERSION, &device, &selected, &context);
    fprintf(stderr, "d3d11-smoke: after D3D11CreateDevice hr=0x%08lx\n",
            (unsigned long)result);
    fflush(stderr);
    if (FAILED(result)) return 61;

    /* Exercise the path used by real D3D11 titles for dynamic constant and
     * vertex buffers.  The mapped address must be guest-visible: returning a
     * native CRT/Metal pointer here makes an x86-64 TCTI store fault even
     * though device creation and render-target operations succeed. */
    {
        D3D11_BUFFER_DESC buffer_desc = {0};
        ID3D11Buffer *buffer = NULL;
        D3D11_MAPPED_SUBRESOURCE mapped = {0};
        DWORD *words;

        buffer_desc.ByteWidth = 64;
        buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
        buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        result = ID3D11Device_CreateBuffer(device, &buffer_desc, NULL, &buffer);
        if (FAILED(result) || !buffer) {
            ID3D11DeviceContext_Release(context);
            ID3D11Device_Release(device);
            return 65;
        }
        result = ID3D11DeviceContext_Map(context, (ID3D11Resource *)buffer, 0,
                                         D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result) || !mapped.pData) {
            ID3D11Buffer_Release(buffer);
            ID3D11DeviceContext_Release(context);
            ID3D11Device_Release(device);
            return 66;
        }
        words = (DWORD *)mapped.pData;
        words[0] = 0x4d415044; /* "MAPD" */
        words[1] = (DWORD)selected;
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)buffer, 0);
        ID3D11Buffer_Release(buffer);
    }

    texture_desc.Width = 16;
    texture_desc.Height = 16;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    result = ID3D11Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    if (FAILED(result)) {
        ID3D11DeviceContext_Release(context);
        ID3D11Device_Release(device);
        return 62;
    }
    result = ID3D11Device_CreateRenderTargetView(device,
                                                  (ID3D11Resource *)texture,
                                                  NULL, &view);
    if (FAILED(result)) {
        ID3D11Texture2D_Release(texture);
        ID3D11DeviceContext_Release(context);
        ID3D11Device_Release(device);
        return 63;
    }

    ID3D11DeviceContext_ClearRenderTargetView(context, view, color);
    ID3D11DeviceContext_Flush(context);
    ID3D11RenderTargetView_Release(view);
    ID3D11Texture2D_Release(texture);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    return selected >= D3D_FEATURE_LEVEL_10_0 ? 49 : 64;
}
