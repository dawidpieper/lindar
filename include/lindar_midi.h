#pragma once
#include "lindar.h"
#include "lindar_io.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Reference-counted SF2 instrument bank; sources retain it independently of the caller. */
typedef struct LND_SOUNDFONT LND_SOUNDFONT;

/** MIDI-file synthesis settings; an explicit SF2 bank is required. Zero dimensions/limits select defaults.
 * Seeking rebuilds synthesis from the beginning. These constructors do not register a global .mid
 * codec or accept live MIDI input.
 */
typedef struct LND_MIDI_OPTIONS {
    LND_SOUNDFONT *soundfont; /**< Required SoundFont retained by the source. */
    uint32_t sample_rate_hz; /**< Synthesis rate in Hz; zero uses the default. */
    uint32_t channels; /**< Mono/stereo output count; zero uses the default. */
    uint32_t voices; /**< Maximum simultaneous synthesiser voices; zero uses the default. */
    uint32_t block_frames; /**< Render block length; zero uses the default. */
    uint32_t release_ms; /**< Final note release duration in milliseconds; zero uses the default. */
    float gain_db; /**< Gain in decibels; 0 is unity. */
} LND_MIDI_OPTIONS;

/** Copied SF2 bank/program entry and display name. */
typedef struct LND_SOUNDFONT_PRESET {
    uint32_t bank; /**< SF2 bank number. */
    uint32_t program; /**< SF2 preset/program number. */
    char name[64]; /**< NUL-terminated preset display name. */
} LND_SOUNDFONT_PRESET;

/** Load bytes of SF2 data; data is borrowed only during the call.
 *
 * @param data Input bytes borrowed for the operation.
 * @param bytes Buffer length in bytes.
 * @return Owned SoundFont or NULL; release with LND_SoundfontFree.
 */
LND_API LND_SOUNDFONT *LND_SoundfontCreateMemory(const void *data, size_t bytes);

/** Load a SoundFont from borrowed io; caller retains and closes io.
 *
 * @param io IO handle to operate on.
 * @return Owned SoundFont or NULL; release with LND_SoundfontFree.
 */
LND_API LND_SOUNDFONT *LND_SoundfontCreateIo(LND_IO *io);

/** Release the caller's SoundFont reference; MIDI sources retain their references. NULL is
 * accepted.
 *
 * @param soundfont SoundFont to operate on.
 */
LND_API void LND_SoundfontFree(LND_SOUNDFONT *soundfont);

/** Get soundfont's preset count.
 *
 * @param soundfont SoundFont to operate on.
 * @return Soundfont's preset count, or zero for NULL.
 */
LND_API uint32_t LND_SoundfontGetPresetCount(const LND_SOUNDFONT *soundfont);

/** Copy soundfont's preset at zero-based index into preset.
 *
 * @param soundfont SoundFont to operate on.
 * @param index Zero-based entry index.
 * @param preset Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundfontGetPreset(const LND_SOUNDFONT *soundfont, uint32_t index, LND_SOUNDFONT_PRESET *preset);

/** Load bytes of MIDI data using options; data is borrowed only during the call and SoundFont is
 * retained.
 *
 * @param data Input bytes borrowed for the operation.
 * @param bytes Buffer length in bytes.
 * @param options Required settings, borrowed during the call.
 * @return Owned source or NULL; use LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateMidiMemory(const void *data, size_t bytes, const LND_MIDI_OPTIONS *options);

/** Load MIDI from borrowed io using options and retain its SoundFont.
 *
 * @param io IO handle to operate on.
 * @param options Required settings, borrowed during the call.
 * @return Owned source or NULL; caller closes io and releases source with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateMidiIo(LND_IO *io, const LND_MIDI_OPTIONS *options);
#ifdef __cplusplus
}
#endif
