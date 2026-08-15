void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    volatile unsigned int* const fault = (volatile unsigned int*)0U;
    *fault = 1U;
}
