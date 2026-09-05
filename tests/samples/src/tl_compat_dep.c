typedef unsigned long dword_t;

__attribute__((dllexport)) int CompatDependencyEntry(void) {
    return 1;
}

int DllMain(void* module, dword_t reason, void* reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    return 1;
}

__attribute__((used, section(".rdata")))
void (*const compat_dependency_relocation_anchor)(void) =
    (void (*)(void))CompatDependencyEntry;
