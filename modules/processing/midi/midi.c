#include "lindar_midi.h"
#include "src/alloc.h"
#include "src/error.h"
#include "src/spinlock.h"
#include "src/callback.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#define TSF_NO_STDIO
#define TML_NO_STDIO
#define TSF_MALLOC lnd_alloc
#define TSF_REALLOC lnd_realloc
#define TSF_FREE lnd_free
#define TML_MALLOC lnd_alloc
#define TML_REALLOC lnd_realloc
#define TML_FREE lnd_free
#define TSF_IMPLEMENTATION
#define TML_IMPLEMENTATION
#include "tinysoundfont/tsf.h"
#include "tinysoundfont/tml.h"

struct LND_SOUNDFONT {
    tsf *synth;
    lnd_spinlock lock;
    lnd_atomic_u32 refs;
};

typedef struct lnd_midi {
    LND_SOUNDFONT *font;
    tsf *synth;
    tml_message *messages;
    tml_message *next;
    LND_MIDI_OPTIONS options;
    uint64_t position;
    uint64_t length;
    bool released;
    float *buffer;
    size_t buffered;
    size_t offset;
} lnd_midi;

static int read_io(void *user, void *data, unsigned size) { return (int)LND_IoRead(user, data, size > INT_MAX ? INT_MAX : size); }

static int skip_io(void *user, unsigned count) {
    LND_IO *io = user;
    uint64_t pos = LND_IoGetPositionBytes(io);
    return pos <= UINT64_MAX - count && LND_IoSeekBytes(io, pos + count) == LND_OK;
}

static LND_SOUNDFONT *font_create(tsf *synth) {
    if (!synth) return lnd_error_null(LND_ERR_FORMAT);
    LND_SOUNDFONT *f = lnd_alloc_zero(sizeof *f);
    if (!f) {
        tsf_close(synth);
        return nullptr;
    }
    f->synth = synth;
    lnd_store(&f->refs, 1);
    return f;
}

LND_SOUNDFONT *LND_SoundfontCreateMemory(const void *data, size_t bytes) {
    if (!data || !bytes || bytes > INT_MAX) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    return font_create(tsf_load_memory(data, (int)bytes));
}

LND_SOUNDFONT *LND_SoundfontCreateIo(LND_IO *io) {
    if (!io || !LND_IoCanSeek(io)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    struct tsf_stream stream = {io, read_io, skip_io};
    return font_create(tsf_load(&stream));
}

void LND_SoundfontFree(LND_SOUNDFONT *f) {
    if (!f || lnd_sub(&f->refs, 1) != 1) return;
    tsf_close(f->synth);
    lnd_free(f);
}

uint32_t LND_SoundfontGetPresetCount(const LND_SOUNDFONT *f) { return f ? (uint32_t)tsf_get_presetcount(f->synth) : 0; }

int32_t LND_SoundfontGetPreset(const LND_SOUNDFONT *f, uint32_t index, LND_SOUNDFONT_PRESET *preset) {
    if (!f || !preset || index >= LND_SoundfontGetPresetCount(f)) return LND_ERR_INVALID_ARG;
    const struct tsf_preset *p = f->synth->presets + index;
    *preset = (LND_SOUNDFONT_PRESET){.bank = p->bank, .program = p->preset};
    memcpy(preset->name, p->presetName, sizeof p->presetName);
    return LND_OK;
}

static tsf *synth_create(lnd_midi *s) {
    lnd_spinlock_lock(&s->font->lock);
    tsf *synth = tsf_copy(s->font->synth);
    lnd_spinlock_unlock(&s->font->lock);
    if (!synth) return nullptr;
    tsf_set_output(synth, s->options.channels == 1 ? TSF_MONO : TSF_STEREO_INTERLEAVED, (int)s->options.sample_rate_hz, s->options.gain_db);
    bool ok = tsf_set_max_voices(synth, (int)s->options.voices) != 0;
    for (int c = 0; ok && c < 16; ++c) {
        ok = tsf_channel_set_presetindex(synth, c, 0) != 0;
        tsf_channel_set_presetnumber(synth, c, 0, c == 9);
    }
    if (ok) return synth;
    lnd_spinlock_lock(&s->font->lock);
    tsf_close(synth);
    lnd_spinlock_unlock(&s->font->lock);
    return nullptr;
}

static void midi_close(void *user) {
    lnd_midi *s = user;
    if (s->synth) {
        lnd_spinlock_lock(&s->font->lock);
        tsf_close(s->synth);
        lnd_spinlock_unlock(&s->font->lock);
    }
    LND_SoundfontFree(s->font);
    tml_free(s->messages);
    lnd_free(s->buffer);
    lnd_free(s);
}

static void message(lnd_midi *s, const tml_message *m) {
    switch (m->type) {
    case TML_NOTE_ON:
        tsf_channel_note_on(s->synth, m->channel, m->key, m->velocity / 127.0f);
        break;
    case TML_NOTE_OFF:
        tsf_channel_note_off(s->synth, m->channel, m->key);
        break;
    case TML_CONTROL_CHANGE:
        tsf_channel_midi_control(s->synth, m->channel, m->control, m->control_value);
        break;
    case TML_PROGRAM_CHANGE:
        tsf_channel_set_presetnumber(s->synth, m->channel, m->program, m->channel == 9);
        break;
    case TML_PITCH_BEND:
        tsf_channel_set_pitchwheel(s->synth, m->channel, m->pitch_bend);
        break;
    }
}

static size_t midi_render(lnd_midi *s, size_t frames) {
    frames = (size_t)LND_MIN(frames, s->length - s->position);
    size_t done = 0;
    while (done < frames) {
        while (s->next && (uint64_t)s->next->time * s->options.sample_rate_hz / 1000 <= s->position) {
            message(s, s->next);
            s->next = s->next->next;
        }
        if (!s->next && !s->released) {
            for (int c = 0; c < 16; ++c)
                tsf_channel_set_sustain(s->synth, c, 0);
            tsf_note_off_all(s->synth);
            s->released = true;
        }
        uint64_t end = s->next ? (uint64_t)s->next->time * s->options.sample_rate_hz / 1000 : s->length;
        size_t n = (size_t)LND_MIN(frames - done, end - s->position);
        tsf_render_float(s->synth, s->buffer + done * s->options.channels, (int)n, 0);
        done += n;
        s->position += n;
    }
    return done;
}

static int64_t midi_read(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_midi *s = user;
    size_t done = 0;
    while (done < frames) {
        if (s->offset == s->buffered) {
            s->buffered = midi_render(s, s->options.block_frames);
            s->offset = 0;
            if (!s->buffered) break;
        }
        size_t n = LND_MIN(frames - done, s->buffered - s->offset);
        LND_PCM input = {.data = s->buffer, .frames = s->buffered, .channels = s->options.channels, .format = LND_FORMAT_F32};
        int32_t r = LND_PcmConvert(pcm, offset + done, &input, s->offset, n);
        if (r) return r;
        s->offset += n;
        done += n;
    }
    return done ? (int64_t)done : LND_READ_EOF;
}

static int32_t midi_seek(void *user, uint64_t frame) {
    lnd_midi *s = user;
    if (frame > s->length) return LND_ERR_INVALID_ARG;
    tsf *synth = synth_create(s);
    if (!synth) return LND_ERR_OUT_OF_MEMORY;
    lnd_spinlock_lock(&s->font->lock);
    tsf_close(s->synth);
    lnd_spinlock_unlock(&s->font->lock);
    s->synth = synth;
    s->next = s->messages;
    s->position = 0;
    s->released = false;
    s->offset = s->buffered = 0;
    while (frame) {
        s->buffered = midi_render(s, s->options.block_frames);
        s->offset = (size_t)LND_MIN(frame, s->buffered);
        frame -= s->offset;
    }

    return LND_OK;
}

static LND_SOURCE *midi_create(tml_message *messages, const LND_MIDI_OPTIONS *options) {
    if (!messages) return lnd_error_null(LND_ERR_FORMAT);
    LND_MIDI_OPTIONS o = *options;
    if (!o.sample_rate_hz) o.sample_rate_hz = 48000;
    if (!o.channels) o.channels = 2;
    if (!o.voices) o.voices = 256;
    if (!o.block_frames) o.block_frames = 1024;
    if (!o.release_ms) o.release_ms = 1000;
    if (o.sample_rate_hz < 8000 || o.sample_rate_hz > 384000 || o.channels > 2 || o.voices > 65536 || o.block_frames > 65536 || o.release_ms > 60000 ||
        !isfinite(o.gain_db) || o.gain_db < -120 || o.gain_db > 24) {
        tml_free(messages);
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    lnd_midi *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        tml_free(messages);
        return nullptr;
    }
    s->messages = s->next = messages;
    s->font = o.soundfont;
    lnd_add(&s->font->refs, 1);
    s->options = o;
    unsigned length;
    tml_get_info(messages, nullptr, nullptr, nullptr, nullptr, &length);
    s->length = ((uint64_t)length + o.release_ms) * o.sample_rate_hz / 1000;
    s->buffer = lnd_alloc((size_t)o.block_frames * o.channels * sizeof(float));
    s->synth = synth_create(s);
    if (!s->buffer || !s->synth) {
        midi_close(s);
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    LND_SOURCE_CONFIG config = {.read = midi_read,
                                .seek = midi_seek,
                                .close = midi_close,
                                .user = s,
                                .channels = o.channels,
                                .sample_rate_hz = o.sample_rate_hz,
                                .block_frames = o.block_frames,
                                .length_frames = s->length,
                                .length_known = true};
    LND_SOURCE *source = LND_SourceCreate(&config);
    if (!source) midi_close(s);
    return source;
}

LND_SOURCE *LND_SourceCreateMidiMemory(const void *data, size_t bytes, const LND_MIDI_OPTIONS *options) {
    if (!data || !bytes || bytes > INT_MAX || !options || !options->soundfont) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    return midi_create(tml_load_memory(data, (int)bytes), options);
}

LND_SOURCE *LND_SourceCreateMidiIo(LND_IO *io, const LND_MIDI_OPTIONS *options) {
    if (!io || !options || !options->soundfont) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    struct tml_stream stream = {io, read_io};
    return midi_create(tml_load(&stream), options);
}
