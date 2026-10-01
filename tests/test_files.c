#include "io/files/files.h"
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static unsigned checks, failures;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%d: %s\n", __LINE__, #x);                                                                                                                  \
        }                                                                                                                                                      \
    } while (0)

int main(void) {
    setlocale(LC_ALL, "C");
    const wchar_t *wide = L"lindar-test-\u017c\u00f3\u0142\u0107-\U0001f3b5.tmp";
    const char *utf8 = "lindar-test-\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87-\xf0\x9f\x8e\xb5.tmp";
    const char text[] = "lindar";
    lnd_io *io = lnd_io_create_file(wide);
    CHECK(io != nullptr);
    if (io) {
        CHECK(io->vt->write_at(io->state, 0, text, sizeof text) == sizeof text);
        CHECK(io->vt->write_at(io->state, UINT64_MAX, text, 1) == 0);
        lnd_io_close(io);
    }
    io = lnd_io_open_file_utf8(utf8);
    CHECK(io != nullptr);
    if (io) {
        char buffer[16] = {0};
        CHECK(io->size == sizeof text);
        CHECK(io->vt->read_at(io->state, 0, buffer, sizeof buffer) == sizeof text);
        CHECK(!memcmp(buffer, text, sizeof text));
        CHECK(io->vt->read_at(io->state, UINT64_MAX, buffer, 1) == LND_ERR_IO);
        lnd_io_close(io);
    }
#if LND_OS_WINDOWS
    CHECK(_wremove(wide) == 0);
#else
    CHECK(remove(utf8) == 0);
    wchar_t invalid[] = {0xd800, 0};
    CHECK(lnd_io_create_file(invalid) == nullptr);
    invalid[0] = 0x110000;
    CHECK(lnd_io_open_file(invalid) == nullptr);
#endif
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
