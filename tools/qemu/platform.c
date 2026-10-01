#include "platform.h"

#if defined(__arm__)
static void semihost(uint32_t operation, const void *argument) {
    register uint32_t r0 __asm__("r0") = operation;
    register const void *r1 __asm__("r1") = argument;
    __asm__ volatile("bkpt 0xab" : "+r"(r0), "+r"(r1) : : "memory", "cc");
}
void qemu_write(const char *text) { semihost(4, text); }
#elif defined(__AVR__)
#include <avr/io.h>
#include <avr/interrupt.h>
void qemu_write(const char *text) {
    UBRR0 = 8;
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    while (*text) {
        while (!(UCSR0A & _BV(UDRE0))) {}
        UDR0 = *text++;
    }
}
#else
#include <stdio.h>
#include <stdlib.h>
void qemu_write(const char *text) { fputs(text, stdout); }
#endif

void qemu_value(const char *name, uint32_t value) {
    char text[10];
    for (unsigned i = 0; i < 8; i++) {
        unsigned digit = (unsigned)((value >> ((7 - i) * 4)) & 15);
        text[i] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    }
    text[8] = '\n';
    text[9] = 0;
    qemu_write(name);
    qemu_write(" ");
    qemu_write(text);
}

_Noreturn void qemu_exit(uint32_t code) {
    qemu_value("LND_EXIT", code);
#if defined(__arm__)
    const uint32_t arguments[] = {0x20026, code};
    semihost(0x20, arguments);
#elif defined(__AVR__)
    while (!(UCSR0A & _BV(TXC0))) {}
    cli();
#else
    exit((int)code);
#endif
    for (;;) {}
}
