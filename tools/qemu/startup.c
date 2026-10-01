#include "platform.h"

extern uint32_t _stack_top, _data_load, _data_start, _data_end, _bss_start, _bss_end;
int main(void);

_Noreturn void fault(void) {
    qemu_write("LND_FAULT\n");
    qemu_exit(99);
}

_Noreturn void reset(void) {
    uint32_t *source = &_data_load;
    for (uint32_t *p = &_data_start; p < &_data_end; p++) *p = *source++;
    for (uint32_t *p = &_bss_start; p < &_bss_end; p++) *p = 0;
    qemu_exit((uint32_t)main());
}

__attribute__((section(".vectors"), used))
const uintptr_t vectors[16] = {
    (uintptr_t)&_stack_top, (uintptr_t)reset, (uintptr_t)fault, (uintptr_t)fault,
    (uintptr_t)fault, (uintptr_t)fault, (uintptr_t)fault, 0, 0, 0, 0,
    (uintptr_t)fault, (uintptr_t)fault, 0, (uintptr_t)fault, (uintptr_t)fault
};
