#define COBJMACROS

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_3.h>

static const char kClassName[] = "TlD3D12ProbeWindow";
static const char kWindowTitle[] = "TradutorLinux D3D12 Probe";
static const char kSuccessMessage[] = "D3D12 command path ready\n";

static void write_message(const char* const message, const DWORD length) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    (void)WriteFile(output, message, length, &written, NULL);
}

static void write_hresult(const char* const prefix, const HRESULT result) {
    static const char digits[] = "0123456789ABCDEF";
    char message[64] = {0};
    DWORD index = 0;
    DWORD written = 0;
    const DWORD value = (DWORD)result;
    const HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    while (prefix[index] != '\0' && index + 1U < (DWORD)sizeof(message)) {
        message[index] = prefix[index];
        ++index;
    }
    if (index + 11U < (DWORD)sizeof(message)) {
        message[index++] = '0';
        message[index++] = 'x';
        for (DWORD shift = 28U; shift > 0U; shift -= 4U) {
            message[index++] = digits[(value >> shift) & 0xFU];
        }
        message[index++] = digits[value & 0xFU];
        message[index++] = '\n';
    }
    (void)WriteFile(output, message, index, &written, NULL);
}

static LRESULT CALLBACK d3d12_window_proc(const HWND window, const UINT message,
                                           const WPARAM wparam, const LPARAM lparam) {
    return DefWindowProcA(window, message, wparam, lparam);
}

static HWND create_probe_window(HINSTANCE* const instance_out, BOOL* const registered_out) {
    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA window_class = {0};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = d3d12_window_proc;
    window_class.hInstance = instance;
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

static void release_resources(ID3D12Device* const device,
                              ID3D12CommandQueue* const queue,
                              ID3D12CommandAllocator* const allocator,
                              ID3D12GraphicsCommandList* const command_list,
                              ID3D12Fence* const fence,
                              IDXGIFactory2* const factory,
                              IDXGISwapChain1* const swap_chain) {
    if (swap_chain != NULL) (void)IDXGISwapChain1_Release(swap_chain);
    if (factory != NULL) (void)IDXGIFactory2_Release(factory);
    if (command_list != NULL) (void)ID3D12GraphicsCommandList_Release(command_list);
    if (fence != NULL) (void)ID3D12Fence_Release(fence);
    if (allocator != NULL) (void)ID3D12CommandAllocator_Release(allocator);
    if (queue != NULL) (void)ID3D12CommandQueue_Release(queue);
    if (device != NULL) (void)ID3D12Device_Release(device);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    HINSTANCE instance = NULL;
    BOOL registered = FALSE;
    const HWND window = create_probe_window(&instance, &registered);
    if (window == NULL) fail_probe(window, instance, registered, 1U);
    (void)ShowWindow(window, SW_HIDE);

    IDXGIFactory2* factory = NULL;
    HRESULT result = CreateDXGIFactory2(0U, &IID_IDXGIFactory2, (void**)&factory);
    if (FAILED(result) || factory == NULL) {
        fail_probe(window, instance, registered, 2U);
    }

    ID3D12Device* device = NULL;
    result = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device,
                               (void**)&device);
    if (FAILED(result) || device == NULL) {
        release_resources(NULL, NULL, NULL, NULL, NULL, factory, NULL);
        fail_probe(window, instance, registered, 3U);
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 0U;

    ID3D12CommandQueue* queue = NULL;
    result = ID3D12Device_CreateCommandQueue(
        device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&queue);
    if (FAILED(result) || queue == NULL) {
        release_resources(device, NULL, NULL, NULL, NULL, factory, NULL);
        fail_probe(window, instance, registered, 4U);
    }

    ID3D12CommandAllocator* allocator = NULL;
    result = ID3D12Device_CreateCommandAllocator(
        device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
        (void**)&allocator);
    if (FAILED(result) || allocator == NULL) {
        release_resources(device, queue, NULL, NULL, NULL, factory, NULL);
        fail_probe(window, instance, registered, 5U);
    }

    ID3D12GraphicsCommandList* command_list = NULL;
    result = ID3D12Device_CreateCommandList(
        device, 0U, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
        &IID_ID3D12GraphicsCommandList, (void**)&command_list);
    if (FAILED(result) || command_list == NULL) {
        release_resources(device, queue, allocator, NULL, NULL, factory, NULL);
        fail_probe(window, instance, registered, 6U);
    }
    result = ID3D12GraphicsCommandList_Close(command_list);
    if (FAILED(result)) {
        release_resources(device, queue, allocator, command_list, NULL, factory, NULL);
        fail_probe(window, instance, registered, 7U);
    }

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {0};
    swap_chain_desc.Width = 64U;
    swap_chain_desc.Height = 64U;
    swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.SampleDesc.Count = 1U;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2U;
    swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    IDXGISwapChain1* swap_chain = NULL;
    result = IDXGIFactory2_CreateSwapChainForHwnd(
        factory, (IUnknown*)queue, window, &swap_chain_desc, NULL, NULL, &swap_chain);
    if (FAILED(result) || swap_chain == NULL) {
        write_hresult("D3D12 swapchain failed: ", result);
        release_resources(device, queue, allocator, command_list, NULL, factory, NULL);
        fail_probe(window, instance, registered, 8U);
    }

    ID3D12Fence* fence = NULL;
    result = ID3D12Device_CreateFence(device, 0U, D3D12_FENCE_FLAG_NONE,
                                      &IID_ID3D12Fence, (void**)&fence);
    if (FAILED(result) || fence == NULL) {
        release_resources(device, queue, allocator, command_list, NULL, factory, swap_chain);
        fail_probe(window, instance, registered, 9U);
    }
    result = ID3D12CommandQueue_Signal(queue, fence, 1U);
    const HRESULT present_result = IDXGISwapChain1_Present(swap_chain, 1U, 0U);
    release_resources(device, queue, allocator, command_list, fence, factory, swap_chain);
    (void)DestroyWindow(window);
    (void)UnregisterClassA(kClassName, instance);
    if (FAILED(result) || FAILED(present_result)) ExitProcess(10U);

    write_message(kSuccessMessage, (DWORD)(sizeof(kSuccessMessage) - 1U));
    ExitProcess(0U);
}
