#include "processing/stretch/soundtouch/bridge.h"
#include "lindar_soundtouch.h"

#include <stdio.h>
#include <string.h>

extern void lnd_st_test_alloc(int32_t after);
extern uint32_t lnd_st_test_live(void);

static unsigned checks, failures;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

static void test_node_recovery(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    uint32_t baseline = lnd_st_test_live();
    bool success = false;
    for (int32_t fail = 0; fail < 128 && !success; fail++) {
        lnd_st_test_alloc(fail);
        LND_NODE *node = LND_NodeCreateSoundTouch(2, 48000, nullptr);
        lnd_st_test_alloc(-1);
        success = node != nullptr;
        if (node)
            CHECK(LND_NodeFree(node) == LND_OK);
        else
            CHECK(LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
        CHECK(lnd_st_test_live() == baseline);
    }
    CHECK(success);
    LND_NODE *node = LND_NodeCreateSoundTouch(2, 48000, nullptr);
    CHECK(node != nullptr);
    lnd_st_test_alloc(0);
    CHECK(LND_NodeSetSoundTouchSetting(node, LND_SOUNDTOUCH_AA_FILTER_LENGTH, 128) == LND_ERR_OUT_OF_MEMORY);
    LND_SOUNDTOUCH_INFO info;
    CHECK(LND_NodeGetSoundTouchInfo(node, &info) == LND_OK && info.error == LND_OK);
    CHECK(LND_NodeResetSoundTouch(node) == LND_ERR_OUT_OF_MEMORY);
    CHECK(LND_NodeGetSoundTouchInfo(node, &info) == LND_OK && info.error == LND_OK);
    lnd_st_test_alloc(-1);
    CHECK(LND_NodeResetSoundTouch(node) == LND_OK);
    CHECK(LND_NodeGetSoundTouchInfo(node, &info) == LND_OK && info.error == LND_OK);
    CHECK(LND_NodeGetSoundTouchSetting(node, LND_SOUNDTOUCH_AA_FILTER_LENGTH) == 64);
    lnd_st_test_alloc(0);
    CHECK(LND_NodeSetParam(node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, 2) == LND_OK);
    CHECK(LND_LibraryUpdate() == LND_OK);
    CHECK(LND_NodeGetSoundTouchInfo(node, &info) == LND_OK && info.error == LND_ERR_OUT_OF_MEMORY);
    lnd_st_test_alloc(-1);
    CHECK(LND_NodeResetSoundTouch(node) == LND_OK);
    CHECK(LND_NodeGetSoundTouchInfo(node, &info) == LND_OK && info.error == LND_OK);
    CHECK(LND_NodeGetParam(node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO) == 2);
    CHECK(LND_NodeFree(node) == LND_OK);
    CHECK(lnd_st_test_live() == baseline);
    LND_LibraryFree();
}

int main(void) {
    uint32_t baseline = lnd_st_test_live();
    int32_t error;
    CHECK(lnd_st_create(33, 48000, &error) == nullptr && error == LND_ERR_EXTERNAL);
    CHECK(lnd_st_test_live() == baseline);
    for (unsigned operation = 0; operation < 5; operation++) {
        bool success = false;
        for (int32_t fail = 0; fail < 128 && !success; fail++) {
            void *engine;
            if (operation == 0) {
                lnd_st_test_alloc(fail);
                engine = lnd_st_create(2, 48000, &error);
            } else {
                engine = lnd_st_create(2, 48000, &error);
                CHECK(engine && error == LND_OK);
                lnd_st_test_alloc(fail);
                switch (operation) {
                case 1:
                    error = lnd_st_param(engine, 2, 2);
                    break;
                case 2:
                    error = lnd_st_setting(engine, 1, 128);
                    break;
                case 3:
                    error = lnd_st_setting(engine, 5, 64);
                    break;
                default: {
                    static float pcm[96000];
                    error = lnd_st_put(engine, pcm, 48000);
                    break;
                }
                }
            }
            lnd_st_test_alloc(-1);
            success = error == LND_OK;
            CHECK(success || error == LND_ERR_OUT_OF_MEMORY);
            lnd_st_destroy(engine);
            CHECK(lnd_st_test_live() == baseline);
        }
        CHECK(success);
    }
    test_node_recovery();
    printf("SoundTouch boundary: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
