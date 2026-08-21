typedef void* handle_t;
typedef unsigned int uint_t;

// Símbolo deliberadamente inexistente em USER32.dll: o runtime deve rejeitar
// os imports sem executar o entry point (contrato da Fase 3).
__attribute__((dllimport)) int TlUnknownSymbolW(handle_t hwnd, const char* text,
                                                const char* caption, uint_t type);

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    TlUnknownSymbolW((handle_t)0, (const char*)0, (const char*)0, 0U);
}
