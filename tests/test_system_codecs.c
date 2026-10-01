#include "codec_test.h"
#include "lindar_files.h"
#include "lindar_audiotoolbox.h"
#include "lindar_mediacodec.h"
#include "lindar_gstreamer.h"

#if LND_MODULE_GSTREAMER
#include <gst/gst.h>

static bool available(const char *requirements) {
    bool result = true;
    gchar **groups = g_strsplit(requirements, ";", -1);
    for (gchar **group = groups; *group; group++) {
        bool found = false;
        gchar **names = g_strsplit(*group, "|", -1);
        for (gchar **name = names; *name; name++) {
            GstElementFactory *factory = gst_element_factory_find(*name);
            if (factory) {
                found = true;
                gst_object_unref(factory);
            }
        }
        g_strfreev(names);
        if (!found) result = false;
    }
    g_strfreev(groups);
    return result;
}
#endif

static const LND_CODEC *codec;
static unsigned tested_files;

static void check_source(LND_SOURCE *source, bool exact) {
    CHECK(source != nullptr);
    if (!source) return;
    CHECK(LND_SourceGetCodec(source) == codec);
    uint64_t length = LND_SourceGetLengthFrames(source);
    CHECK(length > 0);
    uint32_t channels = LND_SourceGetChannels(source);
    CHECK(channels >= 1 && channels <= 2);
    if (channels < 1 || channels > 2) {
        LND_SourceFree(source);
        return;
    }
    int16_t reference[514], pcm[514];
    CHECK(LND_SourceRead(source, reference, LND_FORMAT_S16, 257) == 257);
    CHECK(LND_SourceSeekFrames(source, 113) == LND_OK);
    CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 113) == 113);
    if (exact) CHECK(!memcmp(reference + 113 * channels, pcm, 113 * channels * sizeof(int16_t)));
    uint64_t total = 226, got;
    uint64_t energy = 0;
    while ((got = LND_SourceRead(source, pcm, LND_FORMAT_S16, 257)) && total < 1000000) {
        CHECK(got <= 257);
        for (size_t i = 0; i < got * channels; i++)
            energy += (uint64_t)((int32_t)pcm[i] * pcm[i]);
        total += got;
    }
    CHECK(total < 1000000 && total > 257 && energy > 0);
    uint64_t difference = length > total ? length - total : total - length;
    printf("duration: %llu frames, decoded: %llu\n", (unsigned long long)length, (unsigned long long)total);
    CHECK(difference <= LND_SourceGetSampleRateHz(source) / 4);
    if (exact) CHECK(difference <= 1);
    CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 257) == 0);
    for (unsigned rewind = 0; rewind < 3; rewind++) {
        CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
        CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 257) == 257);
        if (memcmp(reference, pcm, 257 * channels * sizeof(int16_t)))
            printf("rewind %u: expected %d %d %d %d, got %d %d %d %d\n", rewind, reference[0], reference[1], reference[2], reference[3], pcm[0], pcm[1], pcm[2],
                   pcm[3]);
        CHECK(!memcmp(reference, pcm, 257 * channels * sizeof(int16_t)));
    }
    CHECK(LND_SourceFree(source) == LND_OK);
}

static void check_files(void) {
    const char *files[] = {
        "audiosamples/tone.wav",  "audiosamples/tone.mp3",      "audiosamples/tone.aac",       "audiosamples/tone.m4a",
#if LND_MODULE_GSTREAMER
        "audiosamples/tone.aiff", "audiosamples/tone.flac",     "audiosamples/tone.ogg",       "audiosamples/tone.opus",
        "audiosamples/tone.wma",  "audiosamples/tone.alac.m4a", "audiosamples/tone.video.avi",
#endif
    };
#if LND_MODULE_GSTREAMER
    const char *requirements[] = {"wavparse",
                                  "mpegaudioparse;mpg123audiodec|avdec_mp3",
                                  "aacparse;avdec_aac|faad",
                                  "qtdemux;avdec_aac|faad",
                                  "aiffparse|avdemux_aiff",
                                  "flacparse;flacdec|avdec_flac",
                                  "oggdemux;vorbisdec|avdec_vorbis",
                                  "oggdemux;opusdec|avdec_opus",
                                  "asfdemux;avdec_wmav2",
                                  "qtdemux;avdec_alac",
                                  "avidemux"};
    gst_init(nullptr, nullptr);
#endif
    for (unsigned i = 0; i < sizeof files / sizeof *files; i++) {
#if LND_MODULE_GSTREAMER
        if (!available(requirements[i])) {
            printf("SKIP %s: missing installed GStreamer plugins (%s)\n", files[i], requirements[i]);
            continue;
        }
#endif
        tested_files++;
        printf("%s: %s\n", codec->name, files[i]);
        size_t size;
        uint8_t *data = read_file(files[i], &size);
        if (!data) continue;
        LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = codec->name, .block_frames = 83};
        check_source(LND_SourceCreateEncodedMemory(data, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options), i == 0);
#if LND_MODULE_FILES
        check_source(LND_SourceCreateFile(files[i], LND_ENCODED_SOURCE_LIGHTWEIGHT, &options), i == 0);
#endif
        if (i == 0) {
            uint8_t *window = malloc(size + 21);
            memset(window, 0xa5, size + 21);
            memcpy(window + 7, data, size);
            options.offset_bytes = 7;
            options.length_bytes = size;
            test_input input = {.data = window, .size = size + 21, .limit = 37};
            check_source(LND_SourceCreateEncodedInput(&input_procs, &input, input.size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options), true);
            CHECK(input.closes == 1 && input.reads > 1);
#if LND_MODULE_GRAPH
            check_source(LND_SourceCreateEncodedMemory(window, size + 21, 0, &options), true);
#endif
            free(window);
        }
#if LND_MODULE_GSTREAMER
        if (strstr(files[i], ".alac.")) {
            check_source(LND_SourceCreateEncodedMemory(data, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr), true);
        }
#endif
        free(data);
    }
#if LND_MODULE_GSTREAMER
    if (available("auparse|avdemux_au")) {
        uint8_t au[24 + 1024] = {'.', 's', 'n', 'd', 0, 0, 0, 24, 0, 0, 4, 0, 0, 0, 0, 3, 0, 0, 31, 64, 0, 0, 0, 1};
        for (unsigned i = 0; i < 512; i++) {
            uint16_t sample = (uint16_t)((int)i - 256);
            au[24 + 2 * i] = (uint8_t)(sample >> 8);
            au[25 + 2 * i] = (uint8_t)sample;
        }
        LND_SOURCE *source = LND_SourceCreateEncodedMemory(au, sizeof au, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
        CHECK(source != nullptr);
        if (source) {
            CHECK(LND_SourceGetCodec(source) == codec && LND_SourceGetChannels(source) == 1 && LND_SourceGetSampleRateHz(source) == 8000);
            CHECK(LND_SourceGetLengthFrames(source) == 512);
            int16_t pcm[512];
            CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 512) == 512);
            for (unsigned i = 0; i < 512; i++)
                CHECK(pcm[i] == (int)i - 256);
            CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 1) == 0);
            CHECK(LND_SourceSeekFrames(source, 113) == LND_OK);
            uint64_t got = LND_SourceRead(source, pcm, LND_FORMAT_S16, 16);
            if (got != 16 || pcm[0] != -143) printf("AU seek: %llu frames, sample %d, error %d\n", (unsigned long long)got, pcm[0], LND_ErrorGetLast());
            CHECK(got == 16 && pcm[0] == -143);
            CHECK(LND_SourceFree(source) == LND_OK);
        }
    }
#endif
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = codec->name};
    uint8_t bad[64] = {0};
    test_input input = {.data = bad, .size = sizeof bad};
    CHECK(LND_SourceCreateEncodedInput(&input_procs, &input, input.size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options) == nullptr);
    CHECK(input.closes == 0);
    close_input(&input);
    CHECK(input.closes == 1);
    CHECK(LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 0) == LND_OK);
    CHECK(LND_SourceCreateEncodedMemory(bad, sizeof bad, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options) == nullptr);
    CHECK(LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 1) == LND_OK);
}

int main(void) {
    for (int32_t layout = LND_LAYOUT_INTERLEAVED; layout <= LND_LAYOUT_PLANAR; layout++) {
        test_init(layout);
#if LND_MODULE_GSTREAMER
        codec = LND_GStreamerGetCodec();
#elif LND_MODULE_AUDIOTOOLBOX
        codec = LND_AudioToolboxGetCodec();
#elif LND_MODULE_MEDIACODEC
        codec = LND_MediaCodecGetCodec();
#endif
        CHECK(codec != nullptr && (codec->flags & LND_CODEC_FLAG_SYSTEM));
        if (codec) check_files();
        LND_LibraryFree();
        CHECK(live_allocations == 0);
    }
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : tested_files ? 0 : 77;
}
