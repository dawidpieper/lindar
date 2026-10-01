#include "lindar_bungee.h"
#include "lindar_stretch.h"
#include "stream_test.h"
#if LND_MODULE_SLIDE
#include "lindar_slide.h"
#endif
#if LND_MODULE_WAV_DECODER
#include "lindar_codecs.h"
#endif

#include <math.h>

typedef struct input {
  const float *data;
  uint64_t length, position, available;
  uint32_t channels, sample_rate_hz;
  bool live, eof, reenter;
  int32_t error;
  LND_NODE *node;
} input;

typedef struct playback {
  LND_SOURCE *source;
  LND_SOUND *sound;
  LND_NODE *node;
  LND_RENDERER *renderer;
} playback;

static int64_t read_input(void *user, void *dst, uint64_t frames) {
  input *in = user;
  if (in->reenter) {
    LND_BUNGEE_INFO info;
    CHECK(LND_NodeGetBungeeInfo(in->node, &info) == LND_ERR_BUSY);
    CHECK(LND_NodeResetBungee(in->node) == LND_ERR_BUSY);
    CHECK(LND_NodeEndBungeeInput(in->node) == LND_ERR_BUSY);
    CHECK(LND_NodeSetStretchPitchSemitones(in->node, 1) == LND_ERR_BUSY);
    in->reenter = false;
  }
  if (in->error)
    return in->error;
  uint64_t end = in->live ? in->available : in->length;
  uint64_t count = end > in->position ? end - in->position : 0;
  if (count > frames)
    count = frames;
  if (!count)
    return in->live && !in->eof ? 0 : LND_READ_EOF;
  memcpy(dst, in->data + in->position * in->channels,
         (size_t)count * in->channels * sizeof(float));
  in->position += count;
  return (int64_t)count;
}

static int32_t seek_input(void *user, uint64_t frame) {
  input *in = user;
  if (frame > in->length)
    return LND_ERR_INVALID_ARG;
  in->position = frame;
  return LND_OK;
}

static playback create(input *in, const LND_BUNGEE_CONFIG *config) {
  LND_SOURCE_PROCS procs = {.read = read_input,
                            .seek = seek_input,
                            .length_frames = in->live ? 0 : in->length};
  playback p = {0};
  uint32_t rate = in->sample_rate_hz ? in->sample_rate_hz : 16000;
  p.source = LND_SourceCreateProc(
      &procs, in, LND_FORMAT_F32, in->channels, rate,
      LND_GRAPH_SOURCE_DIRECT | (in->live ? LND_SOURCE_LIVE : 0));
  p.sound = LND_SourceEnsureSound(p.source, nullptr);
  p.node = LND_NodeCreateBungee(in->channels, rate, config);
  CHECK(p.source && p.sound && p.node);
  if (!p.source || !p.sound || !p.node)
    exit(1);
  CHECK(LND_NodeConnect(LND_SourceEnsureNode(p.source), p.node) == LND_OK);
  p.renderer = LND_RendererCreateNode(p.node);
  CHECK(p.renderer && LND_SoundPlay(p.sound) == LND_OK);
  in->node = p.node;
  return p;
}

static void destroy(playback *p) {
  deny_alloc = false;
  CHECK(LND_RendererFree(p->renderer) == LND_OK);
  CHECK(LND_NodeFree(p->node) == LND_OK);
  CHECK(LND_SourceFree(p->source) == LND_OK);
}

static size_t read_all(playback *p, float *out, size_t capacity, size_t chunk,
                       uint32_t channels) {
  LND_PCM pcm = {.data = out,
                 .frames = capacity,
                 .channels = channels,
                 .format = LND_FORMAT_F32};
  size_t total = 0;
  for (unsigned calls = 0; total < capacity && calls < 100000; calls++) {
    size_t count = (chunk < capacity - total ? chunk : capacity - total);
    int64_t got = LND_RendererReadPcm(p->renderer, &pcm, total, count);
    CHECK(got >= 0 && (uint64_t)got <= count);
    if (got < 0)
      break;
    total += (size_t)got;
    if (LND_NodeGetStatus(p->node) == LND_SOURCE_EOF)
      break;
    if (!got) {
      CHECK(LND_NodeGetStatus(p->node) == LND_SOURCE_WAITING);
      break;
    }
  }
  return total;
}

static void tone(float *data, size_t frames, uint32_t channels, double hz) {
  for (size_t i = 0; i < frames; i++)
    for (uint32_t c = 0; c < channels; c++)
      data[i * channels + c] =
          (float)(0.5 * sin(6.283185307179586 * hz * i / 16000)) *
          (c & 1 ? -1 : 1);
}

static double frequency(const float *data, size_t frames, uint32_t channels) {
  unsigned crossings = 0;
  double first = 0, last = 0;
  for (size_t i = frames / 4 + 1; i < frames * 3 / 4; i++) {
    float a = data[(i - 1) * channels], b = data[i * channels];
    if (a > 0 || b <= 0)
      continue;
    double at = i - 1 + (double)-a / (b - a);
    if (!crossings++)
      first = at;
    last = at;
  }
  return crossings > 1 ? (crossings - 1) * 16000 / (last - first) : 0;
}

static void modes(int32_t format, int32_t layout) {
  begin(format, layout);
  static float data[32000], out[256010];
  tone(data, 16000, 2, 437);
  const LND_BUNGEE_CONFIG configs[] = {
      {0},
      {.pitch_ratio = 0.5f},
      {.pitch_ratio = 2},
      {.tempo_ratio = 0.5f},
      {.tempo_ratio = 2},
      {.tempo_ratio = 0.75f, .pitch_ratio = 1.5f, .rate_ratio = 1.25f},
      {.rate_ratio = 0.5f},
      {.rate_ratio = 2},
      {.grain_adjust = -1},
      {.grain_adjust = 1},
      {.pitch_ratio = 1.5f, .resample_mode = LND_BUNGEE_RESAMPLE_INPUT},
      {.pitch_ratio = 0.5f, .resample_mode = LND_BUNGEE_RESAMPLE_OUTPUT},
  };
  for (size_t k = 0; k < sizeof configs / sizeof *configs; k++) {
    input in = {.data = data, .length = 16000, .channels = 2};
    playback p = create(&in, &configs[k]);
    deny_alloc = true;
    size_t total = read_all(&p, out, 128005, 137, 2);
    double tempo = configs[k].tempo_ratio ? configs[k].tempo_ratio : 1;
    double rate = configs[k].rate_ratio ? configs[k].rate_ratio : 1;
    double pitch = configs[k].pitch_ratio ? configs[k].pitch_ratio : 1;
    size_t expected = (size_t)llround(16000 / (tempo * rate));
    double measured = frequency(out, total, 2), target = 437 * pitch * rate;
    printf("mode %zu: frames=%zu/%zu frequency=%.3f/%.3f\n", k, total, expected,
           measured, target);
    CHECK(total == expected);
    CHECK(fabs(measured - target) < target * 0.035);
    CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
    double energy = 0;
    bool finite = true, stereo = true;
    for (size_t i = 0; i < total; i++) {
      finite &= isfinite(out[i * 2]) && isfinite(out[i * 2 + 1]);
      stereo &= fabsf(out[i * 2] + out[i * 2 + 1]) < 0.0002f;
      energy += out[i * 2] * out[i * 2];
    }
    CHECK(finite && stereo && energy > total * 0.01);
    LND_BUNGEE_INFO info;
    CHECK(LND_NodeGetBungeeInfo(p.node, &info) == LND_OK);
    CHECK(info.input_frames == 16000 && info.output_frames == total &&
          info.input_ended && !info.error);
    CHECK(!info.buffered_output_frames && !info.unprocessed_frames &&
          info.initial_latency_frames > 0);
    CHECK(info.hop_frames > 0 && info.grain_frames == info.hop_frames * 8);
    destroy(&p);
  }
  finish();
}

static void edges(void) {
  begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
  static float data[4096], out[65540];
  tone(data, 4096, 1, 211);
  const float values[] = {0.25f, 1, 4};
  for (size_t a = 0; a < 3; a++)
    for (size_t b = 0; b < 3; b++)
      for (size_t c = 0; c < 3; c++) {
        input in = {.data = data, .length = 4096, .channels = 1};
        LND_BUNGEE_CONFIG cfg = {
            .tempo_ratio = values[a], .pitch_ratio = values[b], .rate_ratio = values[c]};
        playback p = create(&in, &cfg);
        size_t total = read_all(&p, out, 65540, 511, 1);
        CHECK(total ==
              (size_t)llround(4096 / ((double)cfg.tempo_ratio * cfg.rate_ratio)));
        CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
        bool finite = true;
        for (size_t i = 0; i < total; i++)
          finite &= isfinite(out[i]);
        CHECK(finite);
        destroy(&p);
      }
  const size_t sizes[] = {0, 1, 2, 15, 127, 128, 129, 255, 256, 257, 1023};
  for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
    input in = {.data = data, .length = sizes[i], .channels = 1};
    playback p = create(&in, &(LND_BUNGEE_CONFIG){.tempo_ratio = 0.75f});
    CHECK(read_all(&p, out, 65540, 17, 1) == (size_t)llround(sizes[i] / 0.75));
    CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
    destroy(&p);
  }
  finish();
}

static void waiting_reset_flush(void) {
  begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
  static float data[16000], out[40000], again[40000];
  tone(data, 16000, 1, 437);
  input in = {.data = data,
              .length = 16000,
              .channels = 1,
              .live = true,
              .reenter = true};
  playback p = create(&in, &(LND_BUNGEE_CONFIG){.tempo_ratio = 0.75f, .pitch_ratio = 1.5f});
  CHECK(read_all(&p, out, 40000, 127, 1) == 0);
  CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_WAITING && !in.reenter);
  size_t total = 0;
  for (unsigned i = 0; i < 16; i++) {
    in.available += 1000;
    total += read_all(&p, out + total, 40000 - total, 113, 1);
    CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_WAITING);
  }
  in.eof = true;
  total += read_all(&p, out + total, 40000 - total, 71, 1);
  CHECK(total == 21333 && LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
  CHECK(LND_SoundSeekFrames(p.sound, 0) == LND_OK &&
        LND_SoundPlay(p.sound) == LND_OK);
  CHECK(LND_NodeResetBungee(p.node) == LND_OK);
  CHECK(read_all(&p, again, 40000, 997, 1) == total);
  double difference = 0;
  for (size_t i = 0; i < total; i++)
    difference += fabs(out[i] - again[i]);
  CHECK(difference < total * 0.00001);
  CHECK(LND_SoundSeekFrames(p.sound, 0) == LND_OK &&
        LND_SoundPlay(p.sound) == LND_OK);
  CHECK(LND_NodeResetStretch(p.node) == LND_OK);
  in.available = 3000;
  in.eof = false;
  total = read_all(&p, out, 40000, 257, 1);
  LND_BUNGEE_INFO info;
  CHECK(LND_NodeGetBungeeInfo(p.node, &info) == LND_OK &&
        info.input_frames == 3000);
  CHECK(LND_NodeEndBungeeInput(p.node) == LND_OK);
  total += read_all(&p, out + total, 40000 - total, 257, 1);
  CHECK(total == 4000 && in.position == 3000 &&
        LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
  destroy(&p);
  finish();
}

static void channels_and_rates(void) {
  begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
  static float data[2048 * 32], out[4096 * 32];
  const uint32_t channels[] = {1, 3, 8, 32},
                 rates[] = {8000, 44100, 48000, 192000};
  for (unsigned k = 0; k < 4; k++) {
    tone(data, 2048, channels[k], 437);
    input in = {.data = data,
                .length = 2048,
                .channels = channels[k],
                .sample_rate_hz = rates[k]};
    playback p =
        create(&in, &(LND_BUNGEE_CONFIG){.tempo_ratio = 0.75f,
                                         .pitch_ratio = 1.5f,
                                         .grain_adjust = k == 3 ? 1 : 0});
    deny_alloc = true;
    CHECK(read_all(&p, out, 4096, 137, channels[k]) == 2731);
    CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
    bool finite = true;
    for (size_t i = 0; i < 2731 * channels[k]; i++)
      finite &= isfinite(out[i]);
    CHECK(finite);
    destroy(&p);
  }
  finish();
}

#if LND_MODULE_WAV_DECODER
static void decoded(void) {
  FILE *file = fopen("audiosamples/tone.wav", "rb");
  CHECK(file != nullptr);
  if (!file)
    return;
  CHECK(fseek(file, 0, SEEK_END) == 0);
  long size = ftell(file);
  CHECK(size > 0 && fseek(file, 0, SEEK_SET) == 0);
  uint8_t *bytes = malloc((size_t)size);
  CHECK(bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size);
  fclose(file);
  begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
  playback p = {0};
  p.source = LND_SourceCreateEncodedMemory(
      bytes, (size_t)size, LND_ENCODED_SOURCE_LIGHTWEIGHT,
      &(LND_ENCODED_SOURCE_OPTIONS){.codec_name = "wav", .block_frames = 37});
  CHECK(p.source != nullptr);
  p.sound = LND_SourceEnsureSound(p.source, nullptr);
  uint32_t channels = LND_SoundGetChannels(p.sound),
           rate = LND_SoundGetSampleRateHz(p.sound);
  uint64_t length = LND_SoundGetLengthFrames(p.sound);
  p.node = LND_NodeCreateBungee(
      channels, rate, &(LND_BUNGEE_CONFIG){.tempo_ratio = 2, .pitch_ratio = 0.5f});
  CHECK(p.node && LND_SoundSetOutput(p.sound, p.node) == LND_OK);
  p.renderer = LND_RendererCreateNode(p.node);
  CHECK(p.renderer && LND_SoundPlay(p.sound) == LND_OK);
  float out[256 * 32];
  LND_PCM pcm = {.data = out,
                 .frames = 256,
                 .channels = channels,
                 .format = LND_FORMAT_F32};
  uint64_t total = 0;
  deny_alloc = true;
  for (unsigned calls = 0; calls < 10000; calls++) {
    int64_t n = LND_RendererReadPcm(p.renderer, &pcm, 0, 256);
    CHECK(n >= 0);
    if (n <= 0)
      break;
    total += (uint64_t)n;
  }
  CHECK(total == (length + 1) / 2 &&
        LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
  CHECK(LND_NodeFree(LND_SourceEnsureNode(p.source)) == LND_OK);
  destroy(&p);
  finish();
  free(bytes);
}
#endif

static void invalid_and_failure(void) {
  CHECK(!LND_NodeCreateBungee(1, 16000, nullptr) &&
        LND_ErrorGetLast() == LND_ERR_STATE);
  begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
  CHECK(!LND_NodeCreateBungee(33, 16000, nullptr));
  CHECK(!LND_NodeCreateBungee(1, 7000, nullptr));
  CHECK(!LND_NodeCreateBungee(1, 16000, &(LND_BUNGEE_CONFIG){.pitch_ratio = NAN}));
  CHECK(
      !LND_NodeCreateBungee(1, 16000, &(LND_BUNGEE_CONFIG){.grain_adjust = 2}));
  CHECK(!LND_NodeCreateBungee(1, 16000,
                              &(LND_BUNGEE_CONFIG){.resample_mode = -1}));
  CHECK(!LND_NodeCreateBungee(1, 16000, &(LND_BUNGEE_CONFIG){.tempo_ratio = 5}));
  CHECK(LND_NodeEndBungeeInput(nullptr) == LND_ERR_INVALID_ARG);
  CHECK(LND_NodeResetBungee(nullptr) == LND_ERR_INVALID_ARG);
  CHECK(LND_NodeGetBungeeInfo(nullptr, nullptr) == LND_ERR_INVALID_ARG);
  LND_NODE *node = LND_NodeCreateBungee(1, 16000, nullptr);
  CHECK(node != nullptr);
  CHECK(LND_NodeSetParam(node, LND_BUNGEE_PARAM_PITCH_RATIO, INFINITY) ==
        LND_ERR_INVALID_ARG);
  CHECK(LND_NodeSetParam(node, LND_BUNGEE_PARAM_TEMPO_RATIO, 0) ==
        LND_ERR_INVALID_ARG);
  CHECK(LND_NodeSetParam(node, LND_PARAM_USER + 5, 1) == LND_ERR_INVALID_ARG);
  CHECK(LND_NodeSetStretchPitchSemitones(node, 12) == LND_OK);
  CHECK(LND_NodeGetStretchPitchSemitones(node) == 12);
  CHECK(LND_NodeSetStretchTempoChangePercent(node, -50) == LND_OK);
  CHECK(LND_NodeSetStretchRateChangePercent(node, 100) == LND_OK);
  CHECK(LND_NodeFree(node) == LND_OK);
  bool success = false;
  for (int i = 0; i < 32; i++) {
    fail_after = i;
    node = LND_NodeCreateBungee(1, 16000, nullptr);
    fail_after = -1;
    if (node) {
      CHECK(LND_NodeFree(node) == LND_OK);
      success = true;
      break;
    }
    CHECK(LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
  }
  CHECK(success);
  static float data[4096], out[8192];
  tone(data, 4096, 1, 437);
  input in = {.data = data, .length = 4096, .channels = 1, .error = LND_ERR_IO};
  playback p = create(&in, nullptr);
  LND_PCM pcm = {
      .data = out, .frames = 8192, .channels = 1, .format = LND_FORMAT_F32};
  CHECK(LND_RendererReadPcm(p.renderer, &pcm, 0, 128) == LND_ERR_IO);
  LND_BUNGEE_INFO info;
  CHECK(LND_NodeGetBungeeInfo(p.node, &info) == LND_OK && info.error == LND_ERR_IO);
  in.error = 0;
  CHECK(LND_NodeResetBungee(p.node) == LND_OK);
  CHECK(LND_SoundSeekFrames(p.sound, 0) == LND_OK &&
        LND_SoundPlay(p.sound) == LND_OK);
  CHECK(read_all(&p, out, 8192, 128, 1) == 4096);
  destroy(&p);
  data[0] = NAN;
  in = (input){.data = data, .length = 4096, .channels = 1};
  p = create(&in, nullptr);
  CHECK(LND_RendererReadPcm(p.renderer, &pcm, 0, 128) == LND_ERR_FORMAT);
  destroy(&p);
  finish();
}

#if LND_MODULE_SLIDE
static void slide(void) {
  begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
  static float data[16000], out[40000], second[40000];
  tone(data, 16000, 1, 437);
  input a = {.data = data, .length = 16000, .channels = 1}, b = a;
  playback pa = create(&a, nullptr), pb = create(&b, nullptr);
  LND_SLIDE_CONFIG cfg = {
      .duration_frames = 8000, .step_frames = 64, .curve = LND_SLIDE_LINEAR};
  CHECK(LND_NodeSlideParam(pa.node, LND_STRETCH_PARAM_PITCH_RATIO, 2, &cfg) ==
        LND_OK);
  CHECK(LND_NodeSlideParam(pb.node, LND_BUNGEE_PARAM_PITCH_RATIO, 2, &cfg) == LND_OK);
  CHECK(LND_NodeSlideParam(pa.node, LND_STRETCH_PARAM_TEMPO_RATIO, 0.75f, &cfg) ==
        LND_OK);
  CHECK(LND_NodeSlideParam(pb.node, LND_BUNGEE_PARAM_TEMPO_RATIO, 0.75f, &cfg) ==
        LND_OK);
  CHECK(LND_NodeSlideParam(pa.node, LND_STRETCH_PARAM_RATE_RATIO, 1.25f, &cfg) ==
        LND_OK);
  CHECK(LND_NodeSlideParam(pb.node, LND_BUNGEE_PARAM_RATE_RATIO, 1.25f, &cfg) ==
        LND_OK);
  size_t count = read_all(&pa, out, 40000, 127, 1);
  CHECK(read_all(&pb, second, 40000, 997, 1) == count && count > 16000);
  double difference = 0;
  for (size_t i = 0; i < count; i++)
    difference += fabs(out[i] - second[i]);
  CHECK(difference < count * 0.00001);
  CHECK(!LND_NodeIsSliding(pa.node, LND_STRETCH_PARAM_PITCH_RATIO));
  CHECK(LND_NodeGetParam(pa.node, LND_STRETCH_PARAM_PITCH_RATIO) == 2);
  destroy(&pa);
  destroy(&pb);
  finish();
}
#endif

int main(void) {
  CHECK(LND_BungeeGetVersion() && *LND_BungeeGetVersion());
  invalid_and_failure();
  modes(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
  modes(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
  edges();
  channels_and_rates();
  waiting_reset_flush();
#if LND_MODULE_WAV_DECODER
  decoded();
#endif
#if LND_MODULE_SLIDE
  slide();
#endif
  return report();
}
