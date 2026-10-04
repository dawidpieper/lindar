#pragma once

#include "lindar_decode.h"
#include "lindar_codecs.h"
#include "src/platform.h"

void lnd_decoder_reset(LND_DECODER *decoder);
/** Attach stream state; its close callback releases decoder resources on reset. */
void lnd_decoder_attach(LND_DECODER *decoder, const LND_CODEC *codec, void *state, const LND_CODEC_INFO *info);
bool lnd_decoder_can_spill(const LND_DECODER *decoder);
int32_t lnd_decoder_spill(LND_DECODER *decoder, LND_IO *io);
uint64_t lnd_decoder_fed(const LND_DECODER *decoder);
uint64_t lnd_decoder_consumed(const LND_DECODER *decoder);

int32_t lnd_decoder_configure(LND_DECODER *decoder, const LND_CODEC_STREAM_CONFIG *config);

bool lnd_decoder_supports_stream(const LND_DECODER *decoder, const char *identifiers);
