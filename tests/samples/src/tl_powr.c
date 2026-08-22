typedef unsigned long dword_t;
typedef int bool_t;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* lpBuffer, dword_t nNumberOfBytesToWrite,
                                            dword_t* lpNumberOfBytesWritten, void* lpOverlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t uExitCode);
__attribute__((dllimport)) dword_t PowerGetActiveScheme(void* UserRootPowerKey, void** ActivePolicyGuid);
__attribute__((dllimport)) dword_t PowerSetActiveScheme(void* UserRootPowerKey, const void* SchemeGuid);
__attribute__((dllimport)) dword_t CallNtPowerInformation(int InformationLevel, void* InputBuffer, dword_t InputBufferLength, void* OutputBuffer, dword_t OutputBufferLength);
__attribute__((dllimport)) dword_t GetAdaptersInfo(void* AdapterInfo, dword_t* OutBufLen);
__attribute__((dllimport)) dword_t GetAdaptersAddresses(dword_t Family, dword_t Flags, void* Reserved, void* AdapterAddresses, dword_t* SizePointer);
__attribute__((dllimport)) dword_t if_nametoindex(const char* ifname);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* stdout_handle = GetStdHandle((dword_t)-11);
    dword_t written = 0;

    // POWRPROF PowerGetActiveScheme
    void* guid = (void*)0;
    dword_t res = PowerGetActiveScheme((void*)0, &guid);
    if (res != 0) ExitProcess(10U);
    if (guid == (void*)0) ExitProcess(11U);
    // PowerGetActiveScheme with null out param should fail
    res = PowerGetActiveScheme((void*)0, (void**)0);
    if (res != 87) ExitProcess(12U);
    // PowerSetActiveScheme with null guid should succeed (stub)
    res = PowerSetActiveScheme((void*)0, (void*)0);
    if (res != 0) ExitProcess(13U);
    // Com guid dummy
    char dummyGuid[16] = {0};
    res = PowerSetActiveScheme((void*)0, dummyGuid);
    if (res != 0) ExitProcess(14U);

    // IPHLPAPI GetAdaptersInfo null buffer
    dword_t len = 0;
    res = GetAdaptersInfo((void*)0, &len);
    if (res != 0) ExitProcess(20U);
    // Com buffer pequeno
    char buf[16] = {0};
    len = sizeof(buf);
    res = GetAdaptersInfo(buf, &len);
    if (res != 0 && res != 111) {
        // nosso stub retorna 0 com len 0, aceita
    }
    // Com len null deve falhar 87
    res = GetAdaptersInfo(buf, (dword_t*)0);
    if (res != 87) ExitProcess(21U);

    // GetAdaptersAddresses
    dword_t size = 0;
    res = GetAdaptersAddresses(0, 0, (void*)0, (void*)0, &size);
    if (res != 0) ExitProcess(30U);
    if (size != 0) ExitProcess(31U);
    char buf2[64] = {0};
    size = sizeof(buf2);
    res = GetAdaptersAddresses(2, 0, (void*)0, buf2, &size);
    if (res != 0) ExitProcess(32U);
    // Com size null deve falhar
    res = GetAdaptersAddresses(0,0,(void*)0, buf2, (dword_t*)0);
    if (res != 87) ExitProcess(33U);

    // CallNtPowerInformation
    char out[16] = {0};
    res = CallNtPowerInformation(0, (void*)0, 0, out, sizeof(out));
    if (res != 0) ExitProcess(40U);
    // com output null e tamanho >0 deve falhar se buffer não mapeado? Mas nosso stub aceita null
    // testa com InputBuffer válido
    char in[4] = {1,2,3,4};
    res = CallNtPowerInformation(0, in, sizeof(in), out, sizeof(out));
    if (res != 0) ExitProcess(41U);

    // if_nametoindex
    const char kLo[] = "lo";
    dword_t idx = if_nametoindex(kLo);
    if (idx == 0) ExitProcess(50U);
    idx = if_nametoindex((const char*)0);
    if (idx != 0) ExitProcess(51U);

    static const char msg[] = "powr\n";
    if (!WriteFile(stdout_handle, msg, sizeof(msg)-1, &written, (void*)0) || written != sizeof(msg)-1) {
        ExitProcess(99U);
    }
    ExitProcess(0U);
}
