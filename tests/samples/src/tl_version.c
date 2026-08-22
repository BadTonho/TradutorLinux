typedef unsigned long dword_t;
typedef int bool_t;
typedef unsigned short word_t;
typedef unsigned long long ull_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) bool_t GetVersionExA(void* lpVersionInformation);
__attribute__((dllimport)) bool_t GetVersionExW(void* lpVersionInformation);
__attribute__((dllimport)) bool_t VerifyVersionInfoW(void* lpVersionInfo, dword_t dwTypeMask, ull_t dwlConditionMask);
__attribute__((dllimport)) ull_t VerSetConditionMask(ull_t ConditionMask, dword_t TypeMask, unsigned char Condition);
__attribute__((dllimport)) int GetUserDefaultLocaleName(word_t* lpLocaleName, int cchLocaleName);
__attribute__((dllimport)) dword_t LocaleNameToLCID(const word_t* lpName, dword_t dwFlags);
__attribute__((dllimport)) dword_t GetLastError(void);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;

    // ---- GetVersionExA OSVERSIONINFOA (148) ----
    unsigned char infoA148[156] = {0};
    // dwOSVersionInfoSize = 148
    infoA148[0] = 148; infoA148[1] = 0; infoA148[2] = 0; infoA148[3] = 0;
    if (!GetVersionExA(infoA148)) ExitProcess(10U);
    // Verificar major=10, minor=0, build=19044, platform=2
    dword_t major = *(dword_t*)(infoA148 + 4);
    dword_t minor = *(dword_t*)(infoA148 + 8);
    dword_t build = *(dword_t*)(infoA148 + 12);
    dword_t platform = *(dword_t*)(infoA148 + 16);
    if (major != 10U || minor != 0U || build != 19044U || platform != 2U) ExitProcess(11U);
    // szCSDVersion[0] should be 0
    if (infoA148[20] != 0) ExitProcess(12U);

    // ---- GetVersionExA EX (156) ----
    unsigned char infoAEx[156] = {0};
    infoAEx[0] = 156; infoAEx[1]=0; infoAEx[2]=0; infoAEx[3]=0;
    if (!GetVersionExA(infoAEx)) ExitProcess(13U);
    // wProductType at 154 should be 1
    if (infoAEx[154] != 1) ExitProcess(14U);

    // ---- GetVersionExA failure: null ----
    if (GetVersionExA((void*)0)) ExitProcess(15U);
    // failure: size 0
    unsigned char bad[4] = {0,0,0,0};
    if (GetVersionExA(bad)) ExitProcess(16U);

    // ---- GetVersionExW OSVERSIONINFOW (276) ----
    unsigned char infoW276[284] = {0};
    infoW276[0] = 20; infoW276[1] = 1; infoW276[2] = 0; infoW276[3] = 0; // 276 = 0x114 = 20 1
    // 276 decimal = 0x114 -> little endian 0x14 0x01 0x00 0x00
    infoW276[0] = 0x14; infoW276[1] = 0x01; infoW276[2]=0; infoW276[3]=0;
    if (!GetVersionExW(infoW276)) ExitProcess(20U);
    major = *(dword_t*)(infoW276 + 4);
    if (major != 10U) ExitProcess(21U);

    // ---- GetVersionExW EX (284) ----
    unsigned char infoWEx[284] = {0};
    infoWEx[0] = 0x1C; infoWEx[1]=0x01; infoWEx[2]=0; infoWEx[3]=0; // 284=0x11C
    if (!GetVersionExW(infoWEx)) ExitProcess(22U);
    if (infoWEx[282] != 1) ExitProcess(23U); // wProductType at 282

    // ---- VerifyVersionInfoW ----
    // Preparar OSVERSIONINFOEXW para Verify
    unsigned char vi[284] = {0};
    vi[0]=0x1C; vi[1]=0x01; vi[2]=0; vi[3]=0;
    *(dword_t*)(vi+4)=10; // major 10
    // VerSetConditionMask chain
    ull_t mask = 0;
    mask = VerSetConditionMask(mask, 0x00000002U, 3); // VER_MAJORVERSION, VER_GREATER_EQUAL
    mask = VerSetConditionMask(mask, 0x00000001U, 3); // VER_MINORVERSION
    if (mask == 0) ExitProcess(30U);
    if (!VerifyVersionInfoW(vi, 0x00000003U, mask)) ExitProcess(31U);
    // Verify with null should fail
    if (VerifyVersionInfoW((void*)0, 0x02U, mask)) ExitProcess(32U);
    // VerSetConditionMask with TypeMask 0 should return unchanged
    ull_t m0 = VerSetConditionMask(12345ULL, 0U, 1);
    if (m0 != 12345ULL) ExitProcess(33U);

    // ---- GetUserDefaultLocaleName ----
    // First call: null -> returns needed (6)
    int needed = GetUserDefaultLocaleName((word_t*)0, 0);
    if (needed != 6) ExitProcess(40U);
    word_t buf[6] = {0};
    int ret = GetUserDefaultLocaleName(buf, 6);
    if (ret != 6) ExitProcess(41U);
    // Verify "en-US"
    if (buf[0] != 'e' || buf[1] != 'n' || buf[2] != '-' || buf[3] != 'U' || buf[4] != 'S' || buf[5] != 0) ExitProcess(42U);
    // Small buffer should fail with 122
    word_t small[2] = {0};
    int rsmall = GetUserDefaultLocaleName(small, 2);
    if (rsmall != 0) ExitProcess(43U);
    if (GetLastError() != 122U) ExitProcess(44U);
    // LocaleNameToLCID
    const word_t kEnUS[] = {'e','n','-','U','S',0};
    const word_t kPtBR[] = {'p','t','-','B','R',0};
    const word_t kEn[] = {'e','n',0};
    const word_t kUnknown[] = {'x','x','-','X','X',0};
    dword_t lcid = LocaleNameToLCID(kEnUS, 0);
    if (lcid != 0x0409U) ExitProcess(50U);
    lcid = LocaleNameToLCID(kPtBR, 0);
    if (lcid != 0x0416U) ExitProcess(51U);
    lcid = LocaleNameToLCID(kEn, 0);
    if (lcid != 0x0009U) ExitProcess(52U);
    lcid = LocaleNameToLCID(kUnknown, 0);
    if (lcid != 0) ExitProcess(53U);
    lcid = LocaleNameToLCID((word_t*)0, 0);
    if (lcid != 0) ExitProcess(54U);
    // case-insensitive
    const word_t kEnUsLower[] = {'e','n','-','u','s',0};
    lcid = LocaleNameToLCID(kEnUsLower, 0);
    if (lcid != 0x0409U) ExitProcess(55U);

    static const char msg[] = "version\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    ExitProcess(0U);
}
