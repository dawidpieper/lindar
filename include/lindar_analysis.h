#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Include every channel in level analysis; not accepted by FftExecute. */
#define LND_CHANNEL_ALL UINT32_MAX

enum {
    LND_WINDOW_RECTANGULAR = 0, /**< No taper; best bin resolution with more spectral leakage. */
    LND_WINDOW_HANN = 1, /**< Hann taper for general spectral analysis. */
    LND_WINDOW_HAMMING = 2, /**< Hamming taper suppressing nearby sidelobes. */
    LND_WINDOW_BLACKMAN = 3, /**< Blackman taper with stronger sidelobe suppression and a wider main lobe. */
};

/** Measured peak/RMS values; dBFS uses amplitude 1 as 0 dB and silence as negative infinity. */
typedef struct LND_LEVELS {
    double peak; /**< Largest absolute linear sample amplitude. */
    double rms; /**< Root mean square linear sample amplitude. */
    double peak_dbfs; /**< Peak in dB relative to amplitude 1. */
    double rms_dbfs; /**< RMS in dB relative to amplitude 1. */
    size_t samples; /**< Number of channel samples measured, not frames. */
} LND_LEVELS;

/** One complex Fourier bin; real and imaginary parts use the transform's scaling. */
typedef struct LND_COMPLEX {
    float real; /**< Real component. */
    float imag; /**< Imaginary component. */
} LND_COMPLEX;

/** Reusable real FFT workspace; release with FftFree, including instances in caller storage. */
typedef struct LND_FFT LND_FFT;

/** Convert linear amplitude to dBFS.
 *
 * @param amplitude Linear amplitude; 1 is full scale.
 * @return Amplitude in dBFS; zero amplitude yields negative infinity.
 */
LND_API double LND_AmplitudeToDb(double amplitude);

/** Convert dBFS to linear amplitude.
 *
 * @param db Amplitude in dBFS; 0 is full scale.
 * @return The linear amplitude for db; 0 dB is unity.
 */
LND_API double LND_DbToAmplitude(double db);

/** Measure frames at offset_frames in pcm for channel, or CHANNEL_ALL. Copy levels.
 *
 * @param pcm PCM descriptor; sample storage remains borrowed.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @param channel Zero-based channel index, or LND_CHANNEL_ALL.
 * @param levels Receives peak, RMS and dBFS levels.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_PcmAnalyzeLevels(const LND_PCM *pcm, size_t offset_frames, size_t frames, uint32_t channel, LND_LEVELS *levels);

/** Get workspace bytes for a power-of-two transform of frames.
 *
 * @param frames Power-of-two transform length in frames.
 * @return Workspace bytes for a power-of-two transform of frames, or zero if unsupported.
 */
LND_API size_t LND_FftGetMemoryBytes(uint32_t frames);

/** Initialise a frames-point transform with window in bytes of storage aligned to max_align_t.
 *
 * @param memory Caller-owned storage aligned to max_align_t.
 * @param bytes Available size of memory in bytes.
 * @param frames Power-of-two transform length in frames.
 * @param window LND_WINDOW transform window.
 * @return Fft or NULL; storage remains caller-owned.
 */
LND_API LND_FFT *LND_FftInit(void *memory, size_t bytes, uint32_t frames, int32_t window);

/** Allocate a frames-point transform with window.
 *
 * @param frames Power-of-two transform length in frames.
 * @param window LND_WINDOW transform window.
 * @return An owned FFT or NULL; release with LND_FftFree.
 */
LND_API LND_FFT *LND_FftCreate(uint32_t frames, int32_t window);

/** Release fft's workspace, preserving caller storage from Init; NULL is accepted.
 *
 * @param fft FFT workspace.
 */
LND_API void LND_FftFree(LND_FFT *fft);

/** Get fft's transform length in frames.
 *
 * @param fft FFT workspace.
 * @return Fft's transform length in frames, or zero for NULL.
 */
LND_API uint32_t LND_FftGetFrames(const LND_FFT *fft);

/** Get fft's real-spectrum bin count, including DC and Nyquist.
 *
 * @param fft FFT workspace.
 * @return Fft's real-spectrum bin count, including DC and Nyquist, or zero for NULL.
 */
LND_API uint32_t LND_FftGetBinCount(const LND_FFT *fft);

/** Transform one window from pcm at offset_frames for zero-based channel. Write bins within
 * bins_count capacity.
 *
 * @param fft FFT workspace.
 * @param pcm PCM descriptor; sample storage remains borrowed.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param channel Zero-based channel index.
 * @param bins Output complex spectrum; provide at least LND_FftGetBinCount(fft) entries.
 * @param bins_count Writable capacity of bins in complex entries.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_FftExecute(LND_FFT *fft, const LND_PCM *pcm, size_t offset_frames, uint32_t channel, LND_COMPLEX *bins, size_t bins_count);

/** Normalise bins to amplitudes, optionally in dBFS; count must equal LND_FftGetBinCount.
 *
 * @param fft FFT workspace.
 * @param bins Input complex spectrum containing count bins.
 * @param amplitudes Output array with room for count amplitudes.
 * @param count Number of bins; must equal LND_FftGetBinCount(fft).
 * @param dbfs True for dBFS output; false for linear amplitudes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_FftSpectrum(const LND_FFT *fft, const LND_COMPLEX *bins, float *amplitudes, size_t count, bool dbfs);

#ifdef __cplusplus
}
#endif
