#include "tradutorlinux/runtime/ole32.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/win32/kernel32.hpp"
#include "../../core/runtime_state_common.hpp"
#include "../../core/runtime_memory_state.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_set>

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"

namespace tradutorlinux {

namespace {

constexpr std::int32_t kStgENotImplemented = static_cast<std::int32_t>(0x80004001U);
constexpr std::size_t kMaxStreams = 64;

struct Win32Guid {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::uint8_t data4[8];
};

struct StreamState {
    GuestIStream interface{};
    std::uint8_t* data{nullptr};
    std::size_t size{0};
    std::size_t capacity{0};
    std::size_t position{0};
    std::uint32_t references{1};
    void* global_handle{nullptr};
    bool global_backing{false};
    bool delete_global_on_release{false};
};

std::array<StreamState*, kMaxStreams> g_streams{};
std::mutex g_streams_mutex;
std::unordered_set<void*> g_task_allocations;
std::mutex g_task_allocations_mutex;

void trace_stream(const char* operation, const char* status) noexcept {
    const std::array<diagnostics::TraceField, 3> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"mechanism", "memory"},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, "ole-stream", fields);
}

StreamState* find_stream(GuestIStream* const stream) noexcept {
    if (stream == nullptr) return nullptr;
    std::lock_guard<std::mutex> lock(g_streams_mutex);
    for (StreamState* candidate : g_streams) {
        if (candidate != nullptr && &candidate->interface == stream) return candidate;
    }
    return nullptr;
}

bool register_stream(StreamState* const stream) noexcept {
    std::lock_guard<std::mutex> lock(g_streams_mutex);
    for (StreamState*& candidate : g_streams) {
        if (candidate == nullptr) {
            candidate = stream;
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<GlobalMemorySlot> global_memory_snapshot(
    const void* const handle) noexcept {
    if (handle == nullptr) {
        return std::nullopt;
    }
    std::lock_guard lock(g_global_memory_mutex);
    for (const GlobalMemorySlot& slot : g_global_memory) {
        if (slot.used && slot.global && slot.address == handle) {
            return slot;
        }
    }
    return std::nullopt;
}

bool resize_stream(StreamState& stream, const std::size_t size) noexcept {
    if (size > std::numeric_limits<std::size_t>::max() - 4095U) return false;
    if (size > stream.capacity) {
        if (stream.global_backing) {
            return false;
        }
        const std::size_t capacity = (size + 4095U) & ~static_cast<std::size_t>(4095U);
        void* resized = std::realloc(stream.data, capacity);
        if (resized == nullptr) return false;
        stream.data = static_cast<std::uint8_t*>(resized);
        stream.capacity = capacity;
    }
    if (size > stream.size) {
        std::memset(stream.data + stream.size, 0, size - stream.size);
    }
    stream.size = size;
    if (stream.position > size) stream.position = size;
    return true;
}

TL_OLE_MSABI std::int32_t stream_query_interface(GuestIStream* self, const void* riid,
                                                  GuestIStream** object) noexcept;
TL_OLE_MSABI std::uint32_t stream_add_ref(GuestIStream* self) noexcept;
TL_OLE_MSABI std::uint32_t stream_release(GuestIStream* self) noexcept;
TL_OLE_MSABI std::int32_t stream_read(GuestIStream* self, void* buffer, std::uint32_t bytes,
                                       std::uint32_t* read) noexcept;
TL_OLE_MSABI std::int32_t stream_write(GuestIStream* self, const void* buffer, std::uint32_t bytes,
                                        std::uint32_t* written) noexcept;
TL_OLE_MSABI std::int32_t stream_seek(GuestIStream* self, std::int64_t move, std::uint32_t origin,
                                       std::uint64_t* position) noexcept;
TL_OLE_MSABI std::int32_t stream_set_size(GuestIStream* self, std::uint64_t size) noexcept;
TL_OLE_MSABI std::int32_t stream_copy_to(GuestIStream* self, GuestIStream* destination,
                                          std::uint64_t bytes, std::uint64_t* read,
                                          std::uint64_t* written) noexcept;
TL_OLE_MSABI std::int32_t stream_commit(GuestIStream* self, std::uint32_t flags) noexcept;
TL_OLE_MSABI std::int32_t stream_revert(GuestIStream* self) noexcept;
TL_OLE_MSABI std::int32_t stream_lock_region(GuestIStream* self, std::uint64_t offset,
                                               std::uint64_t bytes, std::uint32_t type) noexcept;
TL_OLE_MSABI std::int32_t stream_unlock_region(GuestIStream* self, std::uint64_t offset,
                                                 std::uint64_t bytes, std::uint32_t type) noexcept;
TL_OLE_MSABI std::int32_t stream_stat(GuestIStream* self, GuestStatStg* stat,
                                       std::uint32_t flags) noexcept;
TL_OLE_MSABI std::int32_t stream_clone(GuestIStream* self, GuestIStream** clone) noexcept;

const GuestIStreamVtable kStreamVtable{
    &stream_query_interface,
    &stream_add_ref,
    &stream_release,
    &stream_read,
    &stream_write,
    &stream_seek,
    &stream_set_size,
    &stream_copy_to,
    &stream_commit,
    &stream_revert,
    &stream_lock_region,
    &stream_unlock_region,
    &stream_stat,
    &stream_clone,
};

inline bool mapped_range(const void* address, std::size_t size, bool writable) noexcept;

constexpr std::array<std::uint8_t, 16> kIidIUnknown{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
};
constexpr std::array<std::uint8_t, 16> kIidIStream{
    0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
};

TL_OLE_MSABI std::int32_t stream_query_interface(GuestIStream* const self, const void* const riid,
                                                  GuestIStream** const object) noexcept {
    if (find_stream(self) == nullptr || riid == nullptr || object == nullptr ||
        !mapped_range(riid, kIidIUnknown.size(), false) ||
        !mapped_range(object, sizeof(*object), true)) {
        trace_stream("query-interface", "invalid");
        return kEInvalidArg;
    }
    *object = nullptr;
    const auto* id = static_cast<const std::uint8_t*>(riid);
    const bool supported = std::memcmp(id, kIidIUnknown.data(), kIidIUnknown.size()) == 0 ||
                           std::memcmp(id, kIidIStream.data(), kIidIStream.size()) == 0;
    if (!supported) {
        trace_stream("query-interface", "no-interface");
        return kENoInterface;
    }
    (void)stream_add_ref(self);
    *object = self;
    trace_stream("query-interface", "success");
    return kSOk;
}

TL_OLE_MSABI std::uint32_t stream_add_ref(GuestIStream* const self) noexcept {
    StreamState* const stream = find_stream(self);
    if (stream == nullptr) return 0;
    std::lock_guard<std::mutex> lock(g_streams_mutex);
    if (stream->references == std::numeric_limits<std::uint32_t>::max()) return stream->references;
    return ++stream->references;
}

TL_OLE_MSABI std::uint32_t stream_release(GuestIStream* const self) noexcept {
    StreamState* const stream = find_stream(self);
    if (stream == nullptr) return 0;
    std::uint32_t references = 0;
    {
        std::lock_guard<std::mutex> lock(g_streams_mutex);
        if (stream->references == 0) return 0;
        references = --stream->references;
        if (references != 0) return references;
        for (StreamState*& candidate : g_streams) {
            if (candidate == stream) {
                candidate = nullptr;
                break;
            }
        }
    }
    if (stream->global_backing) {
        if (stream->delete_global_on_release) {
            (void)tl_GlobalFree(stream->global_handle);
        }
    } else {
        std::free(stream->data);
    }
    std::free(stream);
    trace_stream("release", "destroyed");
    return 0;
}

TL_OLE_MSABI std::int32_t stream_read(GuestIStream* const self, void* const buffer,
                                       const std::uint32_t bytes, std::uint32_t* const read) noexcept {
    StreamState* const stream = find_stream(self);
    if (stream == nullptr || (read != nullptr && !mapped_range(read, sizeof(*read), true)) ||
        (bytes != 0 && (buffer == nullptr || !mapped_range(buffer, bytes, true)))) {
        trace_stream("read", "invalid");
        return kEInvalidArg;
    }
    const std::size_t available = stream->size - stream->position;
    const std::size_t count = std::min<std::size_t>(available, bytes);
    if (count != 0) std::memcpy(buffer, stream->data + stream->position, count);
    stream->position += count;
    if (read != nullptr) *read = static_cast<std::uint32_t>(count);
    trace_stream("read", count == bytes ? "success" : "eof");
    return count == bytes ? kSOk : kSFalse;
}

TL_OLE_MSABI std::int32_t stream_write(GuestIStream* const self, const void* const buffer,
                                        const std::uint32_t bytes, std::uint32_t* const written) noexcept {
    StreamState* const stream = find_stream(self);
    if (stream == nullptr || (written != nullptr && !mapped_range(written, sizeof(*written), true)) ||
        (bytes != 0 && (buffer == nullptr || !mapped_range(buffer, bytes, false)))) {
        trace_stream("write", "invalid");
        return kEInvalidArg;
    }
    if (static_cast<std::uint64_t>(bytes) > std::numeric_limits<std::size_t>::max() - stream->position) {
        trace_stream("write", "too-large");
        return static_cast<std::int32_t>(0x8007000EU);
    }
    const std::size_t end = stream->position + bytes;
    if (!resize_stream(*stream, end)) {
        trace_stream("write", "out-of-memory");
        return static_cast<std::int32_t>(0x8007000EU);
    }
    if (bytes != 0) std::memcpy(stream->data + stream->position, buffer, bytes);
    stream->position = end;
    if (written != nullptr) *written = bytes;
    trace_stream("write", "success");
    return kSOk;
}

TL_OLE_MSABI std::int32_t stream_seek(GuestIStream* const self, const std::int64_t move,
                                       const std::uint32_t origin, std::uint64_t* const position) noexcept {
    StreamState* const stream = find_stream(self);
    if (stream == nullptr || (position != nullptr && !mapped_range(position, sizeof(*position), true))) {
        trace_stream("seek", "invalid");
        return kEInvalidArg;
    }
    if (origin > 2U) {
        trace_stream("seek", "invalid-origin");
        return kEInvalidArg;
    }
    const std::int64_t base = origin == 0U ? 0 :
                              origin == 1U ? static_cast<std::int64_t>(stream->position) :
                                             static_cast<std::int64_t>(stream->size);
    if ((move < 0 && (move == std::numeric_limits<std::int64_t>::min() || base < -move)) ||
        (move > 0 && base > std::numeric_limits<std::int64_t>::max() - move)) {
        trace_stream("seek", "range-error");
        return kStgESeekError;
    }
    const std::int64_t target = base + move;
    if (target < 0) {
        trace_stream("seek", "range-error");
        return kStgESeekError;
    }
    stream->position = static_cast<std::size_t>(target);
    if (position != nullptr) *position = static_cast<std::uint64_t>(stream->position);
    trace_stream("seek", "success");
    return kSOk;
}

TL_OLE_MSABI std::int32_t stream_set_size(GuestIStream* const self, const std::uint64_t size) noexcept {
    StreamState* const stream = find_stream(self);
    if (stream == nullptr || size > std::numeric_limits<std::size_t>::max()) {
        trace_stream("set-size", "invalid");
        return kEInvalidArg;
    }
    if (!resize_stream(*stream, static_cast<std::size_t>(size))) {
        trace_stream("set-size", "out-of-memory");
        return static_cast<std::int32_t>(0x8007000EU);
    }
    trace_stream("set-size", "success");
    return kSOk;
}

TL_OLE_MSABI std::int32_t stream_copy_to(GuestIStream* const self, GuestIStream* const destination,
                                          const std::uint64_t bytes, std::uint64_t* const read,
                                          std::uint64_t* const written) noexcept {
    (void)destination;
    (void)bytes;
    (void)read;
    (void)written;
    if (find_stream(self) == nullptr) return kEInvalidArg;
    return kStgENotImplemented;
}

TL_OLE_MSABI std::int32_t stream_commit(GuestIStream* const self, const std::uint32_t flags) noexcept {
    if (find_stream(self) == nullptr || flags != 0U) return kEInvalidArg;
    return kSOk;
}

TL_OLE_MSABI std::int32_t stream_revert(GuestIStream* const self) noexcept {
    return find_stream(self) == nullptr ? kEInvalidArg : kSOk;
}

TL_OLE_MSABI std::int32_t stream_lock_region(GuestIStream* const self, const std::uint64_t offset,
                                               const std::uint64_t bytes, const std::uint32_t type) noexcept {
    (void)offset;
    (void)bytes;
    (void)type;
    return find_stream(self) == nullptr ? kEInvalidArg : kStgEInvalidFunction;
}

TL_OLE_MSABI std::int32_t stream_unlock_region(GuestIStream* const self, const std::uint64_t offset,
                                                 const std::uint64_t bytes, const std::uint32_t type) noexcept {
    (void)offset;
    (void)bytes;
    (void)type;
    return find_stream(self) == nullptr ? kEInvalidArg : kStgEInvalidFunction;
}

TL_OLE_MSABI std::int32_t stream_stat(GuestIStream* const self, GuestStatStg* const stat,
                                       const std::uint32_t flags) noexcept {
    StreamState* const stream = find_stream(self);
    if (stream == nullptr || stat == nullptr || !mapped_range(stat, sizeof(*stat), true) || flags != 0U) {
        trace_stream("stat", "invalid");
        return kEInvalidArg;
    }
    *stat = GuestStatStg{};
    stat->type = 2U;
    stat->cb_size = static_cast<std::uint64_t>(stream->size);
    trace_stream("stat", "success");
    return kSOk;
}

TL_OLE_MSABI std::int32_t stream_clone(GuestIStream* const self, GuestIStream** const clone) noexcept {
    if (find_stream(self) == nullptr || clone == nullptr || !mapped_range(clone, sizeof(*clone), true)) {
        return kEInvalidArg;
    }
    *clone = nullptr;
    return kStgENotImplemented;
}

inline bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

}  // namespace

extern "C" {

TL_OLE_MSABI std::int32_t tl_CoInitialize(void* reserved) noexcept {
    (void)reserved;
    return kSOk;
}

TL_OLE_MSABI std::int32_t tl_CoInitializeEx(void* reserved, const std::uint32_t co_init) noexcept {
    (void)reserved;
    (void)co_init;
    return kSOk;
}

TL_OLE_MSABI void tl_CoUninitialize() noexcept {
}

TL_OLE_MSABI std::int32_t tl_CoCreateGuid(void* guid) noexcept {
    if (guid == nullptr || !mapped_range(guid, sizeof(Win32Guid), true)) {
        return kEInvalidArg;
    }
    auto* out = static_cast<Win32Guid*>(guid);
    std::ifstream urandom{"/dev/urandom", std::ios::binary};
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(out), sizeof(Win32Guid));
    } else {
        std::uint8_t* raw = reinterpret_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < sizeof(Win32Guid); ++i) {
            raw[i] = static_cast<std::uint8_t>(std::rand() & 0xFF);
        }
    }
    // UUID v4 format
    out->data3 = (out->data3 & 0x0FFFU) | 0x4000U;
    out->data4[0] = (out->data4[0] & 0x3FU) | 0x80U;
    return kSOk;
}

TL_OLE_MSABI void* tl_CoTaskMemAlloc(const std::size_t size) noexcept {
    if (size == 0) {
        return nullptr;
    }
    void* const pointer = std::malloc(size);
    if (pointer != nullptr) {
        std::lock_guard lock(g_task_allocations_mutex);
        g_task_allocations.insert(pointer);
    }
    return pointer;
}

TL_OLE_MSABI void tl_CoTaskMemFree(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    bool owned = false;
    {
        std::lock_guard lock(g_task_allocations_mutex);
        owned = g_task_allocations.erase(ptr) != 0;
    }
    if (owned) {
        std::free(ptr);
    } else {
        trace_stream("CoTaskMemFree", "ignored-unowned-pointer");
    }
}

TL_OLE_MSABI void* tl_CoTaskMemRealloc(void* ptr, const std::size_t size) noexcept {
    if (ptr == nullptr) {
        return tl_CoTaskMemAlloc(size);
    }
    if (size == 0) {
        tl_CoTaskMemFree(ptr);
        return nullptr;
    }
    {
        std::lock_guard lock(g_task_allocations_mutex);
        if (!g_task_allocations.contains(ptr)) {
            trace_stream("CoTaskMemRealloc", "ignored-unowned-pointer");
            return nullptr;
        }
    }
    void* const replacement = std::realloc(ptr, size);
    if (replacement != nullptr && replacement != ptr) {
        std::lock_guard lock(g_task_allocations_mutex);
        g_task_allocations.erase(ptr);
        g_task_allocations.insert(replacement);
    }
    return replacement;
}

namespace {

TL_OLE_MSABI std::int32_t imalloc_query_interface(GuestIMalloc*, const void*, void** object) noexcept {
    if (object == nullptr) {
        return kEInvalidArg;
    }
    *object = &g_guest_imalloc;
    return kSOk;
}

TL_OLE_MSABI std::uint32_t imalloc_add_ref(GuestIMalloc*) noexcept {
    return 1;
}

TL_OLE_MSABI std::uint32_t imalloc_release(GuestIMalloc*) noexcept {
    return 1;
}

TL_OLE_MSABI void* imalloc_alloc(GuestIMalloc*, const std::size_t cb) noexcept {
    return tl_CoTaskMemAlloc(cb);
}

TL_OLE_MSABI void* imalloc_realloc(GuestIMalloc*, void* const pv, const std::size_t cb) noexcept {
    return tl_CoTaskMemRealloc(pv, cb);
}

TL_OLE_MSABI void imalloc_free(GuestIMalloc*, void* const pv) noexcept {
    tl_CoTaskMemFree(pv);
}

TL_OLE_MSABI std::size_t imalloc_get_size(GuestIMalloc*, void*) noexcept {
    return 0;
}

TL_OLE_MSABI std::int32_t imalloc_did_alloc(GuestIMalloc*, void*) noexcept {
    return -1;
}

TL_OLE_MSABI void imalloc_heap_minimize(GuestIMalloc*) noexcept {}

const GuestIMallocVtable g_imalloc_vtable = {
    imalloc_query_interface,
    imalloc_add_ref,
    imalloc_release,
    imalloc_alloc,
    imalloc_realloc,
    imalloc_free,
    imalloc_get_size,
    imalloc_did_alloc,
    imalloc_heap_minimize,
};

}  // namespace

GuestIMalloc g_guest_imalloc = { &g_imalloc_vtable };

TL_OLE_MSABI std::int32_t tl_CoGetMalloc(const std::uint32_t context, void** const pp_malloc) noexcept {
    (void)context;
    if (pp_malloc == nullptr || !mapped_range(pp_malloc, sizeof(void*), true)) {
        return kEInvalidArg;
    }
    *pp_malloc = &g_guest_imalloc;
    return kSOk;
}

TL_OLE_MSABI std::int32_t tl_CreateStreamOnHGlobal(const OleHGlobal hglobal,
                                                   const std::int32_t delete_on_release,
                                                   GuestIStream** stream) noexcept {
    if (stream == nullptr || !mapped_range(stream, sizeof(*stream), true)) {
        trace_stream("create", "invalid");
        return kEInvalidArg;
    }
    const std::optional<GlobalMemorySlot> global = global_memory_snapshot(hglobal);
    if (hglobal != nullptr && !global.has_value()) {
        trace_stream("create", "invalid");
        return kEInvalidArg;
    }
    auto* state = static_cast<StreamState*>(std::calloc(1, sizeof(StreamState)));
    if (state == nullptr || !register_stream(state)) {
        std::free(state);
        trace_stream("create", "failed");
        return static_cast<std::int32_t>(0x8007000EU);
    }
    state->references = 1;
    state->interface.vtable = const_cast<GuestIStreamVtable*>(&kStreamVtable);
    if (global.has_value()) {
        state->data = static_cast<std::uint8_t*>(global->address);
        state->size = global->size;
        state->capacity = global->size;
        state->global_handle = hglobal;
        state->global_backing = true;
        state->delete_global_on_release = delete_on_release != 0;
    }
    *stream = &state->interface;
    trace_stream("create", "success");
    return kSOk;
}

TL_OLE_MSABI std::int32_t tl_CoCreateInstance(const void* rclsid, void* unkOuter, const std::uint32_t clsContext,
                                              const void* riid, void** ppv) noexcept {
    (void)clsContext;
    if (rclsid == nullptr || riid == nullptr || ppv == nullptr ||
        !mapped_range(rclsid, sizeof(Win32Guid), false) || !mapped_range(riid, sizeof(Win32Guid), false) ||
        !mapped_range(ppv, sizeof(void*), true)) {
        return kEInvalidArg;
    }
    if (unkOuter != nullptr) {
        return static_cast<std::int32_t>(0x80040110U); // CLASS_E_NOAGGREGATION
    }
    *ppv = nullptr;
    return static_cast<std::int32_t>(0x80040154U); // REGDB_E_CLASSNOTREG
}

TL_OLE_MSABI std::int32_t tl_CoGetClassObject(const void* rclsid, const std::uint32_t clsContext,
                                              void* serverInfo, const void* riid, void** ppv) noexcept {
    (void)serverInfo;
    return tl_CoCreateInstance(rclsid, nullptr, clsContext, riid, ppv);
}

TL_OLE_MSABI std::int32_t tl_OleInitialize(void* reserved) noexcept {
    (void)reserved;
    return kSOk;
}

TL_OLE_MSABI void tl_OleUninitialize() noexcept {
}

TL_OLE_MSABI std::int32_t tl_CLSIDFromString(const std::uint16_t* lpsz, void* pclsid) noexcept {
    if (pclsid == nullptr) {
        return kEInvalidArg;
    }
    std::memset(pclsid, 0, 16);
    if (lpsz == nullptr) {
        return kSOk;
    }
    return kSOk;
}

TL_OLE_MSABI std::int32_t tl_RegisterDragDrop(void* const hwnd, void* const drop_target) noexcept {
    (void)hwnd;
    (void)drop_target;
    return kSOk;
}

TL_OLE_MSABI std::int32_t tl_RevokeDragDrop(void* const hwnd) noexcept {
    (void)hwnd;
    return kSOk;
}

TL_OLE_MSABI std::int32_t tl_DoDragDrop(void* const data_obj, void* const drop_source,
                                       const std::uint32_t ok_effects, std::uint32_t* const effect) noexcept {
    (void)data_obj;
    (void)drop_source;
    (void)ok_effects;
    if (effect != nullptr) {
        *effect = 0; // DROPEFFECT_NONE
    }
    return 0x00040100; // DRAGDROP_S_CANCEL
}

TL_OLE_MSABI void tl_ReleaseStgMedium(void* const medium) noexcept {
    (void)medium;
}

TL_OLE_MSABI int tl_StringFromGUID2(const void* const rguid, wchar_t* const lpsz, const int cchMax) noexcept {
    if (rguid == nullptr || lpsz == nullptr || cchMax < 39 || !mapped_range(lpsz, static_cast<std::size_t>(cchMax) * sizeof(wchar_t), true)) {
        return 0;
    }
    const wchar_t dummy_guid[] = L"{00000000-0000-0000-0000-000000000000}";
    std::memcpy(lpsz, dummy_guid, sizeof(dummy_guid));
    return 39;
}

TL_OLE_MSABI int tl_CLSIDFromProgID(const wchar_t* const lpszProgID, void* const lpclsid) noexcept {
    (void)lpszProgID;
    if (lpclsid != nullptr && mapped_range(lpclsid, 16, true)) {
        std::memset(lpclsid, 0, 16);
    }
    return 0; // S_OK
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_ole32_module() {
    static const ExportedFunction kOle32Exports[] = {
        {"CoInitialize", 1, reinterpret_cast<std::uintptr_t>(&tl_CoInitialize)},
        {"CoInitializeEx", 2, reinterpret_cast<std::uintptr_t>(&tl_CoInitializeEx)},
        {"CoUninitialize", 3, reinterpret_cast<std::uintptr_t>(&tl_CoUninitialize)},
        {"CoCreateGuid", 4, reinterpret_cast<std::uintptr_t>(&tl_CoCreateGuid)},
        {"CoTaskMemAlloc", 5, reinterpret_cast<std::uintptr_t>(&tl_CoTaskMemAlloc)},
        {"CoTaskMemFree", 6, reinterpret_cast<std::uintptr_t>(&tl_CoTaskMemFree)},
        {"CoTaskMemRealloc", 7, reinterpret_cast<std::uintptr_t>(&tl_CoTaskMemRealloc)},
        {"CreateStreamOnHGlobal", 12, reinterpret_cast<std::uintptr_t>(&tl_CreateStreamOnHGlobal)},
        {"CoCreateInstance", 8, reinterpret_cast<std::uintptr_t>(&tl_CoCreateInstance)},
        {"CoGetClassObject", 9, reinterpret_cast<std::uintptr_t>(&tl_CoGetClassObject)},
        {"OleInitialize", 10, reinterpret_cast<std::uintptr_t>(&tl_OleInitialize)},
        {"OleUninitialize", 11, reinterpret_cast<std::uintptr_t>(&tl_OleUninitialize)},
        {"CLSIDFromString", 13, reinterpret_cast<std::uintptr_t>(&tl_CLSIDFromString)},
        {"RegisterDragDrop", 14, reinterpret_cast<std::uintptr_t>(&tl_RegisterDragDrop)},
        {"RevokeDragDrop", 15, reinterpret_cast<std::uintptr_t>(&tl_RevokeDragDrop)},
        {"DoDragDrop", 16, reinterpret_cast<std::uintptr_t>(&tl_DoDragDrop)},
        {"ReleaseStgMedium", 17, reinterpret_cast<std::uintptr_t>(&tl_ReleaseStgMedium)},
        {"StringFromGUID2", 18, reinterpret_cast<std::uintptr_t>(&tl_StringFromGUID2)},
        {"CLSIDFromProgID", 19, reinterpret_cast<std::uintptr_t>(&tl_CLSIDFromProgID)},
        {"CoGetMalloc", 20, reinterpret_cast<std::uintptr_t>(&tl_CoGetMalloc)},
    };
    static const InternalModule kOle32Module{"ole32.dll", kOle32Exports};
    register_module(kOle32Module);
}

}  // namespace tradutorlinux::loader
