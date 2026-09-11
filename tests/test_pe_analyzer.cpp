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

TEST(PeAnalyzerTest, CleanBinaryNotPacked) {
    PeInfo info{};
    SectionInfo sec_text{};
    sec_text.name = ".text";
    sec_text.characteristics = 0x60000020;  // RX
    sec_text.raw_data_size = 0x1000;
    sec_text.virtual_size = 0x1000;

    SectionInfo sec_rdata{};
    sec_rdata.name = ".rdata";
    sec_rdata.characteristics = 0x40000040;  // R
    sec_rdata.raw_data_size = 0x500;
    sec_rdata.virtual_size = 0x500;

    info.sections = {sec_text, sec_rdata};

    const PackerInspectionResult result = inspect_pe_packers(info);
    EXPECT_FALSE(result.is_packed);
    EXPECT_TRUE(result.packer_name.empty());
    EXPECT_TRUE(result.indicators.empty());
}

TEST(PeAnalyzerTest, DetectsUpxPacker) {
    PeInfo info{};
    SectionInfo upx0{};
    upx0.name = "UPX0";
    upx0.characteristics = 0xE0000080;  // RWX
    upx0.raw_data_size = 0;
    upx0.virtual_size = 0x10000;

    SectionInfo upx1{};
    upx1.name = "UPX1";
    upx1.characteristics = 0xE0000040;  // RWX
    upx1.raw_data_size = 0x4000;
    upx1.virtual_size = 0x4000;

    info.sections = {upx0, upx1};

    const PackerInspectionResult result = inspect_pe_packers(info);
    EXPECT_TRUE(result.is_packed);
    EXPECT_EQ(result.packer_name, "UPX");
    EXPECT_FALSE(result.indicators.empty());
}

TEST(PeAnalyzerTest, DetectsVmProtectAndThemida) {
    PeInfo info_vmp{};
    SectionInfo vmp0{};
    vmp0.name = ".vmp0";
    vmp0.characteristics = 0x60000020;
    info_vmp.sections = {vmp0};
    const PackerInspectionResult res_vmp = inspect_pe_packers(info_vmp);
    EXPECT_TRUE(res_vmp.is_packed);
    EXPECT_EQ(res_vmp.packer_name, "VMProtect");

    PeInfo info_themida{};
    SectionInfo thm{};
    thm.name = ".themida";
    thm.characteristics = 0x60000020;
    info_themida.sections = {thm};
    const PackerInspectionResult res_thm = inspect_pe_packers(info_themida);
    EXPECT_TRUE(res_thm.is_packed);
    EXPECT_EQ(res_thm.packer_name, "Themida");
}

TEST(PeAnalyzerTest, DetectsGenericWxSection) {
    PeInfo info{};
    SectionInfo sec{};
    sec.name = ".code";
    sec.characteristics = 0xA0000020;  // Executable (0x20000000) + Writable (0x80000000)
    sec.raw_data_size = 0x1000;
    sec.virtual_size = 0x1000;
    info.sections = {sec};

    const PackerInspectionResult result = inspect_pe_packers(info);
    EXPECT_TRUE(result.is_packed);
    EXPECT_EQ(result.packer_name, "Generic/Packer");
    ASSERT_EQ(result.indicators.size(), 1U);
    EXPECT_NE(result.indicators[0].find("secao W+X (.code)"), std::string::npos);
}

}  // namespace
}  // namespace tradutorlinux::pe

