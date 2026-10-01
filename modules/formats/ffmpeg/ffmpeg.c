#include "ffmpeg.h"
#include "src/atomic.h"
#include "src/thread.h"
#include "utility/log/log.h"
#include <libavutil/log.h>
#include <string.h>

static void lnd_ff_log(void *avcl, int level, const char *fmt, va_list args) {
    if (level > AV_LOG_WARNING) return;
    char line[512];
    int prefix = 1;
    av_log_format_line2(avcl, level, fmt, args, line, sizeof line, &prefix);
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
#if LND_MODULE_LOG
    lnd_log(level <= AV_LOG_FATAL ? LND_LOG_ERROR : (level <= AV_LOG_ERROR ? LND_LOG_WARN : LND_LOG_INFO), "ffmpeg: %s", line);
#endif
}

void lnd_ff_init(void) {
    static lnd_atomic_u32 ready;
    if (lnd_load(&ready) == 2) return;
    uint32_t expected = 0;
    if (lnd_cas(&ready, &expected, 1)) {
        av_log_set_callback(lnd_ff_log);
        lnd_store(&ready, 2);
    } else {
        while (lnd_load(&ready) != 2) lnd_sleep_ms(0);
    }
}
