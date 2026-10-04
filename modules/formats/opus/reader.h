#pragma once

#include "io/io.h"
#include <opusfile.h>

/** Open borrowed IO; release the reader with op_free. */
OggOpusFile *lnd_opus_open_io(lnd_io *io);
/** Validate link ends and read exact duration; changes the IO position. */
int32_t lnd_opus_io_duration(lnd_io *io, OggOpusFile *file, int64_t *duration_us);
