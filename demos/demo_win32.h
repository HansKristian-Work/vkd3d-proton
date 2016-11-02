/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 * Copyright 2016 Henri Verbeet for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <dxgi1_4.h>
#include <stdbool.h>
#include <stdio.h>

#define DEMO_WINDOW_CLASS_NAME L"demo_wc"

struct demo
{
    HMODULE d3dcompiler;
    HRESULT (WINAPI *compile_from_file)(const WCHAR *filename, const void *defines, void *include,
            const char *entry_point, const char *profile, UINT flags1, UINT flags2,
            ID3DBlob **code, ID3DBlob **errors);
    size_t window_count;
    bool quit;
};

struct demo_window
{
    HINSTANCE instance;
    HWND hwnd;
    struct demo *demo;
    void *user_data;
    void (*draw_func)(void *user_data);
    void (*key_press_func)(struct demo_window *window, demo_key key, void *user_data);
};

struct demo_swapchain
{
    IDXGISwapChain3 *swapchain;
};

static inline struct demo_window *demo_window_create(struct demo *demo, const char *title,
        unsigned int width, unsigned int height, void (*draw_func)(void *user_data), void *user_data)
{
    RECT rect = {0, 0, width, height};
    struct demo_window *window;
    int title_size;
    WCHAR *title_w;
    DWORD style;

    if (!(window = malloc(sizeof(*window))))
        return NULL;

    title_size = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    if (!(title_w = calloc(title_size, sizeof(*title_w))))
    {
        free(window);
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, title, -1, title_w, title_size);

    window->instance = GetModuleHandle(NULL);
    window->draw_func = draw_func;
    window->user_data = user_data;
    window->key_press_func = NULL;

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    AdjustWindowRect(&rect, style, FALSE);
    window->hwnd = CreateWindowExW(0, DEMO_WINDOW_CLASS_NAME, title_w, style, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, window->instance, NULL);
    free(title_w);
    if (!window->hwnd)
    {
        free(window);
        return NULL;
    }
    SetWindowLongPtrW(window->hwnd, GWLP_USERDATA, (LONG_PTR)window);
    window->demo = demo;
    ++demo->window_count;

    return window;
}

static inline void demo_window_destroy(struct demo_window *window)
{
    if (window->hwnd)
        DestroyWindow(window->hwnd);
    if (!--window->demo->window_count)
        window->demo->quit = true;
    free(window);
}

static inline demo_key demo_key_from_vkey(DWORD vkey)
{
    if (vkey == VK_ESCAPE)
        return DEMO_KEY_ESCAPE;
    return DEMO_KEY_UNKNOWN;
}

static inline LRESULT CALLBACK demo_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct demo_window *window = (void *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (message)
    {
        case WM_PAINT:
            if (window && window->draw_func)
                window->draw_func(window->user_data);
                return 0;

        case WM_KEYDOWN:
            if (!window->key_press_func)
                break;
            window->key_press_func(window, demo_key_from_vkey(wparam), window->user_data);
            return 0;

        case WM_DESTROY:
            window->hwnd = NULL;
            demo_window_destroy(window);
            return 0;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static inline void demo_window_set_key_press_func(struct demo_window *window,
        void (*key_press_func)(struct demo_window *window, demo_key key, void *user_data))
{
    window->key_press_func = key_press_func;
}

static inline void demo_process_events(struct demo *demo)
{
    MSG msg = {0};

    while (GetMessage(&msg, NULL, 0, 0) != -1)
    {
        if (msg.message == WM_QUIT)
            break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (demo->quit)
            PostQuitMessage(0);
    }
}

static inline bool demo_init(struct demo *demo)
{
    WNDCLASSEXW wc;

    if (!(demo->d3dcompiler = LoadLibraryW(L"d3dcompiler_47")))
        return false;
    if (!(demo->compile_from_file = (void *)GetProcAddress(demo->d3dcompiler, "D3DCompileFromFile")))
        goto fail;

    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = demo_window_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = DEMO_WINDOW_CLASS_NAME;
    wc.hIconSm = LoadIconW(NULL, IDI_WINLOGO);
    if (!RegisterClassExW(&wc))
        goto fail;

    demo->quit = false;

    return true;

fail:
    FreeLibrary(demo->d3dcompiler);
    return false;
}

static inline void demo_cleanup(struct demo *demo)
{
    UnregisterClassW(DEMO_WINDOW_CLASS_NAME, GetModuleHandle(NULL));
    FreeLibrary(demo->d3dcompiler);
}

static inline struct demo_swapchain *demo_swapchain_create(ID3D12CommandQueue *command_queue,
        struct demo_window *window, const struct demo_swapchain_desc *desc)
{
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
    struct demo_swapchain *swapchain;
    IDXGISwapChain1 *swapchain1;
    IDXGIFactory2 *factory;
    HRESULT hr;

    if (!(swapchain = malloc(sizeof(*swapchain))))
        return NULL;

    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory)))
        goto fail;

    memset(&swapchain_desc, 0, sizeof(swapchain_desc));
    swapchain_desc.BufferCount = desc->buffer_count;
    swapchain_desc.Width = desc->width;
    swapchain_desc.Height = desc->height;
    swapchain_desc.Format = desc->format;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.SampleDesc.Count = 1;

    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)command_queue,
            window->hwnd, &swapchain_desc, NULL, NULL, &swapchain1);
    IDXGIFactory2_Release(factory);
    if (FAILED(hr))
        goto fail;

    hr = IDXGISwapChain1_QueryInterface(swapchain1, &IID_IDXGISwapChain3, (void **)&swapchain->swapchain);
    IDXGISwapChain1_Release(swapchain1);
    if (FAILED(hr))
        goto fail;

    return swapchain;

fail:
    free(swapchain);
    return NULL;
}

static inline unsigned int demo_swapchain_get_current_back_buffer_index(struct demo_swapchain *swapchain)
{
    return IDXGISwapChain3_GetCurrentBackBufferIndex(swapchain->swapchain);
}

static inline ID3D12Resource *demo_swapchain_get_back_buffer(struct demo_swapchain *swapchain, unsigned int index)
{
    ID3D12Resource *buffer;

    if (FAILED(IDXGISwapChain3_GetBuffer(swapchain->swapchain, index,
            &IID_ID3D12Resource, (void **)&buffer)))
        return NULL;

    return buffer;
}

static inline void demo_swapchain_present(struct demo_swapchain *swapchain)
{
    IDXGISwapChain3_Present(swapchain->swapchain, 1, 0);
}

static inline void demo_swapchain_destroy(struct demo_swapchain *swapchain)
{
    IDXGISwapChain3_Release(swapchain->swapchain);
    free(swapchain);
}

static inline HANDLE demo_create_event(void)
{
    return CreateEventA(NULL, FALSE, FALSE, NULL);
}

static inline unsigned int demo_wait_event(HANDLE event, unsigned int ms)
{
    return WaitForSingleObject(event, ms);
}

static inline void demo_destroy_event(HANDLE event)
{
    CloseHandle(event);
}

static inline HRESULT demo_create_root_signature(ID3D12Device *device,
        const D3D12_ROOT_SIGNATURE_DESC *desc, ID3D12RootSignature **signature)
{
    ID3DBlob *blob;
    HRESULT hr;

    if (FAILED(hr = D3D12SerializeRootSignature(desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, NULL)))
        return hr;
    hr = ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)signature);
    ID3D10Blob_Release(blob);

    return hr;
}

static inline bool demo_load_shader(struct demo *demo, const wchar_t *hlsl_name, const char *entry_point,
        const char *profile, const char *spv_name, D3D12_SHADER_BYTECODE *shader)
{
    ID3D10Blob *blob, *errors;
    HRESULT hr;

    hr = demo->compile_from_file(hlsl_name, NULL, NULL, entry_point, profile, 0, 0, &blob, &errors);
    if (errors)
    {
        fprintf(stderr, "%.*s\n", (int)ID3D10Blob_GetBufferSize(errors), (char *)ID3D10Blob_GetBufferPointer(errors));
        ID3D10Blob_Release(errors);
    }
    if (FAILED(hr))
        return false;

    shader->BytecodeLength = ID3D10Blob_GetBufferSize(blob);
    if (!(shader->pShaderBytecode = malloc(shader->BytecodeLength)))
    {
        ID3D10Blob_Release(blob);
        return false;
    }

    memcpy((void *)shader->pShaderBytecode, ID3D10Blob_GetBufferPointer(blob), shader->BytecodeLength);

    ID3D10Blob_Release(blob);
    return true;
}
