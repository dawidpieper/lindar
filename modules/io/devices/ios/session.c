#include "session.h"
#include "src/context.h"
#include "src/error.h"

static LND_IOS_SESSION_CONFIG lnd_ios_config;
static uint32_t lnd_ios_streams[2];
static int32_t lnd_ios_orientation;
static bool lnd_ios_activated;

static int32_t lnd_ios_input_orientation(void) {
    return lnd_ios_config.orientation == LND_IOS_ORIENTATION_AUTO ? lnd_ios_orientation : lnd_ios_config.orientation;
}

static bool lnd_ios_manual(void) { return (lnd_ios_config.flags & LND_IOS_SESSION_MANUAL) != 0; }

int32_t LND_IosSessionSetConfig(const LND_IOS_SESSION_CONFIG *config) {
    LND_IOS_SESSION_CONFIG c = config ? *config : (LND_IOS_SESSION_CONFIG){0};
    if (c.category < LND_IOS_CATEGORY_DEFAULT || c.category > LND_IOS_CATEGORY_MULTI_ROUTE || c.mode < LND_IOS_MODE_DEFAULT ||
        c.mode > LND_IOS_MODE_SPOKEN_AUDIO || c.options & ~((LND_IOS_SESSION_INTERRUPT_SPOKEN_AUDIO << 1) - 1u) ||
        c.flags & ~(LND_IOS_SESSION_MANUAL | LND_IOS_SESSION_ALLOW_HAPTICS) || c.sample_rate_hz > 384000 || c.buffer_duration_us > 1000000 ||
        c.recording < LND_IOS_RECORDING_DEFAULT || c.recording > LND_IOS_RECORDING_STEREO || c.orientation < LND_IOS_ORIENTATION_DEFAULT ||
        c.orientation > LND_IOS_ORIENTATION_LANDSCAPE_RIGHT || (c.orientation && c.recording != LND_IOS_RECORDING_STEREO))
        return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t r = lnd_ios_streams[0] || lnd_ios_streams[1] || lnd_ios_activated ? LND_ERR_BUSY : LND_OK;
    if (r == LND_OK) lnd_ios_config = c;
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_IosSessionGetConfig(LND_IOS_SESSION_CONFIG *config) {
    if (!config) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    *config = lnd_ios_config;
    lnd_context_unlock();
    return LND_OK;
}

int32_t LND_IosSessionApply(void) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t r = lnd_ios_native_configure(&lnd_ios_config);
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_IosSessionApplyInput(void) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t r = lnd_ios_native_input(lnd_ios_config.recording, lnd_ios_input_orientation());
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_IosSessionSetActive(bool active) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t r = lnd_ios_native_active(active);
    if (r == LND_OK) {
        if (!active) lnd_ios_activated = false;
        LND_CoreAudioSetSessionActive(active);
    }
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_IosSetDeviceOrientation(int32_t orientation) {
    if (orientation < LND_IOS_ORIENTATION_DEFAULT || orientation == LND_IOS_ORIENTATION_AUTO || orientation > LND_IOS_ORIENTATION_LANDSCAPE_RIGHT)
        return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t r = LND_OK;
    if (orientation && orientation != lnd_ios_orientation) {
        if (!lnd_ios_manual() && lnd_ios_streams[1] && lnd_ios_config.orientation == LND_IOS_ORIENTATION_AUTO) r = lnd_ios_native_orientation(orientation);
        if (r == LND_OK) lnd_ios_orientation = orientation;
    }
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_IosGetDeviceOrientation(void) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t orientation = lnd_ios_orientation;
    lnd_context_unlock();
    return orientation;
}

int32_t lnd_ios_session_acquire(bool capture) {
    if (lnd_ios_streams[capture] == UINT32_MAX) return LND_ERR_BUSY;
    if (!lnd_ios_manual()) {
        bool first = !lnd_ios_streams[0] && !lnd_ios_streams[1];
        int32_t r = first ? lnd_ios_native_configure(&lnd_ios_config) : LND_OK;
        if (r == LND_OK && first) {
            r = lnd_ios_native_active(true);
            if (r == LND_OK) lnd_ios_activated = true;
        }
        if (r == LND_OK && capture) r = lnd_ios_native_input(lnd_ios_config.recording, lnd_ios_input_orientation());
        if (r != LND_OK) {
            if (first && lnd_ios_activated && lnd_ios_native_active(false) == LND_OK) lnd_ios_activated = false;
            return r;
        }
    }
    lnd_ios_streams[capture]++;
    return LND_OK;
}

uint32_t lnd_ios_session_input_channels(void) { return lnd_ios_manual() ? 0 : (uint32_t)lnd_ios_config.recording; }

void lnd_ios_session_release(bool capture) {
    if (lnd_ios_streams[capture]) lnd_ios_streams[capture]--;
    if (!lnd_ios_streams[0] && !lnd_ios_streams[1] && lnd_ios_activated && lnd_ios_native_active(false) == LND_OK) lnd_ios_activated = false;
}

void lnd_ios_free(void) {
    if (lnd_ios_activated) lnd_ios_native_active(false);
    lnd_ios_config = (LND_IOS_SESSION_CONFIG){0};
    lnd_ios_streams[0] = lnd_ios_streams[1] = 0;
    lnd_ios_orientation = LND_IOS_ORIENTATION_DEFAULT;
    lnd_ios_activated = false;
}
