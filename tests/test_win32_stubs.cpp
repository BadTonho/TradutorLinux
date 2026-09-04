#include "test_win32_common.hpp"

#include "tradutorlinux/diagnostics/trace.hpp"
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
    EXPECT_EQ(tl_PlaySoundA(nullptr, nullptr, 0), 1);
    constexpr std::uint16_t sound_name[] = {'t', 'e', 's', 't', 0};
    EXPECT_EQ(tl_PlaySoundW(sound_name, nullptr, 0), 1);
    EXPECT_EQ(tl_timeSetEvent(10, 0, nullptr, 0, 0), 1U);
    EXPECT_EQ(tl_timeKillEvent(1), 0U);
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

}  // namespace
}  // namespace tradutorlinux
