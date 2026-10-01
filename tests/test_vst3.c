#include "codec_test.h"
#include "lindar_vst3.h"
#include "lindar_graph.h"
#include <math.h>
static void test_vst3(const char *path, int32_t format, int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, format) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = test_alloc, .realloc = test_realloc, .free = test_free}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) == LND_OK);
    uint32_t count = 0;
    CHECK(LND_Vst3ModuleScanClassCount(path, &count) == LND_OK && count == 1);
    LND_VST3_CLASS plugin = {0};
    CHECK(LND_Vst3ModuleScanClass(path, 0, &plugin) == LND_OK && !strcmp(plugin.name, "Gain test"));
    LND_VST3_OPTIONS o = {.path = path, .channels = 2, .sample_rate_hz = 48000, .block_frames = 31, .offline = true};
    memcpy(o.class_id, plugin.id, 16);
    LND_NODE *node = LND_NodeCreateVst3(&o);
    CHECK(node != nullptr);
    if (!node) {
        LND_LibraryFree();
        CHECK(live_allocations == 0);
        return;
    }
    LND_VST3_INFO info;
    CHECK(LND_NodeGetVst3Info(node, &info) == LND_OK && info.parameter_count == 2 && info.program_list_count == 1 && info.tail_frames == 32);
    LND_VST3_PARAMETER p;
    CHECK(LND_NodeGetVst3Parameter(node, 0, &p) == LND_OK && p.id == 10 && !strcmp(p.name, "Gain"));
    CHECK(LND_NodeSetVst3ParameterValue(node, 10, 0.25) == LND_OK);
    double value = 0;
    CHECK(LND_NodeGetVst3ParameterValue(node, 10, &value) == LND_OK && value == 0.25);
    CHECK(LND_NodeSetVst3ParameterValue(node, 10, NAN) == LND_ERR_INVALID_ARG);
    char text[128];
    CHECK(LND_NodeFormatVst3ParameterValue(node, 10, 0.25, text, sizeof text) == LND_OK && !strcmp(text, "25.0"));
    CHECK(LND_NodeParseVst3ParameterValue(node, 10, "75", &value) == LND_OK && value == 0.75);
    size_t bytes = 0;
    CHECK(LND_NodeSaveVst3State(node, nullptr, 0, &bytes) == LND_OK && bytes > 56);
    uint8_t *preset = malloc(bytes);
    CHECK(LND_NodeSaveVst3State(node, preset, bytes, &bytes) == LND_OK && !memcmp(preset, "VST3", 4));
    CHECK(LND_NodeSetVst3ParameterValue(node, 10, 0.75) == LND_OK);
    CHECK(LND_NodeLoadVst3State(node, preset, bytes) == LND_OK);
    CHECK(LND_NodeGetVst3ParameterValue(node, 10, &value) == LND_OK && value == 0.25);
    preset[8] ^= 1;
    CHECK(LND_NodeLoadVst3State(node, preset, bytes) == LND_ERR_FORMAT);
    preset[8] ^= 1;
    CHECK(LND_NodeLoadVst3State(node, preset, bytes - 1) == LND_ERR_FORMAT);
    free(preset);
    LND_VST3_PROGRAM_LIST programs;
    CHECK(LND_NodeGetVst3ProgramList(node, 0, &programs) == LND_OK && programs.id == 7 && programs.count == 2);
    CHECK(LND_NodeGetVst3ProgramName(node, 7, 1, text, sizeof text) == LND_OK && !strcmp(text, "Loud"));
    CHECK(LND_NodeSetVst3Program(node, 7, 1) == LND_OK);
    CHECK(LND_NodeSetVst3Program(node, 7, 2) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetVst3ParameterValue(node, 10, 0.25) == LND_OK);
    LND_VST3_TRANSPORT transport = {.tempo_bpm = 120, .numerator = 4, .denominator = 4, .playing = true};
    CHECK(LND_NodeSetVst3Transport(node, &transport) == LND_OK);
    CHECK(LND_NodeOpenVst3Editor(node, (void *)(uintptr_t)1, LND_VST3_HWND, nullptr, nullptr) == LND_OK);
    uint32_t width, height;
    CHECK(LND_NodeGetVst3EditorSize(node, &width, &height) == LND_OK && width == 320 && height == 200);
    CHECK(LND_NodeResizeVst3Editor(node, 640, 480) == LND_OK);
    CHECK(LND_NodeGetVst3EditorSize(node, &width, &height) == LND_OK && width == 640 && height == 480);
    CHECK(LND_NodeCloseVst3Editor(node) == LND_OK);
    float input[1024], output[1152];
    for (unsigned i = 0; i < 1024; ++i)
        input[i] = (i & 1) ? -0.5f : 0.5f;
    LND_PCM pcm = {.data = input, .frames = 512, .channels = 2, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 2, .sample_rate_hz = 48000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(source), node) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    CHECK(renderer && LND_SoundPlay(sound) == LND_OK);
    float planar[2][580];
    void *planes[] = {planar[0], planar[1]};
    LND_PCM out = {.data = output, .planes = planes, .frames = 576, .channels = 2, .format = LND_FORMAT_F32, .layout = layout};
    CHECK(LND_RendererReadPcm(renderer, &out, 0, 576) == 544);
    if (layout == LND_LAYOUT_PLANAR)
        for (unsigned i = 0; i < 1152; ++i) output[i] = planar[i % 2][i / 2];
    for (unsigned i = 0; i < 1024; ++i)
        CHECK(fabsf(output[i] - input[i] * 0.25f) < 0.0001f);
    for (unsigned i = 1024; i < 1088; ++i)
        CHECK(output[i] == 0);
    CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_NodeResetVst3(node) == LND_OK);
    out.frames = 580;
    out.stride_bytes = 0;
    out.layout = LND_LAYOUT_PLANAR;
    CHECK(LND_RendererReadPcm(renderer, &out, 3, 63) == 63);
    CHECK(LND_NodeSetVst3ParameterValue(node, 10, 0.75) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &out, 66, 65) == 65);
    for (unsigned c = 0; c < 2; ++c) {
        for (unsigned f = 3; f < 66; ++f) CHECK(fabsf(planar[c][f] - (c ? -0.125f : 0.125f)) < 0.0001f);
        for (unsigned f = 66; f < 131; ++f) CHECK(fabsf(planar[c][f] - (c ? -0.375f : 0.375f)) < 0.0001f);
    }
    CHECK(LND_NodeSetVst3Program(node, 7, 1) == LND_OK);
    CHECK(LND_NodeSetVst3ParameterValue(node, 10, 0.5) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &out, 131, 33) == 33);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned f = 131; f < 164; ++f) CHECK(fabsf(planar[c][f] - (c ? -0.25f : 0.25f)) < 0.0001f);
    CHECK(LND_NodeSetVst3Bypass(node, true) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &out, 164, 37) == 37);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned f = 164; f < 201; ++f) CHECK(fabsf(planar[c][f] - (c ? -0.5f : 0.5f)) < 0.0001f);
    CHECK(LND_NodeGetVst3Info(node, &info) == LND_OK && info.bypass);
    float extra[1024];
    for (unsigned i = 0; i < 1024; ++i) extra[i] = i & 1 ? -0.75f : 0.75f;
    pcm.data = extra;
    LND_SOURCE *second = LND_SourceCreate(&config);
    LND_SOUND *second_sound = LND_SourceEnsureSound(second, nullptr);
    CHECK(second && second_sound && LND_NodeConnect(LND_SourceEnsureNode(second), node) == LND_OK);
    CHECK(LND_NodeSetGain(LND_SourceGetNode(source), 1.5f) == LND_OK);
    CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK && LND_SoundPlay(sound) == LND_OK && LND_SoundPlay(second_sound) == LND_OK);
    CHECK(LND_NodeResetVst3(node) == LND_OK);
    CHECK(LND_NodeSetVst3Bypass(node, false) == LND_OK);
    CHECK(LND_NodeSetVst3ParameterValue(node, 10, 0.5) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &out, 201, 64) == 64);
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned f = 201; f < 265; ++f) CHECK(fabsf(planar[c][f] - (c ? -0.75f : 0.75f)) < 0.0001f);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    CHECK(LND_NodeFree(node) == LND_OK);
    CHECK(LND_NodeFree(LND_SourceGetNode(source)) == LND_OK);
    CHECK(LND_SoundFree(sound) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    LND_LibraryFree();
    CHECK(live_allocations == 0);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    for (int32_t layout = LND_LAYOUT_INTERLEAVED; layout <= LND_LAYOUT_PLANAR; ++layout) {
        test_vst3(argv[1], LND_FORMAT_F32, layout);
        test_vst3(argv[1], LND_FORMAT_S16, layout);
    }
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
