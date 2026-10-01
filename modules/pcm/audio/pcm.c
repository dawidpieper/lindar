#include "source.h"
#include "src/error.h"
#include "src/pcm.h"

int64_t lnd_source_read_pcm(lnd_source *s, const LND_PCM *pcm, size_t offset, size_t frames, bool sync) {
    if (!frames) return 0;
    if (lnd_source_status(s) == LND_SOURCE_EOF) return 0;
    if (s->deferred_error) {
        int32_t error = s->deferred_error;
        s->deferred_error = LND_OK;
        lnd_store(&s->status, error);
        return lnd_error(error);
    }
    if (lnd_source_status(s) < 0) lnd_store(&s->status, LND_SOURCE_READY);
    if (pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == s->channels * sizeof(float) &&
        (uintptr_t)lnd_pcm_at(pcm, 0, offset) % alignof(float) == 0) {
        float *data = (float *)lnd_pcm_at(pcm, 0, offset);
        uint64_t got = sync ? lnd_source_read_sync(s, data, frames) : lnd_source_read(s, data, frames);
        return !got && lnd_source_status(s) < 0 ? lnd_error(lnd_source_status(s)) : (int64_t)got;
    }
    if (sync && s->vt->read_pcm_sync) return lnd_source_result(s, s->vt->read_pcm_sync(s, pcm, offset, frames), frames);
    if (s->vt->read_pcm) return lnd_source_result(s, s->vt->read_pcm(s, pcm, offset, frames), frames);
    float data[256];
    LND_PCM scratch = {.data = data, .frames = 256 / s->channels, .channels = s->channels, .format = LND_FORMAT_F32};
    size_t done = 0;
    while (done < frames) {
        size_t want = LND_MIN(frames - done, scratch.frames);
        uint64_t got = sync ? lnd_source_read_sync(s, data, want) : lnd_source_read(s, data, want);
        if (got > want) return lnd_error(LND_ERR_IO);
        int32_t result = LND_PcmConvert(pcm, offset + done, &scratch, 0, (size_t)got);
        if (result != LND_OK) return result;
        done += (size_t)got;
        if (got < want) break;
    }
    return !done && lnd_source_status(s) < 0 ? lnd_error(lnd_source_status(s)) : (int64_t)done;
}
