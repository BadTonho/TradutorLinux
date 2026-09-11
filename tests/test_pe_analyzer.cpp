#include "pe_builder.hpp"
#include "tradutorlinux/pe/pe_analyzer.hpp"
#include "tradutorlinux/pe/pe_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace tradutorlinux::pe {
namespace {

using namespace tradutorlinux::pe::testutil;

TEST(PeAnalyzerTest, DefaultMitigationsDisabledWhenZero) {
    BuildSpec spec;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x10)};
    spec.dll_characteristics = 0;
    const std::vector<std::byte> pe_data = build(spec);
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    const MitigationInfo mitigations = inspect_pe_mitigations(pe_data, parse_result.info);
    EXPECT_TRUE(mitigations.has_mitigations);
    EXPECT_EQ(mitigations.raw_characteristics, 0);
    EXPECT_FALSE(mitigations.aslr);
    EXPECT_FALSE(mitigations.high_entropy_va);
    EXPECT_FALSE(mitigations.dep);
    EXPECT_FALSE(mitigations.cfg);
    EXPECT_FALSE(mitigations.no_seh);
    EXPECT_FALSE(mitigations.app_container);
    EXPECT_FALSE(mitigations.force_integrity);
}

TEST(PeAnalyzerTest, DetectsAslrDepCfgAndHighEntropy) {
    BuildSpec spec;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x10)};
    spec.dll_characteristics = kDllCharDynamicBase |
                               kDllCharHighEntropyVa |
                               kDllCharNxCompat |
                               kDllCharGuardCf;
    const std::vector<std::byte> pe_data = build(spec);
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    const MitigationInfo mitigations = inspect_pe_mitigations(pe_data, parse_result.info);
    EXPECT_TRUE(mitigations.has_mitigations);
    EXPECT_TRUE(mitigations.aslr);
    EXPECT_TRUE(mitigations.high_entropy_va);
    EXPECT_TRUE(mitigations.dep);
    EXPECT_TRUE(mitigations.cfg);
    EXPECT_FALSE(mitigations.no_seh);
    EXPECT_FALSE(mitigations.app_container);
    EXPECT_FALSE(mitigations.force_integrity);
}

TEST(PeAnalyzerTest, DetectsAppContainerIntegrityAndNoSeh) {
    BuildSpec spec;
    spec.section_data = {std::vector<std::byte>(0x10), std::vector<std::byte>(0x10)};
    spec.dll_characteristics = kDllCharAppContainer |
                               kDllCharForceIntegrity |
                               kDllCharNoSeh;
    const std::vector<std::byte> pe_data = build(spec);
    const ParseResult parse_result = parse_pe(pe_data);
    ASSERT_EQ(parse_result.status, ParseStatus::Success);

    const MitigationInfo mitigations = inspect_pe_mitigations(pe_data, parse_result.info);
    EXPECT_TRUE(mitigations.has_mitigations);
    EXPECT_FALSE(mitigations.aslr);
    EXPECT_FALSE(mitigations.dep);
    EXPECT_TRUE(mitigations.no_seh);
    EXPECT_TRUE(mitigations.app_container);
    EXPECT_TRUE(mitigations.force_integrity);
}

TEST(PeAnalyzerTest, HandlesTruncatedAndMalformedPeSafely) {
    PeInfo info{};
    // Empty data
    const MitigationInfo empty_res = inspect_pe_mitigations({}, info);
    EXPECT_FALSE(empty_res.has_mitigations);

    // Short data (< 64 bytes)
    std::vector<std::byte> short_data(32, std::byte{0});
    const MitigationInfo short_res = inspect_pe_mitigations(short_data, info);
    EXPECT_FALSE(short_res.has_mitigations);

    // Invalid DOS magic
    std::vector<std::byte> invalid_dos(128, std::byte{0});
    const MitigationInfo bad_dos_res = inspect_pe_mitigations(invalid_dos, info);
    EXPECT_FALSE(bad_dos_res.has_mitigations);

    // Truncated before DllCharacteristics
    BuildSpec spec;
    std::vector<std::byte> pe_data = build(spec);
    pe_data.resize(0x40 + 24 + 20);  // Truncated before optional header characteristics
    const MitigationInfo trunc_res = inspect_pe_mitigations(pe_data, info);
    EXPECT_FALSE(trunc_res.has_mitigations);
}

}  // namespace
}  // namespace tradutorlinux::pe

