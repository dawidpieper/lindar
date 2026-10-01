#include "format.h"

const char *lnd_format_name(int32_t format) {
    switch (format) {
    case LND_FORMAT_U8:
        return "u8";
    case LND_FORMAT_S16:
        return "s16";
    case LND_FORMAT_S24:
        return "s24";
    case LND_FORMAT_S32:
        return "s32";
    case LND_FORMAT_F32:
        return "f32";
    case LND_FORMAT_F64:
        return "f64";
    default:
        return "none";
    }
}
