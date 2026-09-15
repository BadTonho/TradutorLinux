int tl_entry(void);
__attribute__((used, section(".text"))) int (*const tl_relocation_anchor)(void) = &tl_entry;

int tl_entry(void) {
    return 0;
}
