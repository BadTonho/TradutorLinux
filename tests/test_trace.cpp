#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

namespace tradutorlinux::diagnostics {
namespace {

TEST(TraceTest, WritesStableStructuredLine) {
    const std::array fields{
        TraceField{"feature", "pe-loader"},
        TraceField{"phase", "1"},
    };
    std::ostringstream stream;

    write_trace(stream, TraceComponent::Runtime, TraceLevel::Error, "feature-unavailable", fields);

    EXPECT_EQ(stream.str(),
              "[tl][runtime][error] feature-unavailable feature=\"pe-loader\" phase=\"1\"\n");
}

TEST(TraceTest, EscapesQuotedAndMultilineValues) {
    const std::array fields{TraceField{"message", "linha \"um\"\nlinha dois"}};
    std::ostringstream stream;

    write_trace(stream, TraceComponent::Cli, TraceLevel::Warning, "input-warning", fields);

    EXPECT_EQ(stream.str(), "[tl][cli][warning] input-warning message=\"linha \\\"um\\\"\\nlinha dois\"\n");
}

TEST(TraceTest, NamesFailureCategories) {
    EXPECT_EQ(failure_category_name(FailureCategory::GuestMemory), "guest-memory");
    EXPECT_EQ(failure_category_name(FailureCategory::LinuxError), "linux-error");
    EXPECT_EQ(failure_category_name(FailureCategory::GuestSignal), "guest-signal");
}

TEST(TraceTest, WritesJsonEventImmediately) {
    const auto directory = std::filesystem::temp_directory_path() / "tl-trace-json-test";
    std::filesystem::remove_all(directory);
    ASSERT_TRUE(configure_trace_json_directory(directory));

    const std::array fields{TraceField{"action", "paint"}};
    std::ostringstream stream;
    write_trace(stream, TraceComponent::Gui, TraceLevel::Info, "window-created", fields);
    disable_trace_json_directory();

    std::size_t json_files = 0;
    std::filesystem::path json_path;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".jsonl") {
            ++json_files;
            json_path = entry.path();
        }
    }
    ASSERT_EQ(json_files, 1U);
    std::ifstream input(json_path);
    const std::string contents{std::istreambuf_iterator<char>{input}, {}};
    EXPECT_NE(contents.find("\"component\": \"gui\""), std::string::npos);
    EXPECT_NE(contents.find("\"event\": \"window-created\""), std::string::npos);
    EXPECT_NE(contents.find("\"action\": \"paint\""), std::string::npos);
    std::filesystem::remove_all(directory);
}

}  // namespace
}  // namespace tradutorlinux::diagnostics
