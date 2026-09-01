#include "tradutorlinux/runtime/guest_context.hpp"

#include <gtest/gtest.h>

namespace tradutorlinux::runtime {

TEST(GuestContextTest, ScopeRestoresPreviousContext) {
    GuestContext first;
    GuestContext second;

    EXPECT_EQ(current_guest_context(), &default_guest_context());
    {
        GuestContextScope first_scope(first);
        EXPECT_EQ(current_guest_context(), &first);
        {
            GuestContextScope second_scope(second);
            EXPECT_EQ(current_guest_context(), &second);
        }
        EXPECT_EQ(current_guest_context(), &first);
    }
    EXPECT_EQ(current_guest_context(), &default_guest_context());
}

TEST(GuestContextTest, ProcessStateDoesNotLeakBetweenContexts) {
    GuestContext first;
    GuestContext second;

    {
        GuestContextScope scope(first);
        first.last_error = 5;
        first.prefix_path = "/tmp/first-prefix";
        first.module_file_name = "first.exe";
        first.image_base = reinterpret_cast<const std::byte*>(0x1000);
    }
    {
        GuestContextScope scope(second);
        EXPECT_EQ(second.last_error, 0U);
        EXPECT_TRUE(second.prefix_path.empty());
        EXPECT_TRUE(second.module_file_name.empty());
        EXPECT_EQ(second.image_base, nullptr);
    }
}

TEST(GuestContextTest, ContextCarriesIndependentStandardHandles) {
    GuestContext first;
    GuestContext second;
    int first_handle = 1;
    int second_handle = 2;

    first.standard_handles[1] = &first_handle;
    second.standard_handles[1] = &second_handle;

    EXPECT_EQ(first.standard_handles[1], &first_handle);
    EXPECT_EQ(second.standard_handles[1], &second_handle);
}

TEST(GuestContextTest, FileTablesAreIndependent) {
    GuestContext first;
    GuestContext second;
    first.files[0].used = true;
    first.files[0].path = "first.txt";
    second.files[0].used = true;
    second.files[0].path = "second.txt";

    EXPECT_TRUE(first.files[0].used);
    EXPECT_TRUE(second.files[0].used);
    EXPECT_EQ(first.files[0].path, "first.txt");
    EXPECT_EQ(second.files[0].path, "second.txt");
}

}  // namespace tradutorlinux::runtime
