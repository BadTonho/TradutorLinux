#include "test_win32_common.hpp"
#include "tradutorlinux/win32/kernel32.hpp"
#include "tradutorlinux/runtime/shlwapi.hpp"

namespace tradutorlinux {
namespace {
TEST(WininetTest, RejectsExternalHostBeforeTransport) {
    constexpr std::uint16_t kAgent[] = {'t', 'e', 's', 't', 0};
    constexpr std::uint16_t kExternalHost[] = {'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', 0};

    const HInternet session = tl_InternetOpenW(kAgent, kInternetOpenTypeDirect,
                                                nullptr, nullptr, 0);
    ASSERT_NE(session, 0U);
    EXPECT_EQ(tl_InternetConnectW(session, kExternalHost, 443, nullptr, nullptr,
                                  kInternetServiceHttp, 0, 0),
              0U);
    EXPECT_EQ(tl_GetLastError(), kErrorInternetNameNotResolved);
    EXPECT_NE(tl_InternetCloseHandle(session), 0);
}

TEST(WininetTest, CrackUrlSplitsLoopbackHttpsComponents) {
    constexpr std::uint16_t kUrl[] = {
        'h', 't', 't', 'p', 's', ':', '/', '/', 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't',
        ':', '4', '4', '4', '3', '/', 'f', 'i', 'x', 't', 'u', 'r', 'e', '?', 'x', '=', '1', 0,
    };
    std::uint16_t scheme[8]{};
    std::uint16_t host[16]{};
    std::uint16_t path[16]{};
    std::uint16_t extra[16]{};
    GuestUrlComponentsW components{};
    components.dw_struct_size = sizeof(components);
    components.lpsz_scheme = scheme;
    components.dw_scheme_length = std::size(scheme);
    components.lpsz_host_name = host;
    components.dw_host_name_length = std::size(host);
    components.lpsz_url_path = path;
    components.dw_url_path_length = std::size(path);
    components.lpsz_extra_info = extra;
    components.dw_extra_info_length = std::size(extra);

    ASSERT_NE(tl_InternetCrackUrlW(kUrl, 0, 0, &components), 0);
    EXPECT_EQ(components.n_scheme, 2U);
    EXPECT_EQ(components.n_port, 4443U);
    EXPECT_EQ(components.dw_scheme_length, 5U);
    EXPECT_EQ(components.dw_host_name_length, 9U);
    EXPECT_EQ(components.dw_url_path_length, 8U);
    EXPECT_EQ(components.dw_extra_info_length, 4U);
    EXPECT_EQ(scheme[0], 'h');
    EXPECT_EQ(host[0], 'l');
    EXPECT_EQ(path[0], '/');
    EXPECT_EQ(extra[0], '?');
}

TEST(WininetTest, CrackUrlRejectsPlainHttp) {
    constexpr std::uint16_t kUrl[] = {'h', 't', 't', 'p', ':', '/', '/', 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', '/', 0};
    GuestUrlComponentsW components{};
    components.dw_struct_size = sizeof(components);

    EXPECT_EQ(tl_InternetCrackUrlW(kUrl, 0, 0, &components), 0);
    EXPECT_EQ(tl_GetLastError(), kErrorInternetInvalidUrl);
}

TEST(OleStreamTest, InMemoryStreamRoundTripsAndReportsSize) {
    GuestIStream* stream = nullptr;
    ASSERT_EQ(tl_CreateStreamOnHGlobal(nullptr, 1, &stream), kSOk);
    ASSERT_NE(stream, nullptr);

    constexpr char payload[] = "ole-stream-data";
    std::uint32_t written = 0;
    ASSERT_EQ(stream->vtable->write(stream, payload, sizeof(payload) - 1, &written), kSOk);
    EXPECT_EQ(written, sizeof(payload) - 1);

    GuestStatStg stat{};
    ASSERT_EQ(stream->vtable->stat(stream, &stat, 0), kSOk);
    EXPECT_EQ(stat.type, 2U);
    EXPECT_EQ(stat.cb_size, sizeof(payload) - 1);

    std::uint64_t position = 0;
    ASSERT_EQ(stream->vtable->seek(stream, 0, 0, &position), kSOk);
    EXPECT_EQ(position, 0U);
    char round_trip[sizeof(payload)]{};
    std::uint32_t read = 0;
    ASSERT_EQ(stream->vtable->read(stream, round_trip, sizeof(payload) - 1, &read), kSOk);
    EXPECT_EQ(read, sizeof(payload) - 1);
    EXPECT_STREQ(round_trip, payload);
    EXPECT_EQ(stream->vtable->release(stream), 0U);
}

TEST(OleStreamTest, UsesValidGlobalHGlobalAsInitialBackingStore) {
    constexpr char payload[] = "global-ole-stream";
    void* const memory = tl_GlobalAlloc(0, sizeof(payload) - 1U);
    ASSERT_NE(memory, nullptr);
    std::memcpy(memory, payload, sizeof(payload) - 1U);

    GuestIStream* stream = nullptr;
    ASSERT_EQ(tl_CreateStreamOnHGlobal(memory, 0, &stream), kSOk);
    ASSERT_NE(stream, nullptr);

    char round_trip[sizeof(payload)]{};
    std::uint32_t read = 0;
    ASSERT_EQ(stream->vtable->read(stream, round_trip, sizeof(payload) - 1U, &read), kSOk);
    EXPECT_EQ(read, sizeof(payload) - 1U);
    EXPECT_STREQ(round_trip, payload);
    EXPECT_EQ(stream->vtable->release(stream), 0U);
    EXPECT_NE(tl_GlobalLock(memory), nullptr);
    EXPECT_EQ(tl_GlobalFree(memory), nullptr);
}

TEST(OleStreamTest, DeleteOnReleaseReleasesGlobalHGlobalBackingStore) {
    void* const memory = tl_GlobalAlloc(0, 4U);
    ASSERT_NE(memory, nullptr);

    GuestIStream* stream = nullptr;
    ASSERT_EQ(tl_CreateStreamOnHGlobal(memory, 1, &stream), kSOk);
    ASSERT_NE(stream, nullptr);
    EXPECT_EQ(stream->vtable->release(stream), 0U);
    EXPECT_EQ(tl_GlobalLock(memory), nullptr);
}

TEST(OleStreamTest, RejectsExternalHGlobalAndUnknownInterface) {
    GuestIStream* stream = nullptr;
    EXPECT_EQ(tl_CreateStreamOnHGlobal(reinterpret_cast<OleHGlobal>(1), 1, &stream), kEInvalidArg);
    ASSERT_EQ(tl_CreateStreamOnHGlobal(nullptr, 1, &stream), kSOk);

    constexpr std::uint8_t unknown_iid[16]{};
    GuestIStream* queried = nullptr;
    EXPECT_EQ(stream->vtable->query_interface(stream, unknown_iid, &queried), kENoInterface);
    EXPECT_EQ(queried, nullptr);
    EXPECT_EQ(stream->vtable->release(stream), 0U);
}

TEST(WintrustTest, RejectsUnsupportedPolicyBeforeCertificateProvider) {
    GuestWintrustData data{};
    EXPECT_EQ(tl_WinVerifyTrust(nullptr, nullptr, &data), kTrustInvalidParameter);

    constexpr std::uint8_t wrong_action[16]{};
    data.cb_struct = sizeof(data);
    EXPECT_EQ(tl_WinVerifyTrust(nullptr, wrong_action, &data), kTrustInvalidParameter);
}

TEST(WintrustTest, HelpersRejectUnknownStateAndChainPointers) {
    EXPECT_EQ(tl_WTHelperProvDataFromStateData(reinterpret_cast<void*>(1)), nullptr);
    EXPECT_EQ(tl_WTHelperGetProvSignerFromChain(nullptr, 0, 0, 0), nullptr);
    EXPECT_EQ(tl_WTHelperGetProvCertFromChain(nullptr, 0), nullptr);
}

TEST(Crypt32Test, CertGetNameStringReadsSubjectIssuerAndValidatesBuffers) {
    const std::array<std::uint8_t, 61> certificate{
        0x30, 0x3B, 0x30, 0x34, 0x02, 0x01, 0x01, 0x30, 0x00,
        0x30, 0x12, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x07, 'T', 'L', ' ', 'R', 'o', 'o', 't',
        0x30, 0x00,
        0x30, 0x15, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x0A, 'T', 'L', ' ', 'F', 'i', 'x', 't', 'u', 'r', 'e',
        0x30, 0x00, 0x30, 0x00, 0x03, 0x01, 0x00,
    };
    GuestCertContext context{};
    context.encoding_type = 1;
    context.encoded = const_cast<std::uint8_t*>(certificate.data());
    context.encoded_size = static_cast<std::uint32_t>(certificate.size());
    std::uint16_t name[32]{};
    constexpr std::uint16_t kSubject[] = {'T', 'L', ' ', 'F', 'i', 'x', 't', 'u', 'r', 'e', 0};
    constexpr std::uint16_t kIssuer[] = {'T', 'L', ' ', 'R', 'o', 'o', 't', 0};
    const char common_name[] = "2.5.4.3";

    EXPECT_EQ(tl_CertGetNameStringW(&context, kCertNameSimpleDisplayType, 0, nullptr, nullptr, 0),
              11U);
    EXPECT_EQ(tl_CertGetNameStringW(&context, kCertNameSimpleDisplayType, 0, nullptr, name, 32),
              11U);
    EXPECT_TRUE(std::equal(std::begin(kSubject), std::end(kSubject), name));
    EXPECT_EQ(tl_CertGetNameStringW(&context, kCertNameSimpleDisplayType, kCertNameIssuerFlag,
                                    nullptr, name, 32),
              8U);
    EXPECT_TRUE(std::equal(std::begin(kIssuer), std::end(kIssuer), name));
    EXPECT_EQ(tl_CertGetNameStringW(&context, kCertNameAttrType, 0, common_name, name, 32),
              11U);
    EXPECT_EQ(tl_CertGetNameStringW(&context, kCertNameSimpleDisplayType, 0, nullptr, name, 2),
              0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
    EXPECT_EQ(tl_CertGetNameStringW(&context, 7, 0, nullptr, name, 32), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Crypt32Test, CertContextAndStoreManagement) {
    const std::array<std::uint8_t, 61> certificate{
        0x30, 0x3B, 0x30, 0x34, 0x02, 0x01, 0x01, 0x30, 0x00,
        0x30, 0x12, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x07, 'T', 'L', ' ', 'R', 'o', 'o', 't',
        0x30, 0x00,
        0x30, 0x15, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03,
        0x0C, 0x0A, 'T', 'L', ' ', 'F', 'i', 'x', 't', 'u', 'r', 'e',
        0x30, 0x00, 0x30, 0x00, 0x03, 0x01, 0x00,
    };
    GuestCertContext context{};
    context.encoding_type = 1;
    context.encoded = const_cast<std::uint8_t*>(certificate.data());
    context.encoded_size = static_cast<std::uint32_t>(certificate.size());

    // Duplicate context
    EXPECT_EQ(tl_CertDuplicateCertificateContext(nullptr), nullptr);
    const GuestCertContext* dup = tl_CertDuplicateCertificateContext(&context);
    ASSERT_NE(dup, nullptr);
    EXPECT_EQ(dup->encoded_size, context.encoded_size);

    // SHA-1 property
    std::uint32_t hash_size = 0;
    EXPECT_EQ(tl_CertGetCertificateContextProperty(dup, kCertSha1HashPropId, nullptr, &hash_size), 1U);
    EXPECT_EQ(hash_size, 20U);

    std::uint8_t small_hash[10]{};
    hash_size = sizeof(small_hash);
    EXPECT_EQ(tl_CertGetCertificateContextProperty(dup, kCertSha1HashPropId, small_hash, &hash_size), 0U);
    EXPECT_EQ(tl_GetLastError(), kErrorMoreData);
    EXPECT_EQ(hash_size, 20U);

    std::uint8_t full_hash[20]{};
    EXPECT_EQ(tl_CertGetCertificateContextProperty(dup, kCertSha1HashPropId, full_hash, &hash_size), 1U);
    EXPECT_EQ(hash_size, 20U);

    // Free context
    EXPECT_EQ(tl_CertFreeCertificateContext(nullptr), 1U);
    EXPECT_EQ(tl_CertFreeCertificateContext(dup), 1U);

    // Store operations
    EXPECT_EQ(tl_CertOpenStore(nullptr, 0, nullptr, 0, nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CertOpenStore("TL_UNKNOWN_STORE_PROVIDER", 0, nullptr, 0, nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    void* mem_store = tl_CertOpenStore(reinterpret_cast<const char*>(kCertStoreProvMemory), 0,
                                       nullptr, 0, nullptr);
    ASSERT_NE(mem_store, nullptr);
    EXPECT_EQ(tl_CertCloseStore(nullptr, 0), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
    EXPECT_EQ(tl_CertCloseStore(mem_store, kCertCloseStoreCheckFlag), 1U);

    // System store wrappers
    void* sys_a = tl_CertOpenSystemStoreA(nullptr, "ROOT");
    ASSERT_NE(sys_a, nullptr);
    EXPECT_EQ(tl_CertCloseStore(sys_a, 0), 1U);

    constexpr std::uint16_t kStoreW[] = {'M', 'Y', 0};
    void* sys_w = tl_CertOpenSystemStoreW(nullptr, kStoreW);
    ASSERT_NE(sys_w, nullptr);
    EXPECT_EQ(tl_CertCloseStore(sys_w, 0), 1U);
}

TEST(Crypt32Test, CryptQueryObjectRejectsUnsupportedInputWithoutFabricatedHandles) {
    std::uint32_t encoding = 0xFFFFFFFFU;
    std::uint32_t content = 0xFFFFFFFFU;
    std::uint32_t format = 0xFFFFFFFFU;
    void* store = reinterpret_cast<void*>(1);
    void* message = reinterpret_cast<void*>(1);
    const void* context = reinterpret_cast<const void*>(1);

    EXPECT_EQ(tl_CryptQueryObject(1U, nullptr, 0U, 0U, 0U, &encoding, &content, &format,
                                  &store, &message, &context), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(encoding, 0U);
    EXPECT_EQ(content, 0U);
    EXPECT_EQ(format, 0U);
    EXPECT_EQ(store, nullptr);
    EXPECT_EQ(message, nullptr);
    EXPECT_EQ(context, nullptr);

    EXPECT_EQ(tl_CryptQueryObject(1U, nullptr, 0U, 0U, 0U,
                                  reinterpret_cast<std::uint32_t*>(1), nullptr, nullptr,
                                  nullptr, nullptr, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Crypt32Test, CryptMsgGetParamRejectsUnknownHandles) {
    std::uint32_t size = 0xFFFFFFFFU;
    std::uint8_t data[8]{};

    EXPECT_EQ(tl_CryptMsgGetParam(nullptr, 0U, 0U, data, &size), 0);
    EXPECT_EQ(size, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);

    size = 0xFFFFFFFFU;
    EXPECT_EQ(tl_CryptMsgGetParam(reinterpret_cast<void*>(1), 0U, 0U, data, &size), 0);
    EXPECT_EQ(size, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);

    EXPECT_EQ(tl_CryptMsgGetParam(reinterpret_cast<void*>(1), 0U, 0U, data, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Crypt32Test, CryptMsgCloseRejectsUnknownHandles) {
    EXPECT_EQ(tl_CryptMsgClose(nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);

    EXPECT_EQ(tl_CryptMsgClose(reinterpret_cast<void*>(1)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidHandle);
}

TEST(User32ExtTest, DesktopCaptureAndRectOperations) {
    EXPECT_NE(tl_GetDesktopWindow(), nullptr);
    EXPECT_NE(tl_MonitorFromWindow(nullptr, 0), nullptr);

    std::uint32_t pid = 0;
    std::uint32_t tid = tl_GetWindowThreadProcessId(tl_GetDesktopWindow(), &pid);
    EXPECT_NE(tid, 0U);
    EXPECT_NE(pid, 0U);

    EXPECT_EQ(tl_SetCapture(nullptr), nullptr);
    EXPECT_EQ(tl_GetCapture(), nullptr);
    EXPECT_EQ(tl_ReleaseCapture(), 1);

    abi::GuestRect r{10, 20, 100, 200};
    EXPECT_EQ(tl_PtInRect(&r, 50, 50), 1);
    EXPECT_EQ(tl_PtInRect(&r, 5, 50), 0);
    EXPECT_EQ(tl_PtInRect(&r, 150, 50), 0);
    EXPECT_EQ(tl_PtInRect(nullptr, 50, 50), 0);

    abi::GuestRect dst{0, 0, 0, 0};
    EXPECT_EQ(tl_CopyRect(&dst, &r), 1);
    EXPECT_EQ(dst.left, 10);
    EXPECT_EQ(dst.bottom, 200);

    std::int32_t pt[2] = {5, 10};
    EXPECT_EQ(tl_MapWindowPoints(nullptr, nullptr, pt, 1), 0);
    EXPECT_EQ(pt[0], 5);
    EXPECT_EQ(pt[1], 10);

    EXPECT_EQ(tl_GetSysColor(kColorWindow), 0x00FFFFFFU);
    EXPECT_EQ(tl_GetSysColor(kColorWindowText), 0x00000000U);

    std::uint16_t upper_char = static_cast<std::uint16_t>(
        reinterpret_cast<std::uintptr_t>(tl_CharUpperW(reinterpret_cast<std::uint16_t*>(u'a'))));
    EXPECT_EQ(upper_char, u'A');

    std::uint16_t str[] = {u'a', u'b', u'c', 0};
    tl_CharUpperW(str);
    EXPECT_EQ(str[0], u'A');
    EXPECT_EQ(str[1], u'B');
    EXPECT_EQ(str[2], u'C');

    abi::GuestRect calc_r{0, 0, 0, 0};
    int height = tl_DrawTextW(nullptr, str, 3, &calc_r, kDtCalcRect);
    EXPECT_GT(height, 0);
    EXPECT_GT(calc_r.right, 0);
    EXPECT_GT(calc_r.bottom, 0);
}

TEST(Kernel32SystemTest, TimeZoneProcessAndAffinity) {
    tl_OutputDebugStringA("test A");
    std::uint16_t wstr[] = {u't', u'e', u's', u't', 0};
    tl_OutputDebugStringW(wstr);

    EXPECT_EQ(tl_SetDllDirectoryW(wstr), 1);
    EXPECT_EQ(tl_SetDllDirectoryW(nullptr), 1);

    std::uint8_t tzi[200]{};
    EXPECT_EQ(tl_GetTimeZoneInformation(tzi), 1U);

    EXPECT_NE(tl_GetProcessId(nullptr), 0U);

    std::uint16_t img_name[260]{};
    std::uint32_t size = 260;
    // QueryFullProcessImageNameW returns success or handles capacity correctly
    tl_QueryFullProcessImageNameW(nullptr, 0, img_name, &size);

    std::uint64_t ft = 0x01D9E00000000000ULL;
    std::uint64_t lft = 0;
    EXPECT_EQ(tl_FileTimeToLocalFileTime(&ft, &lft), 1);
    EXPECT_EQ(lft, ft);

    std::uint16_t path_out[260]{};
    EXPECT_GT(tl_GetLongPathNameW(wstr, path_out, 260), 0U);
    EXPECT_GT(tl_GetShortPathNameW(wstr, path_out, 260), 0U);

    EXPECT_EQ(tl_SetThreadPriority(nullptr, 0), 1);

    std::uintptr_t proc_mask = 0, sys_mask = 0;
    EXPECT_EQ(tl_GetProcessAffinityMask(nullptr, &proc_mask, &sys_mask), 1);
    EXPECT_NE(proc_mask, 0U);
    EXPECT_NE(sys_mask, 0U);
}

TEST(ShellPathTest, PathIsRelativeAndAutoComplete) {
    EXPECT_EQ(tl_PathIsRelativeA("foo/bar"), 1);
    EXPECT_EQ(tl_PathIsRelativeA("/foo/bar"), 0);
    EXPECT_EQ(tl_PathIsRelativeA("C:\\foo\\bar"), 0);

    std::uint16_t rel_w[] = {u'f', u'o', u'o', u'\\', u'b', u'a', u'r', 0};
    std::uint16_t abs_w[] = {u'C', u':', u'\\', u'f', u'o', u'o', 0};
    EXPECT_EQ(tl_PathIsRelativeW(rel_w), 1);
    EXPECT_EQ(tl_PathIsRelativeW(abs_w), 0);

    EXPECT_EQ(tl_SHAutoComplete(nullptr, 0), 0);

    std::uint8_t op_buf[100]{};
    EXPECT_EQ(tl_SHFileOperationW(op_buf), 0);
}

TEST(ShellPathTest, PathFileExistsAndIsDirectoryWithZDrive) {
    EXPECT_EQ(tl_PathFileExistsA("Z:\\etc\\passwd"), 1);
    EXPECT_EQ(tl_PathIsDirectoryA("Z:\\etc"), 1);
    EXPECT_EQ(tl_PathIsDirectoryA("Z:\\etc\\passwd"), 0);

    constexpr std::uint16_t z_passwd[] = {u'Z', u':', u'\\', u'e', u't', u'c', u'\\', u'p', u'a', u's', u's', u'w', u'd', 0};
    constexpr std::uint16_t z_etc[] = {u'Z', u':', u'\\', u'e', u't', u'c', 0};
    constexpr std::uint16_t z_missing[] = {u'Z', u':', u'\\', u'n', u'o', u'n', u'e', u'x', u'i', u's', u't', u'e', u'n', u't', 0};

    EXPECT_EQ(tl_PathFileExistsW(z_passwd), 1);
    EXPECT_EQ(tl_PathIsDirectoryW(z_etc), 1);
    EXPECT_EQ(tl_PathIsDirectoryW(z_passwd), 0);
    EXPECT_EQ(tl_PathFileExistsW(z_missing), 0);
    EXPECT_EQ(tl_PathIsDirectoryW(z_missing), 0);
}

TEST(ShellPathTest, SHGetFolderPathWReturnsWindowsPath) {
    std::uint16_t buffer[260]{};
    // CSIDL_APPDATA = 0x001a
    EXPECT_EQ(tl_SHGetFolderPathW(nullptr, 0x001a, nullptr, 0, buffer), 0);
    EXPECT_TRUE(buffer[0] == u'C' || buffer[0] == u'Z');
    EXPECT_EQ(buffer[1], u':');
    EXPECT_EQ(buffer[2], u'\\');
}

TEST(IphlpapiTest, EnumeratesLinuxAdaptersWithWin32BufferContracts) {
    constexpr std::uint32_t kErrorBufferOverflow = 111U;
    constexpr std::uint32_t kErrorNoData = 232U;

    std::uint32_t info_size = 0;
    const std::uint32_t info_probe = tl_GetAdaptersInfo(nullptr, &info_size);
    if (info_probe == kErrorNoData) GTEST_SKIP() << "host has no network interfaces";
    ASSERT_EQ(info_probe, kErrorBufferOverflow);
    ASSERT_GT(info_size, 0U);
    std::vector<std::uint8_t> info(info_size);
    ASSERT_EQ(tl_GetAdaptersInfo(info.data(), &info_size), 0U);
    std::uint32_t combo_index = 0;
    std::memcpy(&combo_index, info.data(), sizeof(combo_index));
    EXPECT_GT(combo_index, 0U);

    std::uint32_t addresses_size = 0;
    const std::uint32_t addresses_probe =
        tl_GetAdaptersAddresses(2U, 0U, nullptr, nullptr, &addresses_size);
    if (addresses_probe == kErrorNoData) GTEST_SKIP() << "host has no IPv4 network interfaces";
    ASSERT_EQ(addresses_probe, kErrorBufferOverflow);
    ASSERT_GT(addresses_size, 0U);
    std::vector<std::uint8_t> addresses(addresses_size);
    ASSERT_EQ(tl_GetAdaptersAddresses(2U, 0U, nullptr, addresses.data(), &addresses_size), 0U);

    std::uint32_t record_length = 0;
    std::uint32_t interface_index = 0;
    std::uintptr_t unicast_address = 0;
    std::memcpy(&record_length, addresses.data(), sizeof(record_length));
    std::memcpy(&interface_index, addresses.data() + 4U, sizeof(interface_index));
    std::memcpy(&unicast_address, addresses.data() + 24U, sizeof(unicast_address));
    EXPECT_EQ(record_length, 184U);
    EXPECT_GT(interface_index, 0U);
    EXPECT_NE(unicast_address, 0U);
    EXPECT_GE(unicast_address, reinterpret_cast<std::uintptr_t>(addresses.data()));
    EXPECT_LT(unicast_address,
              reinterpret_cast<std::uintptr_t>(addresses.data() + addresses.size()));

    char loopback[] = "lo";
    EXPECT_GT(tl_if_nametoindex(loopback), 0U);
    EXPECT_EQ(tl_if_nametoindex("tl-interface-does-not-exist"), 0U);
}

TEST(WtsApiTest, EnumeratesAndReleasesLocalSession) {
    void* session_info = nullptr;
    std::uint32_t count = 0;
    ASSERT_EQ(tl_WTSEnumerateSessionsW(nullptr, 0U, 1U, &session_info, &count), 1);
    ASSERT_NE(session_info, nullptr);
    ASSERT_EQ(count, 1U);

    std::uint32_t session_id = 0;
    std::uintptr_t station_name = 0;
    std::uint32_t state = 0;
    std::memcpy(&session_id, session_info, sizeof(session_id));
    std::memcpy(&station_name, static_cast<std::byte*>(session_info) + 8U,
                sizeof(station_name));
    std::memcpy(&state, static_cast<std::byte*>(session_info) + 16U, sizeof(state));
    EXPECT_EQ(session_id, 0U);
    EXPECT_NE(station_name, 0U);
    EXPECT_EQ(state, 0U);
    EXPECT_EQ(static_cast<const std::uint16_t*>(reinterpret_cast<const void*>(station_name))[0],
              u'C');

    tl_WTSFreeMemory(session_info);
    tl_WTSFreeMemory(session_info);

    std::uint16_t* query_buffer = reinterpret_cast<std::uint16_t*>(0x1U);
    std::uint32_t bytes_returned = 99U;
    EXPECT_EQ(tl_WTSQuerySessionInformationW(nullptr, 0U, 8U, &query_buffer,
                                              &bytes_returned), 0);
    EXPECT_EQ(query_buffer, nullptr);
    EXPECT_EQ(bytes_returned, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
}

TEST(MsixParserTest, ParseManifestXml) {
    const std::string_view sample_manifest = R"(<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="SerifEuropeLtd.AffinityPhoto2" Publisher="CN=Serif (Europe) Ltd" Version="2.5.5.0" ProcessorArchitecture="x64" />
  <Applications>
    <Application Id="App" Executable="App\Photo.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="Affinity Photo 2" Description="Affinity Photo 2" />
    </Application>
  </Applications>
</Package>)";

    const auto info = package::parse_appx_manifest_xml(sample_manifest);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->package_name, "SerifEuropeLtd.AffinityPhoto2");
    EXPECT_EQ(info->publisher, "CN=Serif (Europe) Ltd");
    EXPECT_EQ(info->version, "2.5.5.0");
    ASSERT_EQ(info->applications.size(), 1U);
    EXPECT_EQ(info->applications[0].id, "App");
    EXPECT_EQ(info->applications[0].executable, "App\\Photo.exe");
    EXPECT_EQ(info->applications[0].display_name, "Affinity Photo 2");
    EXPECT_EQ(info->main_executable.value_or(""), "App\\Photo.exe");
}

TEST(MsixParserTest, ParsesNamespacesCommentsCdataEntitiesAndSingleQuotes) {
    const std::string_view manifest = R"(<?xml version='1.0'?>
<!-- <Application Id="fake" Executable="fake.exe" /> -->
<Package xmlns='http://schemas.microsoft.com/appx/manifest/foundation/windows10'
         xmlns:uap='http://schemas.microsoft.com/appx/manifest/uap/windows10'>
  <Identity Version='1.2.3.4' Publisher='CN=Example &amp; Co' Name='Example.App' />
  <Applications>
    <Application EntryPoint='Windows.FullTrustApplication' Id='App'
                 Executable='bin\\Example.exe'>
      <![CDATA[<Application Id="fake-in-cdata" />]]>
      <uap:VisualElements DisplayName='Example &amp; Tools &#x1F680;' />
    </Application>
  </Applications>
</Package>)";

    const auto info = package::parse_appx_manifest_xml(manifest);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->publisher, "CN=Example & Co");
    ASSERT_EQ(info->applications.size(), 1U);
    EXPECT_EQ(info->applications[0].display_name, "Example & Tools \xF0\x9F\x9A\x80");
    EXPECT_EQ(info->main_executable.value_or(""), "bin\\\\Example.exe");
}

TEST(MsixParserTest, RejectsMalformedXmlAndExternalEntityDeclarations) {
    EXPECT_FALSE(package::parse_appx_manifest_xml(
                     "<Package><Applications></Package>")
                     .has_value());
    EXPECT_FALSE(package::parse_appx_manifest_xml(
                     "<!DOCTYPE Package SYSTEM 'file:///tmp/evil'><Package />")
                     .has_value());
}

TEST(MsixParserTest, InspectsDeflatedManifestFromCentralDirectory) {
    const std::filesystem::path path = "_tl_deflated.msix";
    const std::string manifest = R"(<Package xmlns="urn:appx"><Identity Name="Deflated" Publisher="CN=Test" Version="1.0.0.0"/><Applications><Application Id="App" Executable="app.exe"><uap:VisualElements DisplayName="Deflated app"/></Application></Applications></Package>)";
    ASSERT_TRUE(write_deflated_msix(path, manifest));

    const auto info = package::inspect_msix_package(path);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->package_name, "Deflated");
    EXPECT_EQ(info->main_executable.value_or(""), "app.exe");
    ASSERT_EQ(info->applications.size(), 1U);
    EXPECT_EQ(info->applications[0].display_name, "Deflated app");

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(MsixParserTest, RejectsTruncatedManifestWithoutLargeAllocation) {
    const std::filesystem::path path = "_tl_truncated.msix";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        const auto write_u16 = [&output](const std::uint16_t value) {
            const char bytes[2]{static_cast<char>(value & 0xFFU),
                                static_cast<char>((value >> 8U) & 0xFFU)};
            output.write(bytes, sizeof(bytes));
        };
        const auto write_u32 = [&output](const std::uint32_t value) {
            const char bytes[4]{static_cast<char>(value & 0xFFU),
                                static_cast<char>((value >> 8U) & 0xFFU),
                                static_cast<char>((value >> 16U) & 0xFFU),
                                static_cast<char>((value >> 24U) & 0xFFU)};
            output.write(bytes, sizeof(bytes));
        };
        write_u32(0x04034B50U);
        write_u16(20);  // version
        write_u16(0);   // flags
        write_u16(0);   // stored
        write_u16(0);
        write_u16(0);
        write_u32(0);   // crc32
        write_u32(0);   // compressed size
        write_u32(0xFFFFFFFFU);  // impossible uncompressed size
        const std::string name = "AppxManifest.xml";
        write_u16(static_cast<std::uint16_t>(name.size()));
        write_u16(0);
        output.write(name.data(), static_cast<std::streamsize>(name.size()));
    }
    EXPECT_FALSE(package::inspect_msix_package(path).has_value());
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(MsixParserTest, RejectsOversizedManifestBeforeParsing) {
    const std::string oversized_manifest(17U * 1024U * 1024U, 'x');
    EXPECT_FALSE(package::parse_appx_manifest_xml(oversized_manifest).has_value());
}

TEST(MsixParserTest, RejectsZipPathTraversal) {
    const std::filesystem::path path = "_tl_traversal.msix";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        const auto write_u16 = [&output](const std::uint16_t value) {
            const char bytes[2]{static_cast<char>(value & 0xFFU),
                                static_cast<char>((value >> 8U) & 0xFFU)};
            output.write(bytes, sizeof(bytes));
        };
        const auto write_u32 = [&output](const std::uint32_t value) {
            const char bytes[4]{static_cast<char>(value & 0xFFU),
                                static_cast<char>((value >> 8U) & 0xFFU),
                                static_cast<char>((value >> 16U) & 0xFFU),
                                static_cast<char>((value >> 24U) & 0xFFU)};
            output.write(bytes, sizeof(bytes));
        };
        write_u32(0x04034B50U);
        write_u16(20);
        write_u16(0);
        write_u16(0);
        write_u16(0);
        write_u16(0);
        write_u32(0);
        write_u32(0);
        write_u32(0);
        const std::string name = "../AppxManifest.xml";
        write_u16(static_cast<std::uint16_t>(name.size()));
        write_u16(0);
        output.write(name.data(), static_cast<std::streamsize>(name.size()));
    }
    EXPECT_FALSE(package::inspect_msix_package(path).has_value());
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(MsixParserTest, RejectsPackagesWithTooManyEntries) {
    const std::filesystem::path path = "_tl_many_entries.msix";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good());
        const auto write_u16 = [&output](const std::uint16_t value) {
            const char bytes[2]{static_cast<char>(value & 0xFFU),
                                static_cast<char>((value >> 8U) & 0xFFU)};
            output.write(bytes, sizeof(bytes));
        };
        const auto write_u32 = [&output](const std::uint32_t value) {
            const char bytes[4]{static_cast<char>(value & 0xFFU),
                                static_cast<char>((value >> 8U) & 0xFFU),
                                static_cast<char>((value >> 16U) & 0xFFU),
                                static_cast<char>((value >> 24U) & 0xFFU)};
            output.write(bytes, sizeof(bytes));
        };
        for (std::uint32_t index = 0; index < 10001U; ++index) {
            write_u32(0x04034B50U);
            write_u16(20);
            write_u16(0);
            write_u16(0);
            write_u16(0);
            write_u16(0);
            write_u32(0);
            write_u32(0);
            write_u32(0);
            write_u16(1);
            write_u16(0);
            output.put('a');
        }
    }
    EXPECT_FALSE(package::inspect_msix_package(path).has_value());
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(AppCatalogTest, RejectsPathTraversalAndReadsUnicodeEscapes) {
    catalog::AppCatalog catalog;
    catalog::AppEntry unsafe{};
    unsafe.id = "../escape";
    unsafe.executable_path = "/tmp/app.exe";
    EXPECT_FALSE(catalog.add_app(unsafe));
    EXPECT_FALSE(catalog::AppCatalog::create_desktop_entry(unsafe, "."));

    const std::filesystem::path path = "_tl_catalog_unicode.json";
    {
        std::ofstream output(path, std::ios::trunc);
        ASSERT_TRUE(output.good());
        output << R"({"version":1,"apps":[{"id":"unicode_app","name":"Caf\u00e9","executable_path":"/tmp/app.exe"}]})";
    }
    catalog::AppCatalog loaded;
    ASSERT_TRUE(loaded.load_from_file(path));
    const auto app = loaded.find_app("unicode_app");
    ASSERT_TRUE(app.has_value());
    EXPECT_EQ(app->name, "Caf\xC3\xA9");
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(AppCatalogTest, PersistsResourceLimits) {
    const std::filesystem::path path = "_tl_catalog_resource_limits.json";
    catalog::AppCatalog original;
    catalog::AppEntry entry{};
    entry.id = "limited_app";
    entry.name = "Limited app";
    entry.executable_path = "/tmp/app.exe";
    entry.cpu_limit_seconds = 7;
    entry.memory_limit_mib = 256;
    ASSERT_TRUE(original.add_app(entry));
    ASSERT_TRUE(original.save_to_file(path));

    catalog::AppCatalog loaded;
    ASSERT_TRUE(loaded.load_from_file(path));
    const auto restored = loaded.find_app("limited_app");
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->cpu_limit_seconds, 7U);
    EXPECT_EQ(restored->memory_limit_mib, 256U);

    std::error_code error;
    std::filesystem::remove(path, error);
}

}  // namespace
}  // namespace tradutorlinux
