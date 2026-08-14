void tl_target(void) {
}

typedef void (*entry_fn)(void);

const entry_fn kTable = tl_target;

void tl_entry(void) {
    (void)kTable;
}
