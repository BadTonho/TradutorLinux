void tl_entry(void);
__attribute__((used, section(".text"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
}
