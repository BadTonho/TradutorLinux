typedef unsigned char byte_t;
typedef unsigned short word_t;
typedef unsigned long dword_t;
typedef int bool_t;

typedef struct sid_authority_t {
    byte_t value[6];
} sid_authority_t;

typedef struct token_user_t {
    void* sid;
    dword_t attributes;
    dword_t padding;
} token_user_t;

typedef struct trustee_w_t {
    void* multiple_trustee;
    dword_t multiple_trustee_operation;
    dword_t trustee_form;
    dword_t trustee_type;
    dword_t padding;
    void* name;
} trustee_w_t;

typedef struct explicit_access_w_t {
    dword_t access_permissions;
    dword_t access_mode;
    dword_t inheritance;
    dword_t padding;
    trustee_w_t trustee;
} explicit_access_w_t;

typedef struct security_descriptor_t {
    byte_t revision;
    byte_t sbz1;
    word_t control;
    dword_t padding;
    void* owner;
    void* group;
    void* sacl;
    void* dacl;
} security_descriptor_t;

typedef struct acl_t {
    byte_t revision;
    byte_t sbz1;
    word_t size;
    word_t ace_count;
    word_t sbz2;
} acl_t;

__attribute__((dllimport, noreturn)) void ExitProcess(dword_t exit_code);
__attribute__((dllimport)) void* GetStdHandle(dword_t standard_handle);
__attribute__((dllimport)) bool_t WriteFile(void* handle, const void* buffer,
                                            dword_t size, dword_t* written, void* overlapped);
__attribute__((dllimport)) bool_t CreateDirectoryW(const word_t* path, const void* security);
__attribute__((dllimport)) void* CreateFileW(const word_t* path, dword_t desired_access,
                                             dword_t share_mode, const void* security,
                                             dword_t creation, dword_t flags,
                                             const void* template_file);
__attribute__((dllimport)) bool_t CloseHandle(const void* handle);
__attribute__((dllimport)) dword_t GetLastError(void);
__attribute__((dllimport)) void* LocalFree(void* memory);
__attribute__((dllimport)) void* GetCurrentProcess(void);

__attribute__((dllimport)) bool_t OpenProcessToken(const void* process, dword_t desired_access,
                                                    void** token);
__attribute__((dllimport)) bool_t GetTokenInformation(const void* token, dword_t information_class,
                                                       void* information, dword_t information_length,
                                                       dword_t* return_length);
__attribute__((dllimport)) bool_t AllocateAndInitializeSid(const sid_authority_t* authority,
    byte_t count, dword_t a0, dword_t a1, dword_t a2, dword_t a3, dword_t a4, dword_t a5,
    dword_t a6, dword_t a7, void** sid);
__attribute__((dllimport)) void* FreeSid(void* sid);
__attribute__((dllimport)) dword_t GetLengthSid(const void* sid);
__attribute__((dllimport)) bool_t CopySid(dword_t length, void* destination, const void* source);
__attribute__((dllimport)) bool_t EqualSid(const void* first, const void* second);
__attribute__((dllimport)) bool_t IsValidSid(const void* sid);
__attribute__((dllimport)) bool_t CreateWellKnownSid(dword_t type, const void* domain,
                                                     void* sid, dword_t* size);
__attribute__((dllimport)) bool_t CheckTokenMembership(const void* token, const void* sid,
                                                       bool_t* member);
__attribute__((dllimport)) void BuildTrusteeWithSidW(trustee_w_t* trustee, void* sid);
__attribute__((dllimport)) bool_t InitializeSecurityDescriptor(security_descriptor_t* descriptor,
                                                                dword_t revision);
__attribute__((dllimport)) bool_t SetSecurityDescriptorDacl(security_descriptor_t* descriptor,
                                                             bool_t present, void* dacl,
                                                             bool_t defaulted);
__attribute__((dllimport)) dword_t SetEntriesInAclW(dword_t count,
                                                     const explicit_access_w_t* entries,
                                                     const void* old_acl, void** new_acl);
__attribute__((dllimport)) dword_t GetNamedSecurityInfoW(const word_t* object_name,
    dword_t object_type, dword_t security_information, void** owner, void** group, void** dacl,
    void** sacl, void** descriptor);
__attribute__((dllimport)) dword_t SetNamedSecurityInfoW(word_t* object_name, dword_t object_type,
    dword_t security_information, void* owner, void* group, void* dacl, void* sacl,
    dword_t inheritance);
__attribute__((dllimport)) bool_t SetFileSecurityW(const word_t* path,
                                                   dword_t security_information,
                                                   const security_descriptor_t* descriptor);

static word_t directory[] = {'C', ':', '\\', 't', 'l', '_', 's', 'e', 'c', 'u', 'r', 'i', 't', 'y', 0};
static word_t file_name[] = {'C', ':', '\\', 't', 'l', '_', 's', 'e', 'c', 'u', 'r', 'i', 't', 'y', '\\',
                             'a', 'c', 'l', '.', 'b', 'i', 'n', 0};

static void fail(void* output, dword_t* written, dword_t code) {
    static const char message[] = "security-fail\n";
    (void)WriteFile(output, message, sizeof(message) - 1, written, (void*)0);
    ExitProcess(code);
}

static dword_t acl_first_mask(const void* dacl) {
    const byte_t* const bytes = (const byte_t*)dacl;
    return *(const dword_t*)(bytes + sizeof(acl_t) + 4);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    static const char write_message[] = "security-write\n";
    static const char read_message[] = "security-read\n";
    void* output = GetStdHandle((dword_t)-11);
    dword_t written = 0;
    void* token = (void*)0;
    dword_t required = 0;
    byte_t token_buffer[64] = {0};
    void* owner = (void*)0;
    void* dacl = (void*)0;
    void* returned_descriptor = (void*)0;
    void* file = (void*)0;

    (void)CreateDirectoryW(directory, (void*)0);
    file = CreateFileW(file_name, 0xC0000000UL, 0, (void*)0, 4, 0, (void*)0);
    if (file == (void*)0 || !OpenProcessToken(GetCurrentProcess(), 0x0008UL, &token)) {
        fail(output, &written, 1);
    }
    if (GetTokenInformation(token, 1, (void*)0, 0, &required) || GetLastError() != 122U ||
        required < sizeof(token_user_t) || required > sizeof(token_buffer) ||
        !GetTokenInformation(token, 1, token_buffer, required, &required)) {
        fail(output, &written, 2);
    }
    token_user_t* const user = (token_user_t*)token_buffer;
    if (!IsValidSid(user->sid) || GetLengthSid(user->sid) == 0) {
        fail(output, &written, 3);
    }

    byte_t copied_sid[64] = {0};
    if (!CopySid(sizeof(copied_sid), copied_sid, user->sid) || !EqualSid(copied_sid, user->sid)) {
        fail(output, &written, 4);
    }
    sid_authority_t authority = {{0, 0, 0, 0, 0, 5}};
    void* allocated_sid = (void*)0;
    if (!AllocateAndInitializeSid(&authority, 1, 123, 0, 0, 0, 0, 0, 0, 0, &allocated_sid) ||
        !IsValidSid(allocated_sid) || FreeSid(allocated_sid) != (void*)0) {
        fail(output, &written, 5);
    }
    byte_t admin_sid[32] = {0};
    dword_t admin_size = 0;
    bool_t member = 1;
    if (CreateWellKnownSid(26, (void*)0, (void*)0, &admin_size) || GetLastError() != 122U ||
        admin_size > sizeof(admin_sid) || !CreateWellKnownSid(26, (void*)0, admin_sid, &admin_size) ||
        !CheckTokenMembership(token, user->sid, &member) || member != 1 ||
        !CheckTokenMembership(token, admin_sid, &member) || member != 0) {
        fail(output, &written, 6);
    }

    if (GetNamedSecurityInfoW(file_name, 1, 0x00000005UL, &owner, (void**)0, &dacl, (void**)0,
                              &returned_descriptor) != 0 || !EqualSid(owner, user->sid) ||
        dacl == (void*)0) {
        fail(output, &written, 7);
    }

    if (acl_first_mask(dacl) == 0xC0000000UL) {
        if (LocalFree(returned_descriptor) != (void*)0 || !CloseHandle(token) || !CloseHandle(file) ||
            !WriteFile(output, read_message, sizeof(read_message) - 1, &written, (void*)0)) {
            fail(output, &written, 8);
        }
        ExitProcess(0);
    }

    trustee_w_t trustee = {0};
    explicit_access_w_t entry = {0};
    void* new_acl = (void*)0;
    security_descriptor_t descriptor = {0};
    BuildTrusteeWithSidW(&trustee, user->sid);
    entry.access_permissions = 0xC0000000UL;
    entry.access_mode = 1;
    entry.trustee = trustee;
    if (SetEntriesInAclW(1, &entry, dacl, &new_acl) != 0 || new_acl == (void*)0 ||
        SetNamedSecurityInfoW(file_name, 1, 0x00000004UL, (void*)0, (void*)0, new_acl,
                              (void*)0, 0) != 0 ||
        !InitializeSecurityDescriptor(&descriptor, 1) ||
        !SetSecurityDescriptorDacl(&descriptor, 1, new_acl, 0) ||
        !SetFileSecurityW(file_name, 0x00000004UL, &descriptor) ||
        LocalFree(returned_descriptor) != (void*)0 || LocalFree(new_acl) != (void*)0 ||
        !CloseHandle(token) || !CloseHandle(file) ||
        !WriteFile(output, write_message, sizeof(write_message) - 1, &written, (void*)0)) {
        fail(output, &written, 9);
    }
    ExitProcess(0);
}
