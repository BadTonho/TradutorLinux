void tl_entry(void);
__attribute__((used, section(".text"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

static volatile unsigned int g_hang_counter = 0;

void tl_entry(void) {
    for (;;) {
        g_hang_counter = g_hang_counter + 1U;
    }
}