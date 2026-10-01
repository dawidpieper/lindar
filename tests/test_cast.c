#include "codec_test.h"
#include "lindar_cast.h"
#include "src/thread.h"
static LND_CAST_INFO wait_for(LND_CAST *cast, int state) {
    LND_CAST_INFO info = {0};
    uint64_t deadline = lnd_time_ns() + 5000000000ULL;
    do {
        CHECK(LND_CastGetInfo(cast, &info) == LND_OK);
        if (info.state >= state) return info;
        lnd_sleep_ms(5);
    } while (lnd_time_ns() < deadline);
    CHECK(info.state >= state);
    return info;
}
static void broadcast(const char *url, const char *admin, int protocol, bool encode, bool reject) {
    LND_CAST_OPTIONS options = {.url = url,
                                .admin_url = admin,
                                .password = reject ? "bad" : "secret",
                                .protocol = protocol,
                                .name = "Lindar",
                                .mount = "/stream",
                                .queue_bytes = 262144,
                                .timeout_ms = 1000};
    LND_CAST *cast = LND_CastCreate(&options);
    CHECK(cast != nullptr);
    if (!cast) return;
    LND_CAST_INFO info = wait_for(cast, reject ? LND_CAST_FAILED : LND_CAST_READY);
    if (reject) {
        CHECK(info.state == LND_CAST_FAILED && info.error < 0);
        CHECK(LND_CastWrite(cast, "x", 1) < 0);
        LND_CastFree(cast);
        return;
    }
    CHECK(info.state == LND_CAST_READY);
    CHECK(LND_CastSetTitle(cast, "Title & Unicode \xc5\x82") == LND_OK);
    if (encode) {
        LND_ENCODER_PARAMS params = {.encoder_name = "mp3", .channels = 1, .sample_rate_hz = 48000, .bitrate_kbps = 128};
        LND_OUTPUT *output = LND_OutputCreateCast(cast, &params);
        CHECK(output != nullptr);
        CHECK(LND_OutputCreateCast(cast, &params) == nullptr);
        float samples[4800] = {0};
        CHECK(LND_OutputWrite(output, samples, LND_FORMAT_F32, 4800) == LND_OK);
        CHECK(LND_OutputFree(output) == LND_OK);
    } else {
        uint8_t data[40000];
        for (unsigned i = 0; i < sizeof data; ++i)
            data[i] = (uint8_t)i;
        CHECK(LND_CastWrite(cast, data, sizeof data) == sizeof data);
        CHECK(LND_CastEnd(cast) == LND_OK);
    }
    info = wait_for(cast, LND_CAST_ENDED);
    CHECK(info.state == LND_CAST_ENDED && info.error == LND_OK && info.metadata_error == LND_OK && info.queued_bytes == 0);
    CHECK(encode ? info.sent_bytes > 100 : info.sent_bytes == 40000);
    CHECK(LND_CastWrite(cast, "x", 1) == LND_ERR_STATE);
    LND_CastFree(cast);
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    CHECK(LND_LibraryInit() == LND_OK);
    broadcast(argv[1], argv[3], LND_CAST_SHOUTCAST, false, false);
    broadcast(argv[2], argv[3], LND_CAST_ICECAST, false, false);
#if LND_MODULE_MP3_ENCODER
    broadcast(argv[1], argv[3], LND_CAST_SHOUTCAST, true, false);
#endif
    broadcast(argv[1], argv[3], LND_CAST_SHOUTCAST, false, true);
    LND_CAST_OPTIONS options = {.url = argv[1], .password = "secret\r\nInjected: yes"};
    CHECK(LND_CastCreate(&options) == nullptr);
    LND_LibraryFree();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
