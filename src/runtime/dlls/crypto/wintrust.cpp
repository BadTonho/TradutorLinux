#include "tradutorlinux/runtime/wintrust.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/builtin_modules.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace tradutorlinux {
namespace {

using OpenSslX509 = void;
using OpenSslStore = void;
using OpenSslStoreContext = void;

constexpr std::size_t kMaxCertificateSize = 1024U * 1024U;
constexpr std::size_t kMaxPayloadSize = 2U * kMaxCertificateSize + 32U;
constexpr std::array<std::uint8_t, 16> kGenericVerifyV2{
    0x6B, 0xC5, 0xAA, 0x00, 0x44, 0xCD, 0xD0, 0x11,
    0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE,
};
constexpr std::array<std::uint8_t, 4> kPayloadMagic{'T', 'L', 'T', 'C'};

void trace_trust(const char* const operation, const char* const status,
                 const char* const policy, const char* const mechanism) noexcept {
    const std::array<diagnostics::TraceField, 4> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"policy", policy},
        diagnostics::TraceField{"mechanism", mechanism},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, "wintrust", fields);
}

struct OpenSslApi {
    using D2iX509 = OpenSslX509* (*)(OpenSslX509**, const unsigned char**, long);
    using X509Free = void (*)(OpenSslX509*);
    using StoreNew = OpenSslStore* (*)();
    using StoreFree = void (*)(OpenSslStore*);
    using StoreAddCert = int (*)(OpenSslStore*, OpenSslX509*);
    using StoreContextNew = OpenSslStoreContext* (*)();
    using StoreContextFree = void (*)(OpenSslStoreContext*);
    using StoreContextInit = int (*)(OpenSslStoreContext*, OpenSslStore*, OpenSslX509*, void*);
    using VerifyCertificate = int (*)(OpenSslStoreContext*);

    void* library{nullptr};
    D2iX509 d2i_x509{nullptr};
    X509Free x509_free{nullptr};
    StoreNew store_new{nullptr};
    StoreFree store_free{nullptr};
    StoreAddCert store_add_cert{nullptr};
    StoreContextNew store_context_new{nullptr};
    StoreContextFree store_context_free{nullptr};
    StoreContextInit store_context_init{nullptr};
    VerifyCertificate verify_certificate{nullptr};
    bool initialized{false};
    std::mutex mutex;

    ~OpenSslApi() {
        if (library != nullptr) dlclose(library);
    }

    template <typename T>
    bool resolve(T& destination, const char* const name) noexcept {
        void* symbol = dlsym(library, name);
        if (symbol == nullptr) return false;
        destination = reinterpret_cast<T>(symbol);
        return true;
    }

    bool ensure() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if (initialized) return true;
        library = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) library = dlopen("libcrypto.so", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr ||
            !resolve(d2i_x509, "d2i_X509") ||
            !resolve(x509_free, "X509_free") ||
            !resolve(store_new, "X509_STORE_new") ||
            !resolve(store_free, "X509_STORE_free") ||
            !resolve(store_add_cert, "X509_STORE_add_cert") ||
            !resolve(store_context_new, "X509_STORE_CTX_new") ||
            !resolve(store_context_free, "X509_STORE_CTX_free") ||
            !resolve(store_context_init, "X509_STORE_CTX_init") ||
            !resolve(verify_certificate, "X509_verify_cert")) {
            return false;
        }
        initialized = true;
        return true;
    }
};

OpenSslApi g_openssl;

struct TrustStateSlot {
    bool used{false};
    std::vector<std::vector<std::uint8_t>> certificates;
    GuestCertContext certificate_contexts[2]{};
    GuestWintrustProviderCert provider_certificates[2]{};
    GuestWintrustSigner signer{};
    GuestWintrustProviderData provider_data{};
};

std::mutex g_trust_state_mutex;
std::array<TrustStateSlot, 32> g_trust_states{};

bool read_u32(const std::vector<std::uint8_t>& payload, std::size_t& offset,
              std::uint32_t& value) noexcept {
    if (offset > payload.size() || payload.size() - offset < sizeof(value)) return false;
    std::memcpy(&value, payload.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

bool parse_payload(const std::uint8_t* const source, const std::uint32_t size,
                   std::vector<std::vector<std::uint8_t>>& certificates) noexcept {
    certificates.clear();
    if (source == nullptr || size < 12U || size > kMaxPayloadSize) return false;
    try {
        std::vector<std::uint8_t> payload(size);
        if (runtime::read_guest_memory(source, payload.data(), payload.size()).status !=
            runtime::GuestMemoryAccessStatus::Success) {
            return false;
        }
        if (!std::equal(kPayloadMagic.begin(), kPayloadMagic.end(), payload.begin())) return false;
        std::size_t offset = kPayloadMagic.size();
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (!read_u32(payload, offset, version) || !read_u32(payload, offset, count) ||
            version != 1U || count != 2U) return false;
        for (std::uint32_t index = 0; index < count; ++index) {
            std::uint32_t length = 0;
            if (!read_u32(payload, offset, length) || length == 0U || length > kMaxCertificateSize ||
                offset > payload.size() || payload.size() - offset < length) return false;
            certificates.emplace_back(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                      payload.begin() + static_cast<std::ptrdiff_t>(offset + length));
            offset += length;
        }
        return offset == payload.size();
    } catch (...) {
        certificates.clear();
        return false;
    }
}

std::int32_t verify_payload(const std::vector<std::vector<std::uint8_t>>& certificates) noexcept {
    if (certificates.size() != 2U || !g_openssl.ensure()) return kTrustProviderUnknown;

    const auto parse_certificate = [](const std::vector<std::uint8_t>& der,
                                      OpenSslX509*& certificate) noexcept {
        const unsigned char* cursor = der.data();
        certificate = g_openssl.d2i_x509(nullptr, &cursor, static_cast<long>(der.size()));
        return certificate != nullptr && cursor == der.data() + der.size();
    };

    OpenSslX509* leaf = nullptr;
    OpenSslX509* root = nullptr;
    if (!parse_certificate(certificates[0], leaf) || !parse_certificate(certificates[1], root)) {
        if (leaf != nullptr) g_openssl.x509_free(leaf);
        if (root != nullptr) g_openssl.x509_free(root);
        return kTrustInvalidParameter;
    }

    OpenSslStore* store = g_openssl.store_new();
    OpenSslStoreContext* context = store == nullptr ? nullptr : g_openssl.store_context_new();
    const bool ready = store != nullptr && context != nullptr &&
                       g_openssl.store_add_cert(store, root) == 1 &&
                       g_openssl.store_context_init(context, store, leaf, nullptr) == 1;
    const int verified = ready ? g_openssl.verify_certificate(context) : 0;
    if (context != nullptr) g_openssl.store_context_free(context);
    if (store != nullptr) g_openssl.store_free(store);
    g_openssl.x509_free(leaf);
    g_openssl.x509_free(root);
    return ready && verified == 1 ? kTrustSuccess : kTrustUntrustedRoot;
}

TrustStateSlot* find_state_locked(void* const state_data) noexcept {
    if (state_data == nullptr) return nullptr;
    for (TrustStateSlot& state : g_trust_states) {
        if (state.used && state_data == static_cast<void*>(&state)) return &state;
    }
    return nullptr;
}

TrustStateSlot* find_state_from_provider_locked(
    const GuestWintrustProviderData* const provider_data) noexcept {
    if (provider_data == nullptr) return nullptr;
    for (TrustStateSlot& state : g_trust_states) {
        if (state.used && provider_data == &state.provider_data) return &state;
    }
    return nullptr;
}

TrustStateSlot* find_state_from_signer_locked(
    const GuestWintrustSigner* const signer) noexcept {
    if (signer == nullptr) return nullptr;
    for (TrustStateSlot& state : g_trust_states) {
        if (state.used && signer == &state.signer) return &state;
    }
    return nullptr;
}

TrustStateSlot* create_trust_state(
    const std::vector<std::vector<std::uint8_t>>& certificates,
    GuestWintrustData* const data) noexcept {
    if (certificates.size() != 2U || data == nullptr) return nullptr;
    std::lock_guard<std::mutex> lock(g_trust_state_mutex);
    TrustStateSlot* free_state = nullptr;
    for (TrustStateSlot& state : g_trust_states) {
        if (!state.used) {
            free_state = &state;
            break;
        }
    }
    if (free_state == nullptr) return nullptr;
    try {
        free_state->certificates = certificates;
        free_state->certificate_contexts[0] = GuestCertContext{
            .encoding_type = 1U,
            .encoded = free_state->certificates[0].data(),
            .encoded_size = static_cast<std::uint32_t>(free_state->certificates[0].size()),
            .cert_info = nullptr,
            .cert_store = nullptr,
        };
        free_state->certificate_contexts[1] = GuestCertContext{
            .encoding_type = 1U,
            .encoded = free_state->certificates[1].data(),
            .encoded_size = static_cast<std::uint32_t>(free_state->certificates[1].size()),
            .cert_info = nullptr,
            .cert_store = nullptr,
        };
        free_state->provider_certificates[0] = GuestWintrustProviderCert{
            .cb_struct = sizeof(GuestWintrustProviderCert),
            .cert_context = &free_state->certificate_contexts[0],
        };
        free_state->provider_certificates[1] = GuestWintrustProviderCert{
            .cb_struct = sizeof(GuestWintrustProviderCert),
            .cert_context = &free_state->certificate_contexts[1],
            .trusted_root = 1U,
            .self_signed = 1U,
        };
        free_state->signer = GuestWintrustSigner{
            .cb_struct = sizeof(GuestWintrustSigner),
            .cert_count = 2U,
            .cert_chain = free_state->provider_certificates,
        };
        free_state->provider_data = GuestWintrustProviderData{
            .cb_struct = sizeof(GuestWintrustProviderData),
            .wintrust_data = data,
            .signer_count = 1U,
            .signers = &free_state->signer,
        };
    } catch (...) {
        free_state->certificates.clear();
        return nullptr;
    }
    runtime::invalidate_memory_map_cache();
    free_state->used = true;
    return free_state;
}

bool write_state_data(GuestWintrustData* const data, void* const state_data) noexcept {
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(data);
    if (address > std::numeric_limits<std::uintptr_t>::max() -
                      offsetof(GuestWintrustData, state_data)) {
        return false;
    }
    return runtime::write_guest_memory(
               reinterpret_cast<void*>(address + offsetof(GuestWintrustData, state_data)),
               &state_data, sizeof(state_data)).status == runtime::GuestMemoryAccessStatus::Success;
}

bool close_trust_state(void* const state_data) noexcept {
    std::lock_guard<std::mutex> lock(g_trust_state_mutex);
    TrustStateSlot* const state = find_state_locked(state_data);
    if (state == nullptr) return false;
    state->used = false;
    state->certificates.clear();
    state->certificate_contexts[0] = GuestCertContext{};
    state->certificate_contexts[1] = GuestCertContext{};
    state->provider_certificates[0] = GuestWintrustProviderCert{};
    state->provider_certificates[1] = GuestWintrustProviderCert{};
    state->signer = GuestWintrustSigner{};
    state->provider_data = GuestWintrustProviderData{};
    runtime::invalidate_memory_map_cache();
    return true;
}

}  // namespace

extern "C" {

TL_WINTRUST_MSABI std::int32_t tl_WinVerifyTrust(void* const hwnd, const void* const action,
                                                  GuestWintrustData* const data) noexcept {
    (void)hwnd;
    std::array<std::uint8_t, kGenericVerifyV2.size()> action_copy{};
    GuestWintrustData data_copy{};
    if (action == nullptr || data == nullptr ||
        runtime::read_guest_memory(action, action_copy.data(), action_copy.size()).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        runtime::read_guest_memory(data, &data_copy, sizeof(data_copy)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        action_copy != kGenericVerifyV2 || data_copy.cb_struct != sizeof(GuestWintrustData) ||
        data_copy.policy_callback_data != nullptr || data_copy.sip_client_data != nullptr ||
        data_copy.ui_choice != kWtdUiNone || data_copy.revocation_checks != kWtdRevokeNone ||
        data_copy.union_choice != kWtdChoiceBlob || data_copy.url_reference != nullptr ||
        data_copy.provider_flags != 0U || data_copy.ui_context != 0U ||
        data_copy.signature_settings != nullptr ||
        (data_copy.state_action != kWtdStateActionIgnore &&
         data_copy.state_action != kWtdStateActionVerify &&
         data_copy.state_action != kWtdStateActionClose)) {
        trace_trust("verify", "invalid", "explicit-chain", "openssl");
        return kTrustInvalidParameter;
    }

    if (data_copy.state_action == kWtdStateActionClose) {
        if (data_copy.state_data == nullptr || !close_trust_state(data_copy.state_data)) {
            trace_trust("close", "invalid", "explicit-chain", "state-table");
            return kTrustInvalidParameter;
        }
        if (!write_state_data(data, nullptr)) {
            trace_trust("close", "invalid", "explicit-chain", "guest-memory");
            return kTrustInvalidParameter;
        }
        trace_trust("close", "success", "explicit-chain", "state-table");
        return kTrustSuccess;
    }
    if (data_copy.state_data != nullptr || data_copy.union_data == nullptr) {
        trace_trust("verify", "invalid", "explicit-chain", "openssl");
        return kTrustInvalidParameter;
    }
    GuestWintrustBlobInfo blob_copy{};
    if (runtime::read_guest_memory(data_copy.union_data, &blob_copy, sizeof(blob_copy)).status !=
            runtime::GuestMemoryAccessStatus::Success ||
        blob_copy.cb_struct != sizeof(GuestWintrustBlobInfo) || blob_copy.cb_mem_object == 0U ||
        blob_copy.pb_mem_object == nullptr) {
        trace_trust("verify", "invalid", "explicit-chain", "openssl");
        return kTrustInvalidParameter;
    }
    std::vector<std::vector<std::uint8_t>> certificates;
    if (!parse_payload(blob_copy.pb_mem_object, blob_copy.cb_mem_object, certificates)) {
        trace_trust("verify", "invalid", "explicit-chain", "openssl");
        return kTrustInvalidParameter;
    }
    const std::int32_t result = verify_payload(certificates);
    if (result == kTrustSuccess && data_copy.state_action == kWtdStateActionVerify) {
        TrustStateSlot* const state = create_trust_state(certificates, data);
        if (state == nullptr || !write_state_data(data, state)) {
            if (state != nullptr) close_trust_state(state);
            trace_trust("verify", "unavailable", "explicit-chain", "state-table");
            return kTrustProviderUnknown;
        }
    }
    const char* const status = result == kTrustSuccess ? "success" :
                               result == kTrustInvalidParameter ? "invalid" : "untrusted";
    trace_trust("verify", status,
                "explicit-chain", result == kTrustProviderUnknown ? "unavailable" : "openssl");
    return result;
}

TL_WINTRUST_MSABI GuestWintrustProviderData* tl_WTHelperProvDataFromStateData(
    void* const state_data) noexcept {
    std::lock_guard<std::mutex> lock(g_trust_state_mutex);
    TrustStateSlot* const state = find_state_locked(state_data);
    return state == nullptr ? nullptr : &state->provider_data;
}

TL_WINTRUST_MSABI GuestWintrustSigner* tl_WTHelperGetProvSignerFromChain(
    GuestWintrustProviderData* const provider_data, const std::uint32_t signer_index,
    const std::int32_t counter_signer, const std::uint32_t counter_signer_index) noexcept {
    std::lock_guard<std::mutex> lock(g_trust_state_mutex);
    TrustStateSlot* const state = find_state_from_provider_locked(provider_data);
    if (state == nullptr || signer_index != 0U || counter_signer != 0 ||
        counter_signer_index != 0U) {
        return nullptr;
    }
    return &state->signer;
}

TL_WINTRUST_MSABI GuestWintrustProviderCert* tl_WTHelperGetProvCertFromChain(
    GuestWintrustSigner* const signer, const std::uint32_t cert_index) noexcept {
    std::lock_guard<std::mutex> lock(g_trust_state_mutex);
    TrustStateSlot* const state = find_state_from_signer_locked(signer);
    if (state == nullptr || cert_index >= 2U) return nullptr;
    return &state->provider_certificates[cert_index];
}

}  // extern "C"
}  // namespace tradutorlinux

namespace tradutorlinux::loader {

void register_wintrust_module() {
    static const ExportedFunction kWintrustExports[] = {
        {"WinVerifyTrust", 1, reinterpret_cast<std::uintptr_t>(&tl_WinVerifyTrust), ExportSupport::Full},
        {"WTHelperProvDataFromStateData", 2,
         reinterpret_cast<std::uintptr_t>(&tl_WTHelperProvDataFromStateData), ExportSupport::Full},
        {"WTHelperGetProvSignerFromChain", 3,
         reinterpret_cast<std::uintptr_t>(&tl_WTHelperGetProvSignerFromChain), ExportSupport::Full},
        {"WTHelperGetProvCertFromChain", 4,
         reinterpret_cast<std::uintptr_t>(&tl_WTHelperGetProvCertFromChain), ExportSupport::Full},
    };
    static const InternalModule kWintrustModule{"WINTRUST.dll", kWintrustExports};
    register_module(kWintrustModule);
}

}  // namespace tradutorlinux::loader
