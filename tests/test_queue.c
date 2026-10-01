#include "stream_test.h"
#include "lindar_queue.h"

static void test_queue(int32_t layout) {
    begin(LND_FORMAT_S16, layout);
    alignas(max_align_t) unsigned char memory[4096];
    CHECK(LND_QueueGetMemoryBytes(2, 16000, 7) <= sizeof memory);
    CHECK(!LND_QueueGetMemoryBytes(0, 16000, 7) && !LND_QueueGetMemoryBytes(2, 0, 7));
    CHECK(!LND_QueueGetMemoryBytes(2, 16000, 0) && !LND_QueueGetMemoryBytes(1, 16000, UINT32_MAX));
    unsigned before = allocations;
    deny_alloc = true;
    LND_SOURCE *source = LND_QueueInit(memory, sizeof memory, 2, 16000, 7);
    CHECK(source != nullptr && LND_QueueGetCapacityFrames(source) == 7);
    CHECK(!LND_QueueInit(memory, sizeof memory, 2, 16000, 7));
    CHECK(LND_ErrorGetLast() == LND_ERR_BUSY);
    int16_t values[26];
    for (unsigned i = 0; i < 26; i++)
        values[i] = (int16_t)(1000 + i * 20);
    LND_PCM input = {.data = values, .frames = 13, .channels = 2, .format = LND_FORMAT_S16};
    int16_t left[26], right[26];
    void *planes[] = {left, right};
    LND_PCM output = {.planes = planes, .frames = 13, .channels = 2, .format = LND_FORMAT_S16, .layout = LND_LAYOUT_PLANAR, .stride_bytes = 4};
    CHECK(LND_QueueWritePcm(source, &input, 0, 10) == 7);
    CHECK(LND_QueueGetBufferedFrames(source) == 7 && LND_QueueWritePcm(source, &input, 0, 1) == 0);
    CHECK(LND_SourceReadPcm(source, layout == LND_LAYOUT_PLANAR ? &output : &input, 0, 3) == 3);
    if (layout == LND_LAYOUT_PLANAR)
        for (unsigned i = 0; i < 3; i++)
            CHECK(left[i * 2] == values[i * 2] && right[i * 2] == values[i * 2 + 1]);
    CHECK(LND_QueueWritePcm(source, &input, 7, 3) == 3);
    CHECK(LND_QueueDiscardFrames(source, 2) == 2 && LND_QueueGetBufferedFrames(source) == 5);
    CHECK(LND_SourceEnd(source) == LND_OK);
    CHECK(LND_QueueWritePcm(source, &input, 0, 1) == LND_ERR_STATE);
    LND_PCM same = output;
    if (layout == LND_LAYOUT_INTERLEAVED) same = input;
    CHECK(LND_SourceReadPcm(source, &same, 0, 5) == 5);
    if (layout == LND_LAYOUT_PLANAR)
        for (unsigned i = 0; i < 5; i++)
            CHECK(left[i * 2] == 1000 + (i + 5) * 40);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_READY);
    CHECK(LND_SourceReadPcm(source, &same, 0, 1) == 0 && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    CHECK(LND_QueueReset(source) == LND_OK && LND_SourceGetStatus(source) == LND_SOURCE_READY);
    CHECK(LND_SourceGetPositionFrames(source) == 0 && LND_QueueGetBufferedFrames(source) == 0);
    CHECK(LND_SourceReadPcm(source, &same, 0, 1) == 0 && LND_SourceGetStatus(source) == LND_SOURCE_WAITING);
    CHECK(LND_QueueWritePcm(source, &input, 0, 3) == 3);
    CHECK(LND_QueueDiscardFrames(source, SIZE_MAX) == 3 && LND_QueueGetBufferedFrames(source) == 0);
    CHECK(LND_QueueWritePcm(source, &input, 12, 2) == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(allocations == before);
    deny_alloc = false;
    source = LND_SourceCreateQueue(1, 16000, 1);
    CHECK(source != nullptr && LND_SourceFree(source) == LND_OK);
    finish();
}

int main(void) {
    test_queue(LND_LAYOUT_INTERLEAVED);
    test_queue(LND_LAYOUT_PLANAR);
    return report();
}
