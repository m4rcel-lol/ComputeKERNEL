#include <ck/kernel.h>

void ck_early_console_init(void) {
    /* serial/framebuffer init goes here in next implementation phase */
}

void ck_printk(const char *fmt, ...) {
    (void)fmt;
    /* formatted logging backend goes here in next implementation phase */
}

