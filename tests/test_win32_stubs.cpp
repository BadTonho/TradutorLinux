#include "test_win32_common.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
#include "tradutorlinux/runtime/comdlg32.hpp"
#include "tradutorlinux/runtime/imm32.hpp"
#include "tradutorlinux/runtime/winmm.hpp"

#include <algorithm>
#include <iterator>
#include <string>

namespace tradutorlinux {
namespace {

TEST(Win32StubTest, SetupApiReportsEmptyEnumerationAndClearsOutputs) {
    void* const device_info = tl_SetupDiGetClassDevsA(nullptr, nullptr, nullptr, 0);
    ASSERT_EQ(device_info, reinterpret_cast<void*>(0x53455455ULL));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);

    EXPECT_EQ(tl_SetupDiEnumDeviceInfo(device_info, 0, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNoMoreFiles);
    EXPECT_EQ(tl_SetupDiEnumDeviceInterfaces(device_info, nullptr, nullptr, 0, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNoMoreFiles);

    std::uint32_t needed = 123U;
    EXPECT_EQ(tl_SetupDiGetDeviceInterfaceDetailA(device_info, nullptr, nullptr, 0, &needed,
                                                   nullptr), 0);
    EXPECT_EQ(needed, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);

    std::uint32_t reg_type = 0U;
    std::array<std::uint8_t, 8> property_buffer;
    property_buffer.fill(0xA5U);
    needed = 123U;
    EXPECT_EQ(tl_SetupDiGetDeviceRegistryPropertyA(
                  device_info, nullptr, 0, &reg_type, property_buffer.data(),
                  static_cast<std::uint32_t>(property_buffer.size()), &needed),
              0);
    EXPECT_EQ(reg_type, 1U);
    EXPECT_EQ(needed, 0U);
    EXPECT_EQ(property_buffer[0], 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);

    char instance_id[8];
    std::fill(std::begin(instance_id), std::end(instance_id), 'X');
    needed = 123U;
    EXPECT_EQ(tl_SetupDiGetDeviceInstanceIdA(device_info, nullptr, instance_id,
                                              sizeof(instance_id), &needed), 0);
    EXPECT_EQ(instance_id[0], '\0');
    EXPECT_EQ(needed, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);

    EXPECT_EQ(tl_CM_Get_Child(nullptr, 0, 0), 0x0000000DU);
    EXPECT_EQ(tl_SetupDiDestroyDeviceInfoList(device_info), 1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
}

TEST(Win32StubTest, GraphicsStubsClearOutputInterfacesAndReturnFailureHresults) {
    constexpr int kEFail = static_cast<int>(0x80004005U);
    constexpr int kNoInterface = static_cast<int>(0x80004002U);
    constexpr int kDxgiUnsupported = static_cast<int>(0x887A0004U);

    void* code = reinterpret_cast<void*>(0x1U);
    void* errors = reinterpret_cast<void*>(0x2U);
    EXPECT_EQ(tl_D3DCompile(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0,
                             &code, &errors),
              kEFail);
    EXPECT_EQ(code, nullptr);
    EXPECT_EQ(errors, nullptr);

    void* blob = reinterpret_cast<void*>(0x1U);
    void* error = reinterpret_cast<void*>(0x2U);
    EXPECT_EQ(tl_D3D12SerializeRootSignature(nullptr, 0, &blob, &error), kEFail);
    EXPECT_EQ(blob, nullptr);
    EXPECT_EQ(error, nullptr);

    void* factory = reinterpret_cast<void*>(0x1U);
    EXPECT_EQ(tl_CreateDXGIFactory1(nullptr, &factory), kDxgiUnsupported);
    EXPECT_EQ(factory, nullptr);

    void* direct_draw = reinterpret_cast<void*>(0x1U);
    EXPECT_EQ(tl_DirectDrawCreateEx(nullptr, &direct_draw, nullptr, nullptr), kNoInterface);
    EXPECT_EQ(direct_draw, nullptr);

    EXPECT_EQ(tl_Direct3DCreate9(0), 0);
    void* direct3d9 = reinterpret_cast<void*>(0x1U);
    EXPECT_EQ(tl_Direct3DCreate9Ex(0, &direct3d9), static_cast<int>(0x8876086AU));
    EXPECT_EQ(direct3d9, nullptr);

    void* swap_chain = reinterpret_cast<void*>(0x1U);
    void* device = reinterpret_cast<void*>(0x2U);
    EXPECT_EQ(tl_D3D10CreateDeviceAndSwapChain(nullptr, 0, nullptr, 0, 0, nullptr,
                                                 &swap_chain, &device),
              kNoInterface);
    EXPECT_EQ(swap_chain, nullptr);
    EXPECT_EQ(device, nullptr);

    void* shader = reinterpret_cast<void*>(0x1U);
    error = reinterpret_cast<void*>(0x2U);
    void* shader_hr = reinterpret_cast<void*>(0x3U);
    EXPECT_EQ(tl_D3DX10CompileFromMemory(nullptr, 0, nullptr, nullptr, nullptr, nullptr,
                                           nullptr, 0, 0, nullptr, &shader, &error, &shader_hr),
              kEFail);
    EXPECT_EQ(shader, nullptr);
    EXPECT_EQ(error, nullptr);
    EXPECT_EQ(shader_hr, nullptr);

    swap_chain = reinterpret_cast<void*>(0x1U);
    device = reinterpret_cast<void*>(0x2U);
    EXPECT_EQ(tl_D3D11CreateDeviceAndSwapChain(nullptr, 0, nullptr, 0, nullptr, 0, 0,
                                                 nullptr, &swap_chain, &device, nullptr, nullptr),
              kDxgiUnsupported);
    EXPECT_EQ(swap_chain, nullptr);
    EXPECT_EQ(device, nullptr);

    code = reinterpret_cast<void*>(0x1U);
    errors = reinterpret_cast<void*>(0x2U);
    shader_hr = reinterpret_cast<void*>(0x3U);
    EXPECT_EQ(tl_D3DX11CompileFromMemory(nullptr, 0, nullptr, nullptr, nullptr, nullptr,
                                           nullptr, 0, 0, nullptr, &code, &errors, &shader_hr),
              kEFail);
    EXPECT_EQ(code, nullptr);
    EXPECT_EQ(errors, nullptr);
    EXPECT_EQ(shader_hr, nullptr);
}

TEST(Win32StubTest, SensApiReportsItsConservativeNetworkContract) {
    EXPECT_EQ(tl_IsDestinationReachableW(nullptr, nullptr), 1);

    std::uint32_t flags = 0U;
    EXPECT_EQ(tl_IsNetworkAlive(&flags), 1);
    EXPECT_EQ(flags, 1U);
}

TEST(Win32StubTest, WinmmStubsKeepTheirExplicitCompatibilityContract) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    std::array<std::byte, 8> time_caps{};
    EXPECT_EQ(tl_timeGetDevCaps(invalid, time_caps.size()), 97U);
    EXPECT_EQ(tl_timeGetDevCaps(time_caps.data(), 1), 97U);
    EXPECT_EQ(tl_timeGetDevCaps(time_caps.data(), time_caps.size()), 0U);
    EXPECT_EQ(tl_PlaySoundA(nullptr, nullptr, 0), 1);
    constexpr std::uint16_t sound_name[] = {'t', 'e', 's', 't', 0};
    EXPECT_EQ(tl_PlaySoundW(sound_name, nullptr, 0), 1);
    EXPECT_EQ(tl_timeSetEvent(10, 0, nullptr, 0, 0), 1U);
    EXPECT_EQ(tl_timeKillEvent(1), 0U);
}

TEST(Win32StubTest, DebugAndShellDialogStubsReportUnsupported) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    std::uint64_t displacement = 0;
    EXPECT_EQ(tl_SymFromAddr(nullptr, 0x140000000ULL, &displacement, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SymFromAddr(nullptr, 0x140000000ULL, &displacement, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(displacement, 0U);

    std::array<std::uint8_t, 128> image{};
    image[0] = 'M';
    image[1] = 'Z';
    const std::uint32_t nt_offset = 0x40U;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(&nt_offset), sizeof(nt_offset),
                image.begin() + 0x3C);
    const std::uint32_t signature = 0x00004550U;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(&signature), sizeof(signature),
                image.begin() + nt_offset);
    EXPECT_EQ(tl_ImageNtHeader(invalid), nullptr);
    EXPECT_EQ(tl_ImageNtHeader(image.data()), image.data() + nt_offset);

    std::array<std::byte, 64> browse_info{};
    EXPECT_EQ(tl_SHBrowseForFolderW(browse_info.data()), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_SHBrowseForFolderW(nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32StubTest, GdiplusStubsRejectFakeObjectsAndClearOutputs) {
    void* token = reinterpret_cast<void*>(0x1U);
    EXPECT_EQ(tl_GdiplusStartup(&token, nullptr, nullptr), 1);
    EXPECT_EQ(token, nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    void* bitmap = reinterpret_cast<void*>(0x2U);
    EXPECT_EQ(tl_GdipCreateBitmapFromStream(nullptr, &bitmap), 1);
    EXPECT_EQ(bitmap, nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    void* clone = reinterpret_cast<void*>(0x3U);
    EXPECT_EQ(tl_GdipCloneImage(nullptr, &clone), 1);
    EXPECT_EQ(clone, nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    void* hbitmap = reinterpret_cast<void*>(0x4U);
    EXPECT_EQ(tl_GdipCreateHBITMAPFromBitmap(nullptr, &hbitmap, 0), 1);
    EXPECT_EQ(hbitmap, nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    EXPECT_EQ(tl_GdipDisposeImage(nullptr), 1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_GdipAlloc(32), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    tl_GdipFree(nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    tl_GdiplusShutdown(nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
}

TEST(Win32StubTest, ComdlgStubsRejectFalseSuccessAndReportDialogFailure) {
    constexpr std::uint32_t kCdErrDialogFailure = 0xFFFFU;
    std::array<std::byte, 256> dialog{};
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));

    EXPECT_EQ(tl_GetOpenFileNameA(invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CommDlgExtendedError(), 0x0001U);

    EXPECT_EQ(tl_GetOpenFileNameA(dialog.data()), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_CommDlgExtendedError(), kCdErrDialogFailure);
    EXPECT_EQ(tl_GetOpenFileNameW(dialog.data()), 0);
    EXPECT_EQ(tl_GetSaveFileNameA(dialog.data()), 0);
    EXPECT_EQ(tl_GetSaveFileNameW(dialog.data()), 0);
    EXPECT_EQ(tl_ChooseColorA(dialog.data()), 0);
    EXPECT_EQ(tl_ChooseColorW(dialog.data()), 0);
    EXPECT_EQ(tl_ChooseFontA(dialog.data()), 0);
    EXPECT_EQ(tl_ChooseFontW(dialog.data()), 0);
    EXPECT_EQ(tl_PrintDlgW(dialog.data()), 0);
    EXPECT_EQ(tl_CommDlgExtendedError(), kCdErrDialogFailure);

    EXPECT_EQ(tl_ChooseFontW(nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CommDlgExtendedError(), 0x0001U);
}

TEST(Win32StubTest, VersionStubsRejectFabricatedMetadataAndClearQueries) {
    std::uint32_t handle = 123U;
    EXPECT_EQ(tl_GetFileVersionInfoSizeA("test.exe", &handle), 0U);
    EXPECT_EQ(handle, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_GetFileVersionInfoSizeExW(0, nullptr, &handle), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    std::array<std::byte, 64> data;
    data.fill(std::byte{0xA5});
    EXPECT_EQ(tl_GetFileVersionInfoA("test.exe", 0, data.size(), data.data()), 0);
    EXPECT_EQ(data.front(), std::byte{0xA5});
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_GetFileVersionInfoExW(0, nullptr, 0, data.size(), data.data()), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    void* buffer = reinterpret_cast<void*>(0x1U);
    std::uint32_t length = 123U;
    EXPECT_EQ(tl_VerQueryValueA(nullptr, "\\", &buffer, &length), 0);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(length, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    buffer = reinterpret_cast<void*>(0x2U);
    length = 456U;
    EXPECT_EQ(tl_VerQueryValueW(nullptr, nullptr, &buffer, &length), 0);
    EXPECT_EQ(buffer, nullptr);
    EXPECT_EQ(length, 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    EXPECT_EQ(tl_GetFileVersionInfoA(nullptr, 0, 0, nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32StubTest, ImmStubsRejectFakeContextsAndCompositionSuccess) {
    void* window = reinterpret_cast<void*>(0x1U);
    void* context = reinterpret_cast<void*>(0x2U);
    void* form = reinterpret_cast<void*>(0x3U);

    EXPECT_EQ(tl_ImmGetContext(window), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_ImmReleaseContext(window, context), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_ImmSetCompositionWindow(context, form), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_ImmGetCompositionStringA(context, 0, nullptr, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_ImmGetCompositionStringW(context, 0, nullptr, 0), -1);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_ImmAssociateContext(window, context), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_ImmGetVirtualKey(window), 0U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_ImmSetCompositionFontA(context, form), 0);
    EXPECT_EQ(tl_ImmSetCompositionFontW(context, form), 0);
    EXPECT_EQ(tl_ImmSetCandidateWindow(context, form), 0);
    EXPECT_EQ(tl_ImmSetCompositionStringW(context, 0, nullptr, 0, nullptr, 0), 0);
    EXPECT_EQ(tl_ImmEscapeW(nullptr, context, 0, nullptr), 0);
    EXPECT_EQ(tl_ImmNotifyIME(context, 0, 0, 0), 0);

    EXPECT_EQ(tl_ImmGetContext(nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_ImmNotifyIME(nullptr, 0, 0, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32StubTest, DwmStubsRejectFakeCompositionAndClearOutputs) {
    constexpr std::int32_t kENotImpl = static_cast<std::int32_t>(0x80004001U);
    int enabled = 1;
    EXPECT_EQ(tl_DwmIsCompositionEnabled(&enabled), kENotImpl);
    EXPECT_EQ(enabled, 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    std::array<std::byte, 16> attributes;
    attributes.fill(std::byte{0xA5});
    EXPECT_EQ(tl_DwmGetWindowAttribute(nullptr, 0, attributes.data(), attributes.size()), kENotImpl);
    EXPECT_EQ(attributes.front(), std::byte{0});
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);

    std::uint32_t color = 0xFFFFFFFFU;
    int opaque = 1;
    EXPECT_EQ(tl_DwmGetColorizationColor(&color, &opaque), kENotImpl);
    EXPECT_EQ(color, 0U);
    EXPECT_EQ(opaque, 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_DwmSetWindowAttribute(nullptr, 0, nullptr, 0), kENotImpl);
    EXPECT_EQ(tl_DwmExtendFrameIntoClientArea(nullptr, nullptr), kENotImpl);
    EXPECT_EQ(tl_DwmEnableBlurBehindWindow(nullptr, nullptr), kENotImpl);
    EXPECT_EQ(tl_DwmFlush(), kENotImpl);
    EXPECT_EQ(tl_DwmDefWindowProc(nullptr, 0, 0, 0, nullptr), 0);

    EXPECT_EQ(tl_DwmIsCompositionEnabled(nullptr), static_cast<std::int32_t>(0x80070057U));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

TEST(Win32StubTest, UxThemeStubsRejectFakeHandlesAndClearOutputs) {
    constexpr std::int32_t kENotImpl = static_cast<std::int32_t>(0x80004001U);
    EXPECT_EQ(tl_SetWindowTheme(nullptr, nullptr, nullptr), kENotImpl);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_OpenThemeData(nullptr, nullptr), nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_CloseThemeData(nullptr), kENotImpl);
    EXPECT_EQ(tl_IsThemeActive(), 0);
    EXPECT_EQ(tl_IsAppThemed(), 0);

    std::uint32_t color = 0xFFFFFFFFU;
    EXPECT_EQ(tl_GetThemeColor(nullptr, 0, 0, 0, &color), kENotImpl);
    EXPECT_EQ(color, 0U);
    int metric = 123;
    EXPECT_EQ(tl_GetThemeMetric(nullptr, nullptr, 0, 0, 0, &metric), kENotImpl);
    EXPECT_EQ(metric, 0);

    void* hdc_out = reinterpret_cast<void*>(0x1U);
    EXPECT_EQ(tl_BeginBufferedPaint(nullptr, nullptr, 0, nullptr, &hdc_out), nullptr);
    EXPECT_EQ(hdc_out, nullptr);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorNotSupported);
    EXPECT_EQ(tl_BufferedPaintRenderAnimation(nullptr, nullptr), 0);
}

TEST(Win32StubTest, UnsupportedApisEmitTraceWithMechanismAndDetail) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("tl-stub-trace-" + std::to_string(static_cast<unsigned long long>(::getpid())));
    std::filesystem::remove_all(directory);
    ASSERT_TRUE(diagnostics::configure_trace_json_directory(directory));

    void* printer = reinterpret_cast<void*>(0x1U);
    EXPECT_EQ(tl_OpenPrinterW(nullptr, &printer, nullptr), 0);
    std::uint32_t thread_id = 123U;
    EXPECT_EQ(tl_CreateRemoteThread(nullptr, nullptr, 0, nullptr, nullptr, 0, &thread_id),
              nullptr);
    std::size_t written = 123U;
    EXPECT_EQ(tl_WriteProcessMemory(nullptr, nullptr, nullptr, 8, &written), 0);
    std::uint16_t* query_buffer = reinterpret_cast<std::uint16_t*>(0x1U);
    std::uint32_t bytes_returned = 99U;
    EXPECT_EQ(tl_WTSQuerySessionInformationW(nullptr, 0, 8, &query_buffer, &bytes_returned), 0);

    diagnostics::disable_trace_json_directory();

    std::string trace;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".jsonl") continue;
        std::ifstream input(entry.path());
        trace.append(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    }
    EXPECT_NE(trace.find("\"event\": \"OpenPrinterW\""), std::string::npos);
    EXPECT_NE(trace.find("\"event\": \"CreateRemoteThread\""), std::string::npos);
    EXPECT_NE(trace.find("\"event\": \"WriteProcessMemory\""), std::string::npos);
    EXPECT_NE(trace.find("\"event\": \"WTSQuerySessionInformationW\""), std::string::npos);
    EXPECT_NE(trace.find("\"mechanism\": \"stub\""), std::string::npos);
    EXPECT_NE(trace.find("\"status\": \"unsupported\""), std::string::npos);
    std::filesystem::remove_all(directory);
}

TEST(Win32StubTest, ProtectedCoreStubOutputsRejectUnmappedGuestPointers) {
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    EXPECT_EQ(tl_OpenPrinterW(nullptr, static_cast<void**>(invalid), nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_WTSEnumerateSessionsW(nullptr, 0U, 1U, static_cast<void**>(invalid), nullptr), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_WriteProcessMemory(nullptr, nullptr, nullptr, 8U,
                                    static_cast<std::size_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_SetupDiGetDeviceInterfaceDetailA(nullptr, nullptr, nullptr, 0U,
                                                   static_cast<std::uint32_t*>(invalid), nullptr),
              0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_D3DCompile(nullptr, 0U, nullptr, nullptr, nullptr, nullptr, nullptr, 0U, 0U,
                             static_cast<void**>(invalid), nullptr),
              static_cast<int>(0x80004005U));
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_TdhGetPropertySize(nullptr, 0U, nullptr, 0U, nullptr,
                                    static_cast<std::uint32_t*>(invalid)), 87U);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
}

}  // namespace
}  // namespace tradutorlinux
