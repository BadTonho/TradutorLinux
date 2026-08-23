#include "tradutorlinux/runtime/unwind.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace tradutorlinux {
namespace {

using pe::RuntimeFunction;
using pe::UnwindCode;
using pe::UnwindInfo;
using pe::UnwindOperation;
using runtime::ContextAmd64;

constexpr std::uint32_t kFunctionTableRva = 0x300;
constexpr std::uint32_t kHandlerRva = 0x220;

UnwindCode code(const UnwindOperation operation, const std::uint8_t info = 0,
                const std::uint32_t operand = 0, const std::uint8_t offset = 4) {
    return {.code_offset = offset,
            .operation = operation,
            .operation_info = info,
            .operand = operand};
}

RuntimeFunction function(const std::uint32_t begin, const std::uint32_t end,
                         UnwindInfo unwind = {}) {
    return {.begin_rva = begin, .end_rva = end, .unwind_info_rva = 0x180, .unwind = std::move(unwind)};
}

__attribute__((noinline, ms_abi)) void capture_with_nonvolatile_registers(
    ContextAmd64* const context, const runtime::M128A value) {
    asm volatile(
        "movabsq $0x1122334455667788, %%rbx\n\t"
        "movabsq $0x8877665544332211, %%r12\n\t"
        "movdqu %0, %%xmm6\n\t"
        :
        : "m"(value)
        : "rbx", "r12", "xmm6", "memory");
    tl_RtlCaptureContext(context);
}

class UnwindTest : public ::testing::Test {
protected:
    void SetUp() override {
        image.assign(0x1000, std::byte{0});
    }

    void TearDown() override {
        runtime::clear_guest_unwind_view();
    }

    void set_functions(std::vector<RuntimeFunction> next) {
        functions = std::move(next);
        runtime::set_guest_unwind_view(image.data(), image.size(), kFunctionTableRva, functions);
    }

    std::uint32_t* raw_entry(const std::size_t index = 0) {
        return reinterpret_cast<std::uint32_t*>(image.data() + kFunctionTableRva + index * 12U);
    }

    std::vector<std::byte> image;
    std::vector<RuntimeFunction> functions;
};

TEST_F(UnwindTest, LookupAndPcToFileHeaderUseActiveImageOnly) {
    set_functions({function(0x100, 0x200)});
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(image.data());

    std::uint64_t image_base = 0;
    EXPECT_EQ(tl_RtlLookupFunctionEntry(base + 0x150, &image_base, nullptr), raw_entry());
    EXPECT_EQ(image_base, base);
    EXPECT_EQ(tl_RtlLookupFunctionEntry(base + 0x250, &image_base, nullptr), nullptr);
    EXPECT_EQ(image_base, 0U);

    void* header = nullptr;
    EXPECT_EQ(tl_RtlPcToFileHeader(reinterpret_cast<void*>(base + 0x150), &header), image.data());
    EXPECT_EQ(header, image.data());
    EXPECT_EQ(tl_RtlPcToFileHeader(reinterpret_cast<void*>(base + 0x250), &header), image.data());
    EXPECT_EQ(header, image.data());
    EXPECT_EQ(tl_RtlPcToFileHeader(reinterpret_cast<void*>(base + image.size()), &header), nullptr);
    EXPECT_EQ(header, nullptr);
}

TEST_F(UnwindTest, CapturesAmd64ControlAndFloatingContext) {
    ContextAmd64 context{};
    const runtime::M128A xmm6{.low = 0xAABBCCDDEEFF0011U, .high = 0x102030405060708};

    capture_with_nonvolatile_registers(&context, xmm6);

    EXPECT_EQ(context.context_flags, runtime::kContextFull);
    EXPECT_NE(context.rip, 0U);
    EXPECT_NE(context.rsp, 0U);
    EXPECT_NE(context.mx_csr, 0U);
    EXPECT_EQ(context.rbx, 0x1122334455667788U);
    EXPECT_EQ(context.r12, 0x8877665544332211U);
    EXPECT_EQ(context.floating[16].low, xmm6.low);
    EXPECT_EQ(context.floating[16].high, xmm6.high);
}

TEST_F(UnwindTest, SehLayoutsAndVectoredTokensAreValidated) {
    set_functions({});
    EXPECT_EQ(sizeof(runtime::ExceptionRecordAmd64), 152U);
    EXPECT_EQ(sizeof(runtime::ExceptionPointersAmd64), 16U);
    EXPECT_EQ(sizeof(runtime::DispatcherContextAmd64), 80U);

    EXPECT_EQ(runtime::add_vectored_exception_handler(1, nullptr), nullptr);
    void* const first = runtime::add_vectored_exception_handler(1, image.data() + 0x100U);
    void* const last = runtime::add_vectored_exception_handler(0, image.data() + 0x110U);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(last, nullptr);
    EXPECT_NE(first, last);
    EXPECT_EQ(runtime::remove_vectored_exception_handler(first), 1U);
    EXPECT_EQ(runtime::remove_vectored_exception_handler(first), 0U);
    EXPECT_EQ(runtime::remove_vectored_exception_handler(last), 1U);
    EXPECT_EQ(runtime::remove_vectored_exception_handler(nullptr), 0U);
}

TEST_F(UnwindTest, DoesNotExposeExceptionHandlerWhileInProlog) {
    UnwindInfo unwind;
    unwind.prolog_size = 8;
    unwind.flags = 1;
    unwind.handler_rva = kHandlerRva;
    unwind.handler_data_rva = kHandlerRva + 4U;
    unwind.codes = {code(UnwindOperation::AllocSmall, 0, 8, 8)};
    set_functions({function(0x100, 0x200, unwind)});
    std::array<std::uint64_t, 4> stack{};
    stack[0] = 0xD00DU;
    ContextAmd64 context{};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    void* handler_data = reinterpret_cast<void*>(0x1U);

    EXPECT_EQ(tl_RtlVirtualUnwind(1, reinterpret_cast<std::uintptr_t>(image.data()),
                                  reinterpret_cast<std::uintptr_t>(image.data()) + 0x104U,
                                  raw_entry(), &context, &handler_data, nullptr, nullptr), nullptr);
    EXPECT_EQ(handler_data, nullptr);
    EXPECT_EQ(context.rip, 0xD00DU);
}

TEST_F(UnwindTest, UnwindsStackAllocationAndPushedNonvolatileRegister) {
    UnwindInfo unwind;
    unwind.codes = {code(UnwindOperation::PushNonVol, 3, 0, 8),
                    code(UnwindOperation::AllocSmall, 4, 40, 4)};
    set_functions({function(0x100, 0x200, unwind)});
    std::array<std::uint64_t, 16> stack{};
    stack[0] = 0xBEEFU;
    stack[6] = 0xDEADBEEFU;
    ContextAmd64 context{};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    std::uint64_t frame = 0;

    EXPECT_EQ(tl_RtlVirtualUnwind(0, reinterpret_cast<std::uintptr_t>(image.data()),
                                  reinterpret_cast<std::uintptr_t>(image.data()) + 0x150,
                                  raw_entry(), &context, nullptr, &frame, nullptr), nullptr);
    EXPECT_EQ(context.rbx, 0xBEEFU);
    EXPECT_EQ(context.rip, 0xDEADBEEFU);
    EXPECT_EQ(context.rsp, reinterpret_cast<std::uintptr_t>(stack.data() + 7));
    EXPECT_EQ(frame, reinterpret_cast<std::uintptr_t>(stack.data() + 6));
}

TEST_F(UnwindTest, UnwindsV2BodyAndLeavesContextUntouchedInV2Epilog) {
    UnwindInfo unwind;
    unwind.version = 2;
    unwind.codes = {code(UnwindOperation::AllocSmall, 0, 8)};
    unwind.epilogs = {{.begin_rva = 0x180, .end_rva = 0x190}};
    set_functions({function(0x100, 0x200, unwind)});
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(image.data());
    std::array<std::uint64_t, 4> stack{};
    stack[1] = 0xC0FFEEU;

    ContextAmd64 context{};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    EXPECT_EQ(tl_RtlVirtualUnwind(0, base, base + 0x150, raw_entry(), &context,
                                  nullptr, nullptr, nullptr), nullptr);
    EXPECT_EQ(context.rip, 0xC0FFEEU);
    EXPECT_EQ(context.rsp, reinterpret_cast<std::uintptr_t>(stack.data() + 2));

    // No prólogo, a alocação ainda não aconteceu e, portanto, o código de
    // unwind não pode tocá-la.
    stack[0] = 0xF00DU;
    context = {};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    EXPECT_EQ(tl_RtlVirtualUnwind(0, base, base + 0x102, raw_entry(), &context,
                                  nullptr, nullptr, nullptr), nullptr);
    EXPECT_EQ(context.rip, 0xF00DU);
    EXPECT_EQ(context.rsp, reinterpret_cast<std::uintptr_t>(stack.data() + 1));

    context = {};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    context.rip = base + 0x185;
    const ContextAmd64 before = context;
    void* handler_data = reinterpret_cast<void*>(0x1234U);
    std::uint64_t frame = 0x9876U;
    EXPECT_EQ(tl_RtlVirtualUnwind(0, base, base + 0x185, raw_entry(), &context,
                                  &handler_data, &frame, nullptr), nullptr);
    EXPECT_EQ(std::memcmp(&context, &before, sizeof(context)), 0);
    EXPECT_EQ(handler_data, reinterpret_cast<void*>(0x1234U));
    EXPECT_EQ(frame, 0x9876U);
}

TEST_F(UnwindTest, UnwindsLargeSavedRegistersAndXmm) {
    UnwindInfo unwind;
    unwind.codes = {code(UnwindOperation::SaveXmm128, 6, 0, 9),
                    code(UnwindOperation::SaveNonVolFar, 12, 16, 8),
                    code(UnwindOperation::SaveNonVol, 3, 32, 7),
                    code(UnwindOperation::AllocLarge, 0, 40, 4)};
    set_functions({function(0x100, 0x200, unwind)});
    std::array<std::uint64_t, 24> stack{};
    runtime::M128A xmm{.low = 0x1122U, .high = 0x3344};
    std::memcpy(stack.data(), &xmm, sizeof(xmm));
    stack[2] = 0x1234U;
    stack[4] = 0x5678U;
    stack[5] = 0xFEEDU;
    ContextAmd64 context{};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());

    tl_RtlVirtualUnwind(0, reinterpret_cast<std::uintptr_t>(image.data()),
                        reinterpret_cast<std::uintptr_t>(image.data()) + 0x150,
                        raw_entry(), &context, nullptr, nullptr, nullptr);
    EXPECT_EQ(context.floating[16].low, xmm.low);
    EXPECT_EQ(context.floating[16].high, xmm.high);
    EXPECT_EQ(context.r12, 0x1234U);
    EXPECT_EQ(context.rbx, 0x5678U);
    EXPECT_EQ(context.rip, 0xFEEDU);
}

TEST_F(UnwindTest, UnwindsFrameMachineFrameHandlersAndChains) {
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(image.data());
    std::array<std::uint64_t, 24> stack{};
    ContextAmd64 context{};
    context.rbp = reinterpret_cast<std::uintptr_t>(stack.data() + 8);
    stack[6] = 0xCAFEU;

    UnwindInfo frame_unwind;
    frame_unwind.frame_register = 5;
    frame_unwind.frame_offset = 1;
    frame_unwind.codes = {code(UnwindOperation::SetFpReg)};
    set_functions({function(0x100, 0x200, frame_unwind)});
    tl_RtlVirtualUnwind(0, base, base + 0x150, raw_entry(), &context, nullptr, nullptr, nullptr);
    EXPECT_EQ(context.rip, 0xCAFEU);

    UnwindInfo machine_unwind;
    machine_unwind.codes = {code(UnwindOperation::PushMachFrame)};
    set_functions({function(0x100, 0x200, machine_unwind)});
    context = {};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    stack[0] = 0xABCDU;
    stack[3] = reinterpret_cast<std::uintptr_t>(stack.data() + 12);
    tl_RtlVirtualUnwind(0, base, base + 0x150, raw_entry(), &context, nullptr, nullptr, nullptr);
    EXPECT_EQ(context.rip, 0xABCDU);
    EXPECT_EQ(context.rsp, reinterpret_cast<std::uintptr_t>(stack.data() + 12));

    UnwindInfo chained;
    chained.has_chained_function = true;
    chained.chained_begin_rva = 0x200;
    chained.chained_end_rva = 0x280;
    chained.chained_unwind_info_rva = 0x1A0;
    chained.codes = {code(UnwindOperation::AllocSmall, 0, 8)};
    UnwindInfo terminal;
    terminal.flags = 1;
    terminal.handler_rva = kHandlerRva;
    terminal.handler_data_rva = kHandlerRva + 4;
    terminal.codes = {code(UnwindOperation::PushNonVol, 3)};
    RuntimeFunction first = function(0x100, 0x180, chained);
    RuntimeFunction second = function(0x200, 0x280, terminal);
    second.unwind_info_rva = 0x1A0;
    set_functions({first, second});
    context = {};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    stack[0] = 0xFACEU;
    stack[1] = 0xBEEFU;
    stack[2] = 0xD00DU;
    void* handler_data = nullptr;
    EXPECT_EQ(tl_RtlVirtualUnwind(1, base, base + 0x120, raw_entry(), &context,
                                  &handler_data, nullptr, nullptr),
              image.data() + kHandlerRva);
    EXPECT_EQ(handler_data, image.data() + kHandlerRva + 4);
    EXPECT_EQ(context.rbx, 0xBEEFU);
    EXPECT_EQ(context.rip, 0xD00DU);

    UnwindInfo uhandler;
    uhandler.flags = 2;
    uhandler.handler_rva = kHandlerRva;
    uhandler.handler_data_rva = kHandlerRva + 4;
    set_functions({function(0x100, 0x200, uhandler)});
    context = {};
    context.rsp = reinterpret_cast<std::uintptr_t>(stack.data());
    stack[0] = 0xD00DU;
    handler_data = nullptr;
    EXPECT_EQ(tl_RtlVirtualUnwind(2, base, base + 0x150, raw_entry(), &context,
                                  &handler_data, nullptr, nullptr),
              image.data() + kHandlerRva);
    EXPECT_EQ(handler_data, image.data() + kHandlerRva + 4);
}

}  // namespace
}  // namespace tradutorlinux
