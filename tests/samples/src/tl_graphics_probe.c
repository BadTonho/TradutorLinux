#define COBJMACROS

#include <windows.h>
#include <d3d11.h>

static const char kClassName[] = "TlGraphicsProbeWindow";
static const char kWindowTitle[] = "TradutorLinux D3D11 Probe";
static const char kSuccessMessage[] = "D3D11 frame presented\n";

static void write_message(const char* const message, const DWORD length) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    (void)WriteFile(output, message, length, &written, NULL);
}

static LRESULT CALLBACK graphics_window_proc(const HWND window, const UINT message,
                                             const WPARAM wparam, const LPARAM lparam) {
    return DefWindowProcA(window, message, wparam, lparam);
}

static HWND create_probe_window(HINSTANCE* const instance_out, BOOL* const registered_out) {
    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA window_class;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = graphics_window_proc;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = instance;
    window_class.hIcon = NULL;
    window_class.hCursor = NULL;
    window_class.hbrBackground = NULL;
    window_class.lpszMenuName = NULL;
    window_class.lpszClassName = kClassName;

    *instance_out = instance;
    *registered_out = FALSE;
    if (RegisterClassA(&window_class) == 0U) return NULL;
    *registered_out = TRUE;
    return CreateWindowExA(0U, kClassName, kWindowTitle, WS_OVERLAPPEDWINDOW,
                           0, 0, 64, 64, NULL, NULL, instance, NULL);
}

static void fail_probe(const HWND window, const HINSTANCE instance,
                       const BOOL registered, const UINT exit_code) {
    if (window != NULL) (void)DestroyWindow(window);
    if (registered) (void)UnregisterClassA(kClassName, instance);
    ExitProcess(exit_code);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    HINSTANCE instance = NULL;
    BOOL registered = FALSE;
    const HWND window = create_probe_window(&instance, &registered);
    if (window == NULL) fail_probe(window, instance, registered, 1U);
    (void)ShowWindow(window, SW_HIDE);

    DXGI_SWAP_CHAIN_DESC swap_chain_desc;
    swap_chain_desc.BufferDesc.Width = 64U;
    swap_chain_desc.BufferDesc.Height = 64U;
    swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60U;
    swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1U;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    swap_chain_desc.SampleDesc.Count = 1U;
    swap_chain_desc.SampleDesc.Quality = 0U;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 1U;
    swap_chain_desc.OutputWindow = window;
    swap_chain_desc.Windowed = TRUE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swap_chain_desc.Flags = 0U;

    IDXGISwapChain* swap_chain = NULL;
    ID3D11Device* device = NULL;
    ID3D11DeviceContext* context = NULL;
    D3D_FEATURE_LEVEL feature_level = 0;
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0U, NULL, 0U, D3D11_SDK_VERSION,
        &swap_chain_desc, &swap_chain, &device, &feature_level, &context);
    if (FAILED(result) || feature_level < D3D_FEATURE_LEVEL_10_0 ||
        swap_chain == NULL || device == NULL || context == NULL) {
        fail_probe(window, instance, registered, 2U);
    }

    ID3D11Texture2D* back_buffer = NULL;
    result = IDXGISwapChain_GetBuffer(swap_chain, 0U, &IID_ID3D11Texture2D,
                                      (void**)&back_buffer);
    if (FAILED(result) || back_buffer == NULL) {
        ID3D11DeviceContext_Release(context);
        ID3D11Device_Release(device);
        IDXGISwapChain_Release(swap_chain);
        fail_probe(window, instance, registered, 3U);
    }

    ID3D11RenderTargetView* render_target = NULL;
    result = ID3D11Device_CreateRenderTargetView(
        device, (ID3D11Resource*)back_buffer, NULL, &render_target);
    if (FAILED(result) || render_target == NULL) {
        ID3D11Texture2D_Release(back_buffer);
        ID3D11DeviceContext_Release(context);
        ID3D11Device_Release(device);
        IDXGISwapChain_Release(swap_chain);
        fail_probe(window, instance, registered, 4U);
    }

    const FLOAT clear_color[4] = {0.10f, 0.35f, 0.85f, 1.0f};
    ID3D11DeviceContext_ClearRenderTargetView(context, render_target, clear_color);
    ID3D11DeviceContext_Flush(context);
    result = IDXGISwapChain_Present(swap_chain, 1U, 0U);

    ID3D11RenderTargetView_Release(render_target);
    ID3D11Texture2D_Release(back_buffer);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    IDXGISwapChain_Release(swap_chain);
    (void)DestroyWindow(window);
    (void)UnregisterClassA(kClassName, instance);

    if (FAILED(result)) ExitProcess(5U);
    write_message(kSuccessMessage, (DWORD)(sizeof(kSuccessMessage) - 1U));
    ExitProcess(0U);
}
