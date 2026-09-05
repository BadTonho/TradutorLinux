#define COBJMACROS

#include <windows.h>
#include <d3d12.h>

static const char kSuccessMessage[] = "D3D12 command path ready\n";

static void write_message(const char* const message, const DWORD length) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    (void)WriteFile(output, message, length, &written, NULL);
}

static void release_resources(ID3D12Device* const device,
                              ID3D12CommandQueue* const queue,
                              ID3D12CommandAllocator* const allocator,
                              ID3D12GraphicsCommandList* const command_list,
                              ID3D12Fence* const fence) {
    if (command_list != NULL) (void)ID3D12GraphicsCommandList_Release(command_list);
    if (fence != NULL) (void)ID3D12Fence_Release(fence);
    if (allocator != NULL) (void)ID3D12CommandAllocator_Release(allocator);
    if (queue != NULL) (void)ID3D12CommandQueue_Release(queue);
    if (device != NULL) (void)ID3D12Device_Release(device);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    ID3D12Device* device = NULL;
    HRESULT result = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device,
                                       (void**)&device);
    if (FAILED(result) || device == NULL) ExitProcess(1U);

    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 0U;

    ID3D12CommandQueue* queue = NULL;
    result = ID3D12Device_CreateCommandQueue(
        device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&queue);
    if (FAILED(result) || queue == NULL) {
        release_resources(device, NULL, NULL, NULL, NULL);
        ExitProcess(2U);
    }

    ID3D12CommandAllocator* allocator = NULL;
    result = ID3D12Device_CreateCommandAllocator(
        device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
        (void**)&allocator);
    if (FAILED(result) || allocator == NULL) {
        release_resources(device, queue, NULL, NULL, NULL);
        ExitProcess(3U);
    }

    ID3D12GraphicsCommandList* command_list = NULL;
    result = ID3D12Device_CreateCommandList(
        device, 0U, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
        &IID_ID3D12GraphicsCommandList, (void**)&command_list);
    if (FAILED(result) || command_list == NULL) {
        release_resources(device, queue, allocator, NULL, NULL);
        ExitProcess(4U);
    }
    result = ID3D12GraphicsCommandList_Close(command_list);
    if (FAILED(result)) {
        release_resources(device, queue, allocator, command_list, NULL);
        ExitProcess(5U);
    }

    ID3D12Fence* fence = NULL;
    result = ID3D12Device_CreateFence(device, 0U, D3D12_FENCE_FLAG_NONE,
                                      &IID_ID3D12Fence, (void**)&fence);
    if (FAILED(result) || fence == NULL) {
        release_resources(device, queue, allocator, command_list, NULL);
        ExitProcess(6U);
    }
    result = ID3D12CommandQueue_Signal(queue, fence, 1U);
    release_resources(device, queue, allocator, command_list, fence);
    if (FAILED(result)) ExitProcess(7U);

    write_message(kSuccessMessage, (DWORD)(sizeof(kSuccessMessage) - 1U));
    ExitProcess(0U);
}
