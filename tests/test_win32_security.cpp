#include "test_win32_common.hpp"

namespace tradutorlinux {
namespace {
class SecurityPrefixFixture {
public:
    SecurityPrefixFixture()
        : root_(std::filesystem::temp_directory_path() /
                ("tl-security-" + std::to_string(static_cast<unsigned long long>(::getpid())) +
                 "-" + std::to_string(++next_id_))) {
        static_cast<void>(prefix::initialize_prefix(root_));
        set_guest_prefix_path(root_);
        const std::filesystem::path file = root_ / "drive_c" / "security.bin";
        std::FILE* const stream = std::fopen(file.c_str(), "wb");
        if (stream != nullptr) {
            std::fclose(stream);
        }
    }

    ~SecurityPrefixFixture() {
        set_guest_prefix_path({});
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::vector<std::uint16_t> file_name() const {
        const std::u16string value = u"C:\\security.bin";
        std::vector<std::uint16_t> result(value.begin(), value.end());
        result.push_back(0);
        return result;
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
    inline static std::uint32_t next_id_{};
};

TEST(Win32SecurityTest, Amd64LayoutsAndVirtualTokenSidAreConsistent) {
    EXPECT_EQ(sizeof(abi::GuestSidHeader), 8U);
    EXPECT_EQ(sizeof(abi::GuestTokenUser), 16U);
    EXPECT_EQ(sizeof(abi::GuestTokenElevation), 4U);
    EXPECT_EQ(sizeof(abi::GuestSecurityDescriptor), 40U);
    EXPECT_EQ(sizeof(abi::GuestAcl), 8U);
    EXPECT_EQ(sizeof(abi::GuestTrusteeW), 32U);
    EXPECT_EQ(sizeof(abi::GuestExplicitAccessW), 48U);

    SecurityPrefixFixture ctx;
    void* token = nullptr;
    ASSERT_EQ(tl_OpenProcessToken(tl_GetCurrentProcess(), abi::kTokenQuery, &token), 1);
    std::uint32_t required = 0;
    EXPECT_EQ(tl_GetTokenInformation(token, abi::kTokenUser, nullptr, 0, &required), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
    ASSERT_GT(required, sizeof(abi::GuestTokenUser));
    std::vector<std::byte> data(required);
    ASSERT_EQ(tl_GetTokenInformation(token, abi::kTokenUser, data.data(), required, &required), 1);
    const auto* const user = reinterpret_cast<const abi::GuestTokenUser*>(data.data());
    ASSERT_EQ(tl_IsValidSid(user->user.sid), 1);
    const std::uint32_t sid_length = tl_GetLengthSid(user->user.sid);
    ASSERT_GT(sid_length, 8U);
    std::array<std::byte, 64> copied{};
    ASSERT_EQ(tl_CopySid(static_cast<std::uint32_t>(copied.size()), copied.data(), user->user.sid), 1);
    EXPECT_EQ(tl_EqualSid(copied.data(), user->user.sid), 1);

    abi::GuestTokenElevation elevation{};
    required = 0;
    EXPECT_EQ(tl_GetTokenInformation(token, abi::kTokenElevation, nullptr, 0, &required), 0);
    EXPECT_EQ(required, sizeof(elevation));
    ASSERT_EQ(tl_GetTokenInformation(token, abi::kTokenElevation, &elevation,
                                     sizeof(elevation), &required),
              1);
    EXPECT_EQ(elevation.token_is_elevated, 0U);

    const std::array<std::uint8_t, 6> nt_authority{0, 0, 0, 0, 0, 5};
    void* allocated = nullptr;
    ASSERT_EQ(tl_AllocateAndInitializeSid(nt_authority.data(), 1, 42, 0, 0, 0, 0, 0, 0, 0,
                                          &allocated),
              1);
    ASSERT_NE(allocated, nullptr);
    EXPECT_EQ(tl_IsValidSid(allocated), 1);
    EXPECT_EQ(tl_FreeSid(allocated), nullptr);

    std::array<std::byte, 32> administrators{};
    std::uint32_t administrators_size = 0;
    EXPECT_EQ(tl_CreateWellKnownSid(abi::kWinBuiltinAdministratorsSid, nullptr, nullptr,
                                    &administrators_size),
              0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInsufficientBuffer);
    ASSERT_EQ(tl_CreateWellKnownSid(abi::kWinBuiltinAdministratorsSid, nullptr,
                                    administrators.data(), &administrators_size),
              1);
    int member = 1;
    ASSERT_EQ(tl_CheckTokenMembership(token, user->user.sid, &member), 1);
    EXPECT_EQ(member, 1);
    ASSERT_EQ(tl_CheckTokenMembership(token, administrators.data(), &member), 1);
    EXPECT_EQ(member, 0);
    EXPECT_EQ(tl_CloseHandle(token), 1);
}

TEST(Win32SecurityTest, NamedDaclPersistsAndPrefixesRemainIsolated) {
    SecurityPrefixFixture first;
    std::vector<std::uint16_t> name = first.file_name();
    void* owner = nullptr;
    void* dacl = nullptr;
    void* descriptor = nullptr;
    ASSERT_EQ(tl_GetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kOwnerSecurityInformation | abi::kDaclSecurityInformation,
                                       &owner, nullptr, &dacl, nullptr, &descriptor),
              abi::kErrorSuccess);
    ASSERT_NE(owner, nullptr);
    ASSERT_NE(dacl, nullptr);
    ASSERT_NE(descriptor, nullptr);

    abi::GuestTrusteeW trustee{};
    tl_BuildTrusteeWithSidW(&trustee, owner);
    abi::GuestExplicitAccessW entry{};
    entry.access_permissions = abi::kGenericRead | abi::kGenericWrite;
    entry.access_mode = abi::kGrantAccess;
    entry.trustee = trustee;
    void* new_acl = nullptr;
    ASSERT_EQ(tl_SetEntriesInAclW(1, &entry, dacl, &new_acl), abi::kErrorSuccess);
    ASSERT_NE(new_acl, nullptr);
    ASSERT_EQ(tl_SetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kDaclSecurityInformation, nullptr, nullptr, new_acl,
                                       nullptr, 0),
              abi::kErrorSuccess);
    EXPECT_EQ(tl_LocalFree(descriptor), nullptr);
    EXPECT_EQ(tl_LocalFree(new_acl), nullptr);

    set_guest_prefix_path({});
    set_guest_prefix_path(first.root());
    owner = nullptr;
    dacl = nullptr;
    descriptor = nullptr;
    ASSERT_EQ(tl_GetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kOwnerSecurityInformation | abi::kDaclSecurityInformation,
                                       &owner, nullptr, &dacl, nullptr, &descriptor),
              abi::kErrorSuccess);
    const auto* const stored_acl = static_cast<const abi::GuestAcl*>(dacl);
    ASSERT_EQ(stored_acl->ace_count, 2U);

    abi::GuestExplicitAccessW revoke{};
    revoke.access_mode = abi::kRevokeAccess;
    tl_BuildTrusteeWithSidW(&revoke.trustee, owner);
    void* revoked_acl = nullptr;
    ASSERT_EQ(tl_SetEntriesInAclW(1, &revoke, dacl, &revoked_acl), abi::kErrorSuccess);
    ASSERT_NE(revoked_acl, nullptr);
    EXPECT_EQ(static_cast<const abi::GuestAcl*>(revoked_acl)->ace_count, 0U);
    EXPECT_EQ(tl_LocalFree(revoked_acl), nullptr);
    EXPECT_EQ(tl_LocalFree(descriptor), nullptr);

    const std::filesystem::path second_root = first.root().parent_path() /
                                              (first.root().filename().string() + "-second");
    ASSERT_TRUE(prefix::initialize_prefix(second_root));
    std::FILE* const stream = std::fopen((second_root / "drive_c" / "security.bin").c_str(), "wb");
    ASSERT_NE(stream, nullptr);
    std::fclose(stream);
    set_guest_prefix_path(second_root);
    owner = nullptr;
    dacl = nullptr;
    descriptor = nullptr;
    ASSERT_EQ(tl_GetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kOwnerSecurityInformation | abi::kDaclSecurityInformation,
                                       &owner, nullptr, &dacl, nullptr, &descriptor),
              abi::kErrorSuccess);
    const auto* const isolated_acl = static_cast<const abi::GuestAcl*>(dacl);
    EXPECT_EQ(isolated_acl->ace_count, 1U);
    EXPECT_EQ(tl_LocalFree(descriptor), nullptr);
    set_guest_prefix_path(first.root());
    std::error_code error;
    std::filesystem::remove_all(second_root, error);
}

TEST(Win32SecurityTest, RejectsUnsupportedAclInputsAndExternalPaths) {
    SecurityPrefixFixture ctx;
    abi::GuestExplicitAccessW unsupported{};
    unsupported.inheritance = 1;
    void* new_acl = nullptr;
    EXPECT_EQ(tl_SetEntriesInAclW(1, &unsupported, nullptr, &new_acl), abi::kErrorNotSupported);

    std::vector<std::uint16_t> file_name = ctx.file_name();
    void* dacl = nullptr;
    void* descriptor = nullptr;
    EXPECT_EQ(tl_GetNamedSecurityInfoW(file_name.data(), abi::kSeFileObject,
                                       abi::kSaclSecurityInformation, nullptr, nullptr, &dacl,
                                       nullptr, &descriptor),
              abi::kErrorNotSupported);

    std::vector<std::uint16_t> external{u'Z', u':', u'\\', u't', u'm', u'p', 0};
    EXPECT_EQ(tl_GetNamedSecurityInfoW(external.data(), abi::kSeFileObject,
                                       abi::kDaclSecurityInformation, nullptr, nullptr, nullptr,
                                       nullptr, &descriptor),
              abi::kErrorAccessDenied);
    EXPECT_EQ(tl_GetNamedSecurityInfoW(nullptr, abi::kSeFileObject,
                                       abi::kDaclSecurityInformation, nullptr, nullptr, nullptr,
                                       nullptr, &descriptor),
              abi::kErrorInvalidParameter);
}

TEST(Win32SecurityTest, ProtectedTokenAndSidBuffersRejectUnmappedPointers) {
    SecurityPrefixFixture ctx;
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));

    EXPECT_EQ(tl_OpenProcessToken(tl_GetCurrentProcess(), abi::kTokenQuery,
                                  static_cast<void**>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    void* token = nullptr;
    ASSERT_EQ(tl_OpenProcessToken(tl_GetCurrentProcess(), abi::kTokenQuery, &token), 1);
    std::uint32_t return_length = 0;
    abi::GuestTokenElevation elevation{};
    EXPECT_EQ(tl_GetTokenInformation(token, abi::kTokenElevation, invalid,
                                     sizeof(elevation), &return_length), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetTokenInformation(token, abi::kTokenElevation, &elevation,
                                     sizeof(elevation), static_cast<std::uint32_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CloseHandle(token), 1);

    const std::array<std::uint8_t, 6> authority{0, 0, 0, 0, 0, 5};
    void* sid = nullptr;
    ASSERT_EQ(tl_AllocateAndInitializeSid(authority.data(), 1, 42, 0, 0, 0, 0, 0, 0, 0,
                                          &sid), 1);
    ASSERT_NE(sid, nullptr);
    EXPECT_EQ(tl_AllocateAndInitializeSid(invalid, 1, 42, 0, 0, 0, 0, 0, 0, 0,
                                          &sid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_AllocateAndInitializeSid(authority.data(), 1, 42, 0, 0, 0, 0, 0, 0, 0,
                                          static_cast<void**>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    std::array<std::byte, 64> sid_copy{};
    EXPECT_EQ(tl_CopySid(static_cast<std::uint32_t>(sid_copy.size()), invalid, sid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CopySid(static_cast<std::uint32_t>(sid_copy.size()), sid_copy.data(), invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    std::uint32_t sid_size = 32;
    EXPECT_EQ(tl_CreateWellKnownSid(abi::kWinWorldSid, nullptr, invalid, &sid_size), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CreateWellKnownSid(abi::kWinWorldSid, nullptr, sid_copy.data(),
                                    static_cast<std::uint32_t*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    int is_member = 0;
    EXPECT_EQ(tl_CheckTokenMembership(nullptr, sid, static_cast<int*>(invalid)), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_CheckTokenMembership(nullptr, sid, &is_member), 1);

    abi::GuestTrusteeW trustee{};
    tl_BuildTrusteeWithSidW(invalid, sid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    tl_BuildTrusteeWithSidW(&trustee, sid);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorSuccess);
    EXPECT_EQ(tl_FreeSid(sid), nullptr);
}

TEST(Win32SecurityTest, ProtectedAclAndDescriptorBuffersRejectUnmappedPointers) {
    SecurityPrefixFixture ctx;
    auto* const invalid = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000U));
    std::vector<std::uint16_t> name = ctx.file_name();

    abi::GuestSecurityDescriptor descriptor{};
    EXPECT_EQ(tl_InitializeSecurityDescriptor(invalid, abi::kSecurityDescriptorRevision), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    ASSERT_EQ(tl_InitializeSecurityDescriptor(&descriptor, abi::kSecurityDescriptorRevision), 1);
    EXPECT_EQ(tl_SetSecurityDescriptorDacl(invalid, 1, nullptr, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetSecurityDescriptorDacl(&descriptor, 1, invalid, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    void* owner = nullptr;
    void* dacl = nullptr;
    void* named_descriptor = nullptr;
    ASSERT_EQ(tl_GetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kOwnerSecurityInformation | abi::kDaclSecurityInformation,
                                       &owner, nullptr, &dacl, nullptr, &named_descriptor),
              abi::kErrorSuccess);
    ASSERT_NE(owner, nullptr);
    ASSERT_NE(dacl, nullptr);
    ASSERT_NE(named_descriptor, nullptr);

    abi::GuestTrusteeW trustee{};
    tl_BuildTrusteeWithSidW(&trustee, owner);
    abi::GuestExplicitAccessW entry{};
    entry.access_permissions = abi::kGenericRead;
    entry.access_mode = abi::kGrantAccess;
    entry.trustee = trustee;

    void* new_acl = nullptr;
    EXPECT_EQ(tl_SetEntriesInAclW(1, invalid, dacl, &new_acl), abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetEntriesInAclW(1, &entry, dacl, static_cast<void**>(invalid)),
              abi::kErrorInvalidParameter);
    ASSERT_EQ(tl_SetEntriesInAclW(1, &entry, dacl, &new_acl), abi::kErrorSuccess);
    ASSERT_NE(new_acl, nullptr);

    EXPECT_EQ(tl_SetSecurityDescriptorDacl(&descriptor, 1, new_acl, 0), 1);
    EXPECT_EQ(tl_SetSecurityDescriptorDacl(invalid, 1, new_acl, 0), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);

    EXPECT_EQ(tl_GetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kDaclSecurityInformation, static_cast<void**>(invalid),
                                       nullptr, nullptr, nullptr, &named_descriptor),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_GetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kDaclSecurityInformation, nullptr, nullptr, nullptr,
                                       nullptr, static_cast<void**>(invalid)),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetNamedSecurityInfoW(name.data(), abi::kSeFileObject,
                                       abi::kDaclSecurityInformation, nullptr, nullptr, invalid,
                                       nullptr, 0),
              abi::kErrorInvalidParameter);
    EXPECT_EQ(tl_SetFileSecurityW(name.data(), abi::kDaclSecurityInformation, invalid), 0);
    EXPECT_EQ(tl_GetLastError(), abi::kErrorInvalidParameter);
    ASSERT_EQ(tl_SetFileSecurityW(name.data(), abi::kDaclSecurityInformation, &descriptor), 1);

    EXPECT_EQ(tl_LocalFree(named_descriptor), nullptr);
    EXPECT_EQ(tl_LocalFree(new_acl), nullptr);
}

}  // namespace
}  // namespace tradutorlinux
