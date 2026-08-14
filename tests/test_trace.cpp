#include "tradutorlinux/diagnostics/trace.hpp"

#include <array>
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

}  // namespace
}  // namespace tradutorlinux::diagnostics
