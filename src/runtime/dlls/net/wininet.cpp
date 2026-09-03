#include "tradutorlinux/runtime/wininet.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/memory_validator.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradutorlinux {
namespace {

constexpr std::uintptr_t kInternetHandleBase = 0x0000B10000000000ULL;
constexpr std::size_t kMaxInternetHandles = 256;
constexpr std::size_t kMaxUrlLength = 4096;
constexpr std::size_t kMaxHeaderLength = 16U * 1024U;
constexpr std::size_t kMaxBodyLength = 16U * 1024U * 1024U;
constexpr std::uint32_t kInternetSchemeHttps = 2;
constexpr std::size_t kNpos = std::numeric_limits<std::size_t>::max();

// Valores da ABI C de libcurl. Os headers de desenvolvimento não são
// necessários: o backend resolve apenas a superfície utilizada abaixo.
constexpr int kCurlOptUrl = 10002;
constexpr int kCurlOptPort = 3;
constexpr int kCurlOptUserAgent = 10018;
constexpr int kCurlOptHttpHeader = 10023;
constexpr int kCurlOptWriteFunction = 20011;
constexpr int kCurlOptWriteData = 10001;
constexpr int kCurlOptHeaderFunction = 20079;
constexpr int kCurlOptHeaderData = 10029;
constexpr int kCurlOptCustomRequest = 10036;
constexpr int kCurlOptPostFields = 10015;
constexpr int kCurlOptPostFieldSize = 60;
constexpr int kCurlOptNobody = 44;
constexpr int kCurlOptNoSignal = 99;
constexpr int kCurlOptConnectTimeoutMs = 156;
constexpr int kCurlOptTimeoutMs = 155;
constexpr int kCurlOptSslVerifyPeer = 64;
constexpr int kCurlOptSslVerifyHost = 81;
constexpr int kCurlOptCaInfo = 10065;
constexpr int kCurlOptFollowLocation = 52;
constexpr int kCurlOptProxy = 10004;
constexpr int kCurlOptNoProxy = 10177;
constexpr int kCurlInfoResponseCode = 0x200002;

constexpr int kCurlOk = 0;
constexpr int kCurlCouldntResolveHost = 6;
constexpr int kCurlCouldntConnect = 7;
constexpr int kCurlOperationTimedout = 28;
constexpr int kCurlSslConnectError = 35;
constexpr int kCurlPeerFailedVerification = 51;
constexpr int kCurlSslCertProblem = 58;
constexpr int kCurlSslCa = 60;

using CurlEasy = void;
using CurlSlist = void;
using CurlWriteCallback = std::size_t (*)(char*, std::size_t, std::size_t, void*);

struct CurlApi {
    using GlobalInit = int (*)(long);
    using EasyInit = CurlEasy* (*)();
    using EasyCleanup = void (*)(CurlEasy*);
    using EasyPerform = int (*)(CurlEasy*);
    using EasyGetinfo = int (*)(CurlEasy*, int, ...);
    using EasySetopt = int (*)(CurlEasy*, int, ...);
    using SlistAppend = CurlSlist* (*)(CurlSlist*, const char*);
    using SlistFreeAll = void (*)(CurlSlist*);

    void* library{nullptr};
    GlobalInit global_init{nullptr};
    EasyInit easy_init{nullptr};
    EasyCleanup easy_cleanup{nullptr};
    EasyPerform easy_perform{nullptr};
    EasyGetinfo easy_getinfo{nullptr};
    EasySetopt easy_setopt{nullptr};
    SlistAppend slist_append{nullptr};
    SlistFreeAll slist_free_all{nullptr};
    bool initialized{false};
    std::mutex mutex;

    ~CurlApi() {
        if (library != nullptr) {
            dlclose(library);
        }
    }

    template <typename T>
    bool resolve(T& destination, const char* name) noexcept {
        void* symbol = dlsym(library, name);
        if (symbol == nullptr) {
            return false;
        }
        destination = reinterpret_cast<T>(symbol);
        return true;
    }

    bool ensure() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if (initialized) {
            return true;
        }
        if (library == nullptr) {
            library = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
            if (library == nullptr) {
                library = dlopen("libcurl.so", RTLD_NOW | RTLD_LOCAL);
            }
        }
        if (library == nullptr ||
            !resolve(global_init, "curl_global_init") ||
            !resolve(easy_init, "curl_easy_init") ||
            !resolve(easy_cleanup, "curl_easy_cleanup") ||
            !resolve(easy_perform, "curl_easy_perform") ||
            !resolve(easy_getinfo, "curl_easy_getinfo") ||
            !resolve(easy_setopt, "curl_easy_setopt") ||
            !resolve(slist_append, "curl_slist_append") ||
            !resolve(slist_free_all, "curl_slist_free_all")) {
            return false;
        }
        if (global_init(3L) != kCurlOk) {
            return false;
        }
        initialized = true;
        return true;
    }
};

CurlApi g_curl;

enum class HandleKind : std::uint8_t {
    Session,
    Connection,
    Request,
};

struct Header {
    std::string name;
    std::string value;
};

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::uint16_t port{443};
    std::string path{"/"};
    std::string extra;
};

struct InternetSlot {
    bool used{false};
    HandleKind kind{HandleKind::Session};
    std::size_t parent{kNpos};
    std::filesystem::path prefix;
    std::string user_agent;
    std::string host;
    std::uint16_t port{0};
    std::string method{"GET"};
    std::string object_name{"/"};
    std::vector<Header> headers;
    std::vector<std::uint8_t> body;
    std::vector<std::uint8_t> response_body;
    std::string raw_response_headers;
    std::vector<Header> response_headers;
    std::size_t response_position{0};
    std::uint32_t status_code{0};
    std::uint32_t connect_timeout_ms{30000};
    std::uint32_t send_timeout_ms{30000};
    std::uint32_t receive_timeout_ms{30000};
    bool secure{false};
    bool sent{false};
};

std::mutex g_internet_mutex;
std::array<InternetSlot, kMaxInternetHandles> g_internet_slots{};

void set_error(const std::uint32_t error) noexcept {
    tl_SetLastError(error);
}

void trace_wininet(const char* operation, const char* status,
                   const std::uint32_t port = 0, const std::size_t bytes = 0) noexcept {
    if (bytes != 0) {
        const std::array<diagnostics::TraceField, 7> fields{
            diagnostics::TraceField{"operation", operation},
            diagnostics::TraceField{"status", status},
            diagnostics::TraceField{"scheme", "https"},
            diagnostics::TraceField{"scope", "prefix"},
            diagnostics::TraceField{"destination", "loopback"},
            diagnostics::TraceField{"port", std::to_string(port)},
            diagnostics::TraceField{"bytes", std::to_string(bytes)},
        };
        diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                                 diagnostics::TraceLevel::Info, "wininet", fields);
        return;
    }
    const std::array<diagnostics::TraceField, 6> fields{
        diagnostics::TraceField{"operation", operation},
        diagnostics::TraceField{"status", status},
        diagnostics::TraceField{"scheme", "https"},
        diagnostics::TraceField{"scope", "prefix"},
        diagnostics::TraceField{"destination", "loopback"},
        diagnostics::TraceField{"port", std::to_string(port)},
    };
    diagnostics::write_trace(std::cerr, diagnostics::TraceComponent::Runtime,
                             diagnostics::TraceLevel::Info, "wininet", fields);
}

bool mapped_range(const void* address, const std::size_t size, const bool writable) noexcept {
    return runtime::validate_mapped_range(address, size, writable);
}

bool copy_wide(const std::uint16_t* source, const std::size_t length,
               std::string& result) noexcept {
    result.clear();
    if (source == nullptr || length > kMaxUrlLength ||
        length > std::numeric_limits<std::size_t>::max() / sizeof(std::uint16_t) ||
        !mapped_range(source, length * sizeof(std::uint16_t), false)) {
        return false;
    }
    std::vector<std::uint16_t> copy(length + 1U, 0);
    std::memcpy(copy.data(), source, length * sizeof(std::uint16_t));
    result = util::wide_to_utf8(copy.data(), length + 1U);
    return result.size() <= kMaxUrlLength;
}

bool copy_wide_z(const std::uint16_t* source, std::string& result) noexcept {
    if (source == nullptr || !runtime::validate_mapped_wstring(source, kMaxUrlLength)) {
        return false;
    }
    result = util::wide_to_utf8(source, kMaxUrlLength);
    return result.size() <= kMaxUrlLength;
}

std::string lower_ascii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

bool is_loopback_host(const std::string_view host) noexcept {
    return host == "localhost" || host == "127.0.0.1";
}

bool parse_url(const std::string_view input, ParsedUrl& result) noexcept {
    result = {};
    if (input.size() == 0 || input.size() > kMaxUrlLength ||
        input.find('#') != std::string_view::npos) {
        return false;
    }
    const std::size_t scheme_end = input.find("://");
    if (scheme_end == std::string_view::npos) {
        return false;
    }
    result.scheme = lower_ascii(std::string(input.substr(0, scheme_end)));
    if (result.scheme != "https") {
        return false;
    }
    std::string_view authority_and_path = input.substr(scheme_end + 3U);
    const std::size_t path_start = authority_and_path.find('/');
    const std::size_t query_start = authority_and_path.find('?');
    std::size_t authority_end = authority_and_path.size();
    if (path_start != std::string_view::npos) authority_end = std::min(authority_end, path_start);
    if (query_start != std::string_view::npos) authority_end = std::min(authority_end, query_start);
    const std::string_view authority = authority_and_path.substr(0, authority_end);
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find(':') != authority.rfind(':')) {
        // A single colon is allowed as the port separator; multiple colons
        // would be an IPv6 literal, outside the AF_INET contract.
        if (authority.find(':') != authority.rfind(':')) return false;
    }
    const std::size_t colon = authority.find(':');
    if (colon == std::string_view::npos) {
        result.host = lower_ascii(std::string(authority));
        result.port = (result.scheme == "https") ? 443 : 80;
    } else {
        result.host = lower_ascii(std::string(authority.substr(0, colon)));
        const std::string_view port_text = authority.substr(colon + 1U);
        unsigned int port = 0;
        const auto conversion = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (conversion.ec != std::errc{} || conversion.ptr != port_text.data() + port_text.size() ||
            port == 0 || port > 65535U) {
            return false;
        }
        result.port = static_cast<std::uint16_t>(port);
    }
    std::size_t path_offset = std::string_view::npos;
    if (path_start != std::string_view::npos) path_offset = path_start;
    if (query_start != std::string_view::npos) path_offset = std::min(path_offset, query_start);
    if (path_offset != std::string_view::npos) {
        const std::string_view suffix = authority_and_path.substr(path_offset);
        const std::size_t extra = suffix.find('?');
        if (extra == std::string_view::npos) {
            result.path = std::string(suffix);
        } else {
            result.path = std::string(suffix.substr(0, extra));
            result.extra = std::string(suffix.substr(extra));
        }
    }
    if (result.path.empty()) result.path = "/";
    if (result.path.front() != '/') return false;
    return true;
}

bool parse_object_name(const std::string_view input, std::string& output) noexcept {
    if (input.empty() || input.size() > kMaxUrlLength || input.front() != '/' ||
        input.find('#') != std::string_view::npos || input.find("://") != std::string_view::npos) {
        return false;
    }
    output = std::string(input);
    return true;
}

bool valid_header_token(const std::string_view token) noexcept {
    if (token.empty()) return false;
    for (const char raw_character : token) {
        const unsigned char character = static_cast<unsigned char>(raw_character);
        if (character <= 0x20U || character >= 0x7fU || character == ':' || character == '\r' ||
            character == '\n') return false;
    }
    return true;
}

bool prohibited_header(const std::string_view name) noexcept {
    const std::string lower = lower_ascii(std::string(name));
    return lower == "cookie" || lower == "authorization" || lower.starts_with("proxy-") ||
           lower == "host";
}

bool parse_headers(const std::string_view input, std::vector<Header>& output) noexcept {
    if (input.size() > kMaxHeaderLength) return false;
    std::size_t offset = 0;
    while (offset < input.size()) {
        const std::size_t line_end = input.find("\r\n", offset);
        const std::size_t end = line_end == std::string_view::npos ? input.size() : line_end;
        if (end == offset) return false;
        const std::string_view line = input.substr(offset, end - offset);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) return false;
        const std::string name(line.substr(0, colon));
        if (!valid_header_token(name) || prohibited_header(name)) return false;
        std::size_t value_start = colon + 1U;
        while (value_start < line.size() && line[value_start] == ' ') ++value_start;
        const std::string value(line.substr(value_start));
        for (const char raw_character : value) {
            const unsigned char character = static_cast<unsigned char>(raw_character);
            if (character < 0x20U && character != '\t') return false;
        }
        output.push_back(Header{name, value});
        if (line_end == std::string_view::npos) break;
        offset = line_end + 2U;
    }
    return !output.empty();
}

std::uintptr_t handle_value(const std::size_t index) noexcept {
    return kInternetHandleBase + static_cast<std::uintptr_t>(index);
}

std::optional<std::size_t> handle_index(const HInternet handle) noexcept {
    if (handle < kInternetHandleBase || handle >= kInternetHandleBase + kMaxInternetHandles) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(handle - kInternetHandleBase);
}

std::optional<std::size_t> find_handle_locked(const HInternet handle) noexcept {
    const auto index = handle_index(handle);
    if (!index.has_value() || !g_internet_slots[*index].used) return std::nullopt;
    return index;
}

std::optional<std::size_t> allocate_handle_locked(const HandleKind kind, const std::size_t parent) {
    for (std::size_t index = 0; index < g_internet_slots.size(); ++index) {
        if (!g_internet_slots[index].used) {
            g_internet_slots[index] = {};
            g_internet_slots[index].used = true;
            g_internet_slots[index].kind = kind;
            g_internet_slots[index].parent = parent;
            g_internet_slots[index].prefix = guest_prefix_root();
            return index;
        }
    }
    return std::nullopt;
}

bool descendant_of_locked(std::size_t candidate, const std::size_t ancestor) noexcept {
    while (candidate != kNpos) {
        if (candidate == ancestor) return true;
        candidate = g_internet_slots[candidate].parent;
    }
    return false;
}

std::size_t root_session_locked(std::size_t index) noexcept {
    while (g_internet_slots[index].parent != kNpos) index = g_internet_slots[index].parent;
    return index;
}

std::uint32_t effective_timeout_locked(const InternetSlot& request,
                                       const std::uint32_t InternetSlot::*member) noexcept {
    const InternetSlot* current = &request;
    std::size_t index = static_cast<std::size_t>(&request - g_internet_slots.data());
    while (true) {
        const std::uint32_t value = current->*member;
        if (value != 0) return value;
        if (current->parent == kNpos) break;
        index = current->parent;
        current = &g_internet_slots[index];
    }
    return 30000;
}

struct CurlCapture {
    std::string headers;
    std::vector<std::uint8_t> body;
};

std::size_t curl_write_callback(char* data, const std::size_t size,
                                const std::size_t count, void* opaque) {
    const std::size_t bytes = size * count;
    auto* capture = static_cast<CurlCapture*>(opaque);
    if (bytes > kMaxBodyLength - capture->body.size()) return 0;
    capture->body.insert(capture->body.end(), reinterpret_cast<std::uint8_t*>(data),
                         reinterpret_cast<std::uint8_t*>(data) + bytes);
    return bytes;
}

std::size_t curl_header_callback(char* data, const std::size_t size,
                                 const std::size_t count, void* opaque) {
    const std::size_t bytes = size * count;
    auto* capture = static_cast<CurlCapture*>(opaque);
    if (bytes > kMaxHeaderLength - capture->headers.size()) return 0;
    capture->headers.append(data, bytes);
    return bytes;
}

void parse_response_headers(const std::string& raw, std::vector<Header>& output,
                           std::uint32_t& status) {
    output.clear();
    status = 0;
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t line_end = raw.find("\r\n", offset);
        const std::size_t end = line_end == std::string::npos ? raw.size() : line_end;
        const std::string_view line(raw.data() + offset, end - offset);
        if (line.starts_with("HTTP/")) {
            const std::size_t space = line.find(' ');
            if (space != std::string_view::npos) {
                unsigned int parsed = 0;
                const auto code = line.substr(space + 1U, 3U);
                std::from_chars(code.data(), code.data() + code.size(), parsed);
                status = parsed;
            }
            output.clear();
        } else if (!line.empty()) {
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::size_t value_start = colon + 1U;
                while (value_start < line.size() && line[value_start] == ' ') ++value_start;
                output.push_back(Header{std::string(line.substr(0, colon)),
                                        std::string(line.substr(value_start))});
            }
        }
        if (line_end == std::string::npos) break;
        offset = line_end + 2U;
    }
}

const Header* find_response_header(const InternetSlot& request, const std::string_view name) noexcept {
    for (const Header& header : request.response_headers) {
        if (lower_ascii(header.name) == lower_ascii(std::string(name))) return &header;
    }
    return nullptr;
}

int map_curl_error(const int error) noexcept {
    switch (error) {
        case kCurlOperationTimedout: return static_cast<int>(kErrorInternetTimeout);
        case kCurlCouldntResolveHost: return static_cast<int>(kErrorInternetNameNotResolved);
        case kCurlCouldntConnect: return static_cast<int>(kErrorInternetCannotConnect);
        case kCurlSslConnectError:
        case kCurlPeerFailedVerification:
        case kCurlSslCertProblem:
        case kCurlSslCa: return static_cast<int>(kErrorInternetSecCertInvalid);
        default: return static_cast<int>(kErrorInternetCannotConnect);
    }
}

bool perform_request_locked(InternetSlot& request, const InternetSlot& connection,
                             const InternetSlot& session) noexcept {
    if (!g_curl.ensure()) {
        set_error(abi::kErrorModNotFound);
        trace_wininet("send", "libcurl-unavailable", connection.port);
        return false;
    }
    const char* ca_file = std::getenv("TL_WININET_CA_FILE");
    if (ca_file == nullptr || *ca_file == '\0') {
        set_error(kErrorInternetSecCertInvalid);
        trace_wininet("send", "ca-missing", connection.port);
        return false;
    }
    std::error_code path_error;
    const std::filesystem::path ca_path = std::filesystem::weakly_canonical(ca_file, path_error);
    if (path_error || !std::filesystem::is_regular_file(ca_path, path_error)) {
        set_error(kErrorInternetSecCertInvalid);
        trace_wininet("send", "ca-invalid", connection.port);
        return false;
    }

    CurlEasy* easy = g_curl.easy_init();
    if (easy == nullptr) {
        set_error(abi::kErrorNotEnoughMemory);
        return false;
    }
    CurlCapture capture;
    CurlSlist* curl_headers = nullptr;
    const auto cleanup = [&]() noexcept {
        if (curl_headers != nullptr) g_curl.slist_free_all(curl_headers);
        g_curl.easy_cleanup(easy);
    };
    const std::string url = "https://" + connection.host + ":" +
                            std::to_string(connection.port) + request.object_name;
    auto setopt = [&](const int option, auto value) noexcept {
        return g_curl.easy_setopt(easy, option, value) == kCurlOk;
    };
    if (!setopt(kCurlOptUrl, url.c_str()) ||
        !setopt(kCurlOptCustomRequest, request.method.c_str()) ||
        !setopt(kCurlOptWriteFunction, static_cast<CurlWriteCallback>(&curl_write_callback)) ||
        !setopt(kCurlOptWriteData, &capture) ||
        !setopt(kCurlOptHeaderFunction, static_cast<CurlWriteCallback>(&curl_header_callback)) ||
        !setopt(kCurlOptHeaderData, &capture) ||
        !setopt(kCurlOptNoSignal, 1L) ||
        !setopt(kCurlOptFollowLocation, 0L) ||
        !setopt(kCurlOptProxy, "") ||
        !setopt(kCurlOptNoProxy, "*") ||
        !setopt(kCurlOptSslVerifyPeer, 1L) ||
        !setopt(kCurlOptSslVerifyHost, 2L) ||
        !setopt(kCurlOptCaInfo, ca_path.c_str()) ||
        !setopt(kCurlOptConnectTimeoutMs, static_cast<long>(effective_timeout_locked(request, &InternetSlot::connect_timeout_ms))) ||
        !setopt(kCurlOptTimeoutMs, static_cast<long>(std::min(
            effective_timeout_locked(request, &InternetSlot::send_timeout_ms),
            effective_timeout_locked(request, &InternetSlot::receive_timeout_ms))))) {
        cleanup();
        set_error(kErrorInternetCannotConnect);
        return false;
    }
    if (!session.user_agent.empty() && !setopt(kCurlOptUserAgent, session.user_agent.c_str())) {
        cleanup();
        set_error(kErrorInternetCannotConnect);
        return false;
    }
    for (const Header& header : request.headers) {
        const std::string line = header.name + ": " + header.value;
        curl_headers = g_curl.slist_append(curl_headers, line.c_str());
        if (curl_headers == nullptr) {
            cleanup();
            set_error(abi::kErrorNotEnoughMemory);
            return false;
        }
    }
    if (curl_headers != nullptr && !setopt(kCurlOptHttpHeader, curl_headers)) {
        cleanup();
        set_error(kErrorInternetCannotConnect);
        return false;
    }
    if (request.method == "HEAD" && !setopt(kCurlOptNobody, 1L)) {
        cleanup();
        set_error(kErrorInternetCannotConnect);
        return false;
    }
    if (request.method == "POST" &&
        (!setopt(kCurlOptPostFields, request.body.empty() ? nullptr : request.body.data()) ||
         !setopt(kCurlOptPostFieldSize, static_cast<long>(request.body.size())))) {
        cleanup();
        set_error(kErrorInternetCannotConnect);
        return false;
    }
    const int result = g_curl.easy_perform(easy);
    if (result != kCurlOk) {
        cleanup();
        set_error(static_cast<std::uint32_t>(map_curl_error(result)));
        trace_wininet("send", "transport-error", connection.port);
        return false;
    }
    long status = 0;
    if (g_curl.easy_getinfo(easy, kCurlInfoResponseCode, &status) != kCurlOk || status < 100 || status > 999) {
        cleanup();
        set_error(kErrorInternetCannotConnect);
        return false;
    }
    request.raw_response_headers = capture.headers;
    request.response_body = std::move(capture.body);
    parse_response_headers(request.raw_response_headers, request.response_headers,
                           request.status_code);
    request.status_code = static_cast<std::uint32_t>(status);
    request.response_position = 0;
    request.sent = true;
    cleanup();
    set_error(abi::kErrorSuccess);
    trace_wininet("send", "success", connection.port, request.response_body.size());
    return true;
}

bool write_wide_component(std::uint16_t* destination, std::uint32_t& capacity,
                          const std::string& value) noexcept {
    const std::size_t required = value.size() + 1U;
    if (required > std::numeric_limits<std::uint32_t>::max()) return false;
    if (destination == nullptr || capacity == 0) {
        capacity = static_cast<std::uint32_t>(required);
        return value.empty();
    }
    if (capacity < required || !mapped_range(destination, required * sizeof(std::uint16_t), true)) {
        capacity = static_cast<std::uint32_t>(required);
        set_error(abi::kErrorInsufficientBuffer);
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        destination[index] = static_cast<std::uint16_t>(static_cast<unsigned char>(value[index]));
    }
    destination[value.size()] = 0;
    capacity = static_cast<std::uint32_t>(value.size());
    return true;
}

bool merge_headers(InternetSlot& request, const std::string_view text,
                   const std::uint32_t modifiers) noexcept {
    std::vector<Header> parsed;
    if (!parse_headers(text, parsed)) return false;
    const std::uint32_t mode = modifiers & (kHttpAddReqFlagAdd | kHttpAddReqFlagReplace |
                                            kHttpAddReqFlagAddIfNew);
    if (modifiers & ~(kHttpAddReqFlagAdd | kHttpAddReqFlagReplace | kHttpAddReqFlagAddIfNew)) {
        return false;
    }
    for (const Header& header : parsed) {
        const std::string key = lower_ascii(header.name);
        auto existing = std::find_if(request.headers.begin(), request.headers.end(),
                                     [&](const Header& candidate) {
                                         return lower_ascii(candidate.name) == key;
                                     });
        if (existing == request.headers.end()) {
            request.headers.push_back(header);
        } else if (mode == kHttpAddReqFlagReplace) {
            *existing = header;
        } else if (mode == kHttpAddReqFlagAdd) {
            request.headers.push_back(header);
        } else if (mode == kHttpAddReqFlagAddIfNew) {
            continue;
        } else {
            *existing = header;
        }
    }
    return true;
}

}  // namespace

extern "C" {

TL_MSABI HInternet tl_InternetOpenW(const std::uint16_t* user_agent,
                                     const std::uint32_t access_type,
                                     const std::uint16_t* proxy_name,
                                     const std::uint16_t* proxy_bypass,
                                     const std::uint32_t flags) noexcept {
    std::string agent;
    if (access_type != kInternetOpenTypeDirect || proxy_name != nullptr ||
        proxy_bypass != nullptr || flags != 0 ||
        (user_agent != nullptr && !copy_wide_z(user_agent, agent))) {
        set_error(abi::kErrorInvalidParameter);
        trace_wininet("open", "invalid");
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = allocate_handle_locked(HandleKind::Session, kNpos);
    if (!slot.has_value()) {
        set_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    g_internet_slots[*slot].user_agent = std::move(agent);
    set_error(abi::kErrorSuccess);
    trace_wininet("open", "success");
    return handle_value(*slot);
}

TL_MSABI HInternet tl_InternetConnectW(const HInternet internet,
                                        const std::uint16_t* server_name,
                                        const std::uint16_t server_port,
                                        const std::uint16_t* user_name,
                                        const std::uint16_t* password,
                                        const std::uint32_t service,
                                        const std::uint32_t flags,
                                        const std::uintptr_t context) noexcept {
    std::string server;
    if (server_name == nullptr || !copy_wide_z(server_name, server) || !is_loopback_host(lower_ascii(server)) ||
        server_port == 0 || service != kInternetServiceHttp || flags != 0 || context != 0 ||
        user_name != nullptr || password != nullptr) {
        set_error(kErrorInternetNameNotResolved);
        trace_wininet("connect", "invalid", server_port);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto parent = find_handle_locked(internet);
    if (!parent.has_value() || g_internet_slots[*parent].kind != HandleKind::Session) {
        set_error(kErrorInternetIncorrectHandleType);
        return 0;
    }
    const auto slot = allocate_handle_locked(HandleKind::Connection, *parent);
    if (!slot.has_value()) {
        set_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    g_internet_slots[*slot].host = lower_ascii(server);
    g_internet_slots[*slot].port = server_port;
    set_error(abi::kErrorSuccess);
    trace_wininet("connect", "success", server_port);
    return handle_value(*slot);
}

TL_MSABI HInternet tl_HttpOpenRequestW(const HInternet connect,
                                       const std::uint16_t* verb,
                                       const std::uint16_t* object_name,
                                       const std::uint16_t* version,
                                       const std::uint16_t* referrer,
                                       const std::uint16_t* const* accept_types,
                                       const std::uint32_t flags,
                                       const std::uintptr_t context) noexcept {
    std::string method = "GET";
    std::string object;
    std::string version_text;
    if ((verb != nullptr && (!copy_wide_z(verb, method) || method.size() > 8U)) ||
        (object_name == nullptr || !copy_wide_z(object_name, object)) ||
        (version != nullptr && (!copy_wide_z(version, version_text) || version_text != "HTTP/1.1")) ||
        referrer != nullptr || accept_types != nullptr || context != 0 ||
        flags != kInternetFlagSecure ||
        (method != "GET" && method != "HEAD" && method != "POST") ||
        !parse_object_name(object, object)) {
        set_error(abi::kErrorInvalidParameter);
        trace_wininet("request", "invalid");
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto parent = find_handle_locked(connect);
    if (!parent.has_value() || g_internet_slots[*parent].kind != HandleKind::Connection) {
        set_error(kErrorInternetIncorrectHandleType);
        return 0;
    }
    const auto slot = allocate_handle_locked(HandleKind::Request, *parent);
    if (!slot.has_value()) {
        set_error(abi::kErrorNotEnoughMemory);
        return 0;
    }
    g_internet_slots[*slot].method = std::move(method);
    g_internet_slots[*slot].object_name = std::move(object);
    g_internet_slots[*slot].secure = true;
    set_error(abi::kErrorSuccess);
    trace_wininet("request", "success", g_internet_slots[*parent].port);
    return handle_value(*slot);
}

TL_MSABI int tl_HttpAddRequestHeadersW(const HInternet request,
                                       const std::uint16_t* headers,
                                       const std::int32_t headers_length,
                                       const std::uint32_t modifiers) noexcept {
    if (headers == nullptr || headers_length == 0 || headers_length < -1 ||
        (headers_length > 0 && static_cast<std::size_t>(headers_length) > kMaxHeaderLength)) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string text;
    if (headers_length == -1) {
        if (!copy_wide_z(headers, text)) {
            set_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else if (!copy_wide(headers, static_cast<std::size_t>(headers_length), text)) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = find_handle_locked(request);
    if (!slot.has_value() || g_internet_slots[*slot].kind != HandleKind::Request) {
        set_error(kErrorInternetIncorrectHandleType);
        return 0;
    }
    if (!merge_headers(g_internet_slots[*slot], text, modifiers)) {
        set_error(abi::kErrorNotSupported);
        return 0;
    }
    set_error(abi::kErrorSuccess);
    trace_wininet("headers", "success");
    return 1;
}

TL_MSABI int tl_HttpSendRequestW(const HInternet request,
                                 const std::uint16_t* optional_headers,
                                 const std::uint32_t optional_headers_length,
                                 const void* optional_data,
                                 const std::uint32_t optional_data_length) noexcept {
    if ((optional_headers_length != 0 && optional_headers == nullptr) ||
        optional_headers_length > kMaxHeaderLength || optional_data_length > kMaxBodyLength ||
        (optional_data_length != 0 && optional_data == nullptr) ||
        (optional_data != nullptr && optional_data_length != 0 &&
         !mapped_range(optional_data, optional_data_length, false))) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string text;
    if (optional_headers_length != 0 &&
        !copy_wide(optional_headers, optional_headers_length, text)) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = find_handle_locked(request);
    if (!slot.has_value() || g_internet_slots[*slot].kind != HandleKind::Request) {
        set_error(kErrorInternetIncorrectHandleType);
        return 0;
    }
    InternetSlot& request_slot = g_internet_slots[*slot];
    if (request_slot.sent) {
        set_error(kErrorInternetInvalidOperation);
        return 0;
    }
    if (!text.empty() && !merge_headers(request_slot, text, kHttpAddReqFlagAdd)) {
        set_error(abi::kErrorNotSupported);
        return 0;
    }
    request_slot.body.clear();
    if (optional_data_length != 0) {
        const auto* bytes = static_cast<const std::uint8_t*>(optional_data);
        request_slot.body.assign(bytes, bytes + optional_data_length);
    }
    const std::size_t connection_index = request_slot.parent;
    const std::size_t session_index = root_session_locked(connection_index);
    const bool ok = perform_request_locked(request_slot, g_internet_slots[connection_index],
                                           g_internet_slots[session_index]);
    return ok ? 1 : 0;
}

TL_MSABI int tl_InternetReadFile(const HInternet file, void* buffer,
                                 const std::uint32_t number_of_bytes_to_read,
                                 std::uint32_t* number_of_bytes_read) noexcept {
    if (number_of_bytes_read == nullptr || !mapped_range(number_of_bytes_read, sizeof(*number_of_bytes_read), true) ||
        (number_of_bytes_to_read != 0 && (buffer == nullptr ||
         !mapped_range(buffer, number_of_bytes_to_read, true)))) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = find_handle_locked(file);
    if (!slot.has_value() || g_internet_slots[*slot].kind != HandleKind::Request) {
        set_error(kErrorInternetIncorrectHandleType);
        return 0;
    }
    InternetSlot& request = g_internet_slots[*slot];
    if (!request.sent) {
        set_error(kErrorInternetInvalidOperation);
        return 0;
    }
    const std::size_t remaining = request.response_body.size() - request.response_position;
    const std::size_t copy_size = std::min<std::size_t>(remaining, number_of_bytes_to_read);
    if (copy_size != 0) {
        std::memcpy(buffer, request.response_body.data() + request.response_position, copy_size);
        request.response_position += copy_size;
    }
    *number_of_bytes_read = static_cast<std::uint32_t>(copy_size);
    set_error(abi::kErrorSuccess);
    trace_wininet("read", "success", 0, copy_size);
    return 1;
}

TL_MSABI int tl_InternetQueryDataAvailable(const HInternet file,
                                           std::uint32_t* number_of_bytes_available,
                                           const std::uint32_t flags,
                                           const std::uintptr_t context) noexcept {
    if (number_of_bytes_available == nullptr ||
        !mapped_range(number_of_bytes_available, sizeof(*number_of_bytes_available), true) ||
        flags != 0 || context != 0) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = find_handle_locked(file);
    if (!slot.has_value() || g_internet_slots[*slot].kind != HandleKind::Request) {
        set_error(kErrorInternetIncorrectHandleType);
        return 0;
    }
    InternetSlot& request = g_internet_slots[*slot];
    if (!request.sent) {
        set_error(kErrorInternetInvalidOperation);
        return 0;
    }
    *number_of_bytes_available = static_cast<std::uint32_t>(
        std::min<std::size_t>(request.response_body.size() - request.response_position,
                              std::numeric_limits<std::uint32_t>::max()));
    set_error(abi::kErrorSuccess);
    trace_wininet("available", "success", 0, *number_of_bytes_available);
    return 1;
}

TL_MSABI int tl_HttpQueryInfoW(const HInternet request,
                               const std::uint32_t info_level,
                               void* buffer,
                               std::uint32_t* buffer_length,
                               std::uint32_t* index) noexcept {
    if (buffer_length == nullptr || !mapped_range(buffer_length, sizeof(*buffer_length), true) ||
        (index != nullptr && !mapped_range(index, sizeof(*index), true))) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (index != nullptr && *index != 0) {
        set_error(abi::kErrorNoMoreFiles);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = find_handle_locked(request);
    if (!slot.has_value() || g_internet_slots[*slot].kind != HandleKind::Request) {
        set_error(kErrorInternetIncorrectHandleType);
        return 0;
    }
    const InternetSlot& value = g_internet_slots[*slot];
    if (!value.sent) {
        set_error(kErrorInternetInvalidOperation);
        return 0;
    }
    const std::uint32_t query = info_level & 0xFFFFU;
    const bool number = (info_level & kHttpQueryFlagNumber) != 0;
    if (number && query != kHttpQueryStatusCode) {
        set_error(abi::kErrorNotSupported);
        return 0;
    }
    if (query == kHttpQueryStatusCode && number) {
        if (*buffer_length < sizeof(std::uint32_t) || buffer == nullptr ||
            !mapped_range(buffer, sizeof(std::uint32_t), true)) {
            *buffer_length = sizeof(std::uint32_t);
            set_error(abi::kErrorInsufficientBuffer);
            return 0;
        }
        std::memcpy(buffer, &value.status_code, sizeof(value.status_code));
        *buffer_length = sizeof(value.status_code);
        set_error(abi::kErrorSuccess);
        trace_wininet("query", "success");
        return 1;
    }
    std::string text;
    if (query == kHttpQueryStatusCode) text = std::to_string(value.status_code);
    else if (query == kHttpQueryContentType) {
        const Header* header = find_response_header(value, "content-type");
        if (header == nullptr) { set_error(abi::kErrorFileNotFound); return 0; }
        text = header->value;
    } else if (query == kHttpQueryContentLength) {
        const Header* header = find_response_header(value, "content-length");
        text = header != nullptr ? header->value : std::to_string(value.response_body.size());
    } else if (query == kHttpQueryRawHeaders || query == kHttpQueryRawHeadersCrlf) {
        text = value.raw_response_headers;
    } else {
        set_error(abi::kErrorNotSupported);
        return 0;
    }
    const std::size_t required = (text.size() + 1U) * sizeof(std::uint16_t);
    if (required > std::numeric_limits<std::uint32_t>::max() || buffer == nullptr ||
        *buffer_length < required || !mapped_range(buffer, required, true)) {
        *buffer_length = static_cast<std::uint32_t>(required);
        set_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    auto* destination = static_cast<std::uint16_t*>(buffer);
    for (std::size_t character = 0; character < text.size(); ++character) {
        destination[character] = static_cast<std::uint16_t>(static_cast<unsigned char>(text[character]));
    }
    destination[text.size()] = 0;
    *buffer_length = static_cast<std::uint32_t>(required);
    set_error(abi::kErrorSuccess);
    trace_wininet("query", "success");
    return 1;
}

TL_MSABI int tl_InternetSetOptionW(const HInternet internet,
                                   const std::uint32_t option,
                                   void* buffer,
                                   const std::uint32_t buffer_length) noexcept {
    if (buffer == nullptr || buffer_length != sizeof(std::uint32_t) ||
        !mapped_range(buffer, sizeof(std::uint32_t), false)) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    if (option != kInternetOptionConnectTimeout && option != kInternetOptionSendTimeout &&
        option != kInternetOptionReceiveTimeout) {
        set_error(abi::kErrorNotSupported);
        return 0;
    }
    const std::uint32_t timeout = *static_cast<const std::uint32_t*>(buffer);
    if (timeout == 0 || timeout > 600000U) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = find_handle_locked(internet);
    if (!slot.has_value()) {
        set_error(abi::kErrorInvalidHandle);
        return 0;
    }
    if (option == kInternetOptionConnectTimeout) g_internet_slots[*slot].connect_timeout_ms = timeout;
    if (option == kInternetOptionSendTimeout) g_internet_slots[*slot].send_timeout_ms = timeout;
    if (option == kInternetOptionReceiveTimeout) g_internet_slots[*slot].receive_timeout_ms = timeout;
    set_error(abi::kErrorSuccess);
    trace_wininet("option", "success");
    return 1;
}

TL_MSABI int tl_InternetCloseHandle(const HInternet internet) noexcept {
    std::lock_guard<std::mutex> lock(g_internet_mutex);
    const auto slot = find_handle_locked(internet);
    if (!slot.has_value()) {
        set_error(abi::kErrorInvalidHandle);
        return 0;
    }
    for (std::size_t index = 0; index < g_internet_slots.size(); ++index) {
        if (g_internet_slots[index].used && descendant_of_locked(index, *slot)) {
            g_internet_slots[index] = {};
        }
    }
    set_error(abi::kErrorSuccess);
    trace_wininet("close", "success");
    return 1;
}

TL_MSABI int tl_InternetCrackUrlW(const std::uint16_t* url,
                                  const std::uint32_t url_length,
                                  const std::uint32_t flags,
                                  GuestUrlComponentsW* components) noexcept {
    if (url == nullptr || components == nullptr || flags != 0 ||
        !mapped_range(components, sizeof(*components), true) ||
        components->dw_struct_size != sizeof(GuestUrlComponentsW)) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    std::string text;
    if (url_length == 0) {
        if (!copy_wide_z(url, text)) {
            set_error(abi::kErrorInvalidParameter);
            return 0;
        }
    } else if (!copy_wide(url, url_length, text)) {
        set_error(abi::kErrorInvalidParameter);
        return 0;
    }
    ParsedUrl parsed;
    if (!parse_url(text, parsed)) {
        set_error(kErrorInternetInvalidUrl);
        return 0;
    }
    components->n_scheme = kInternetSchemeHttps;
    components->n_port = parsed.port;
    components->dw_user_name_length = 0;
    components->dw_password_length = 0;
    if (!write_wide_component(components->lpsz_scheme, components->dw_scheme_length, parsed.scheme) ||
        !write_wide_component(components->lpsz_host_name, components->dw_host_name_length, parsed.host) ||
        !write_wide_component(components->lpsz_url_path, components->dw_url_path_length, parsed.path) ||
        !write_wide_component(components->lpsz_extra_info, components->dw_extra_info_length, parsed.extra)) {
        if (tl_GetLastError() == abi::kErrorSuccess) set_error(abi::kErrorInsufficientBuffer);
        return 0;
    }
    set_error(abi::kErrorSuccess);
    trace_wininet("crack-url", "success", parsed.port);
    return 1;
}

}  // extern "C"

}  // namespace tradutorlinux
