#include "platform.h"

int main(void) {
#if LND_TEST_HANG
    for (;;) __asm__ volatile("" : : : "memory");
#elif LND_TEST_FAULT && defined(__arm__)
    __asm__ volatile("udf #0");
    qemu_exit(1);
#else
    qemu_exit(7);
#endif
}
