#pragma once

#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"

int32_t lnd_codec_rank(lnd_io *io, const LND_CODEC *const *input, uint32_t count, bool forced, const char *extension, const LND_CODEC **ordered);
int32_t lnd_codec_open_candidates(lnd_io *io, const LND_CODEC *const *input, uint32_t count, bool forced, const char *extension, const LND_CODEC **codec,
                                  LND_CODEC_INFO *info, void **state);
int32_t lnd_codec_open(lnd_io *io, const char *extension, const char *forced, const LND_CODEC **codec, LND_CODEC_INFO *info, void **state);

bool lnd_codec_stream_supported(const LND_CODEC *const *codecs, uint32_t count, const char *identifiers);
