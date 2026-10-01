#include <aacenc_lib.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define SECONDS 2
#define CHANNELS 2

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: aac_tone <out.aac> [aot] [bitrate]\n");
        return 1;
    }
    unsigned aot = argc > 2 ? (unsigned)atoi(argv[2]) : 5;
    unsigned bitrate_bps = argc > 3 ? (unsigned)atoi(argv[3]) : 64000;
    HANDLE_AACENCODER enc;
    if (aacEncOpen(&enc, 0, CHANNELS) != AACENC_OK) return 2;
    aacEncoder_SetParam(enc, AACENC_AOT, aot);
    aacEncoder_SetParam(enc, AACENC_SAMPLERATE, RATE);
    aacEncoder_SetParam(enc, AACENC_CHANNELMODE, MODE_2);
    aacEncoder_SetParam(enc, AACENC_BITRATE, bitrate_bps);
    aacEncoder_SetParam(enc, AACENC_TRANSMUX, TT_MP4_ADTS);
    aacEncoder_SetParam(enc, AACENC_AFTERBURNER, 1);
    if (aacEncEncode(enc, NULL, NULL, NULL, NULL) != AACENC_OK) return 3;
    AACENC_InfoStruct info;
    if (aacEncInfo(enc, &info) != AACENC_OK) return 4;
    FILE *out = fopen(argv[1], "wb");
    if (!out) return 5;
    size_t frame = info.frameLength;
    int16_t *pcm = malloc(frame * CHANNELS * sizeof *pcm);
    uint8_t packet[8192];
    uint64_t total = (uint64_t)RATE * SECONDS;
    uint64_t pos = 0;
    for (;;) {
        size_t n = pos < total ? (size_t)(total - pos < frame ? total - pos : frame) : 0;
        for (size_t i = 0; i < n; i++) {
            double t = (double)(pos + i) / RATE;
            pcm[i * 2] = (int16_t)lrint(0.5 * 32767.0 * sin(6.283185307179586 * 440.0 * t));
            pcm[i * 2 + 1] = (int16_t)lrint(0.5 * 32767.0 * sin(6.283185307179586 * 660.0 * t));
        }
        pos += n;
        void *in_ptr = pcm;
        INT in_id = IN_AUDIO_DATA, in_size = (INT)(n * CHANNELS * sizeof *pcm), in_el = sizeof *pcm;
        AACENC_BufDesc in_desc = {.numBufs = 1, .bufs = &in_ptr, .bufferIdentifiers = &in_id, .bufSizes = &in_size, .bufElSizes = &in_el};
        void *out_ptr = packet;
        INT out_id = OUT_BITSTREAM_DATA, out_size = sizeof packet, out_el = 1;
        AACENC_BufDesc out_desc = {.numBufs = 1, .bufs = &out_ptr, .bufferIdentifiers = &out_id, .bufSizes = &out_size, .bufElSizes = &out_el};
        AACENC_InArgs in_args = {.numInSamples = n ? (INT)(n * CHANNELS) : -1};
        AACENC_OutArgs out_args = {0};
        AACENC_ERROR err = aacEncEncode(enc, &in_desc, &out_desc, &in_args, &out_args);
        if (err == AACENC_ENCODE_EOF) break;
        if (err != AACENC_OK) return 6;
        if (out_args.numOutBytes) fwrite(packet, 1, (size_t)out_args.numOutBytes, out);
    }
    fclose(out);
    free(pcm);
    aacEncClose(&enc);
    return 0;
}
