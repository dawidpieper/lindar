#pragma once
#include <stdint.h>

void qemu_write(const char *text);
void qemu_value(const char *name, uint32_t value);
_Noreturn void qemu_exit(uint32_t code);
