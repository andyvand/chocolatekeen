/*
 * snes_sound_host.c — converts Keen PC-speaker beep tables to the SNES
 * SPC700 format described in snes/src/data_format.h.
 *
 * Source data (see src/engine/engine_audio_gba.c:174-204):
 *   Episode 1: SOUNDS.CK1 file. Episodes 2/3: embedded in the unlzexe'd
 *   EXE at CVort2_SOUNDS_OFFSET=0x12730 / CVort3_SOUNDS_OFFSET=0x13A70
 *   (exeImage-relative; this tool reads outdir/exe_image.bin, which is
 *   already MZ-stripped).
 *   Layout: u16 sound count at +8; per-sound header entry at 16*(i+1):
 *   u16 beep-list byte offset (from blob start), u8 priority. A beep list
 *   is a run of u16 PIT period values, one per 6.866 ms tick (8192 PIT
 *   counts): 0 = rest tick, 0xFFFF = end of sound.
 *
 * Output ck_sounds_bin (uploaded verbatim to SPC RAM):
 *   u16 count; count * { u16 tickStreamOfs (from blob start), u8 priority,
 *   u8 pad }; then the converted tick streams. Each tick word:
 *     0x0000 = rest, 0xFFFF = end,
 *     else bits13..0 = DSP VxPITCH, bit14 = use the fine (8-samples-per-
 *     cycle) square sample, clear = coarse (32-sample-loop) sample.
 *
 * Pitch conversion (documented derivation):
 *   Square frequency from a PIT period:  f = 1193182 / period  [Hz]
 *   The DSP plays BRR samples at rate = 32000 * PITCH / 4096 samples/s.
 *   ck_brr_square32 loops 32 samples containing ONE square cycle
 *   (16 high + 16 low), so tone = rate / 32:
 *       PITCH_coarse = f * 32 * 4096 / 32000  = f * 4.096
 *       max f at PITCH 0x3FFF: ~3999.9 Hz; resolution ~0.24 Hz.
 *   ck_brr_square8 is one 16-sample loop block containing TWO cycles of
 *   an 8-sample square (4 high + 4 low each), so tone = rate * 2 / 16
 *   = rate / 8:
 *       PITCH_fine = f * 16 * 4096 / 32000 / 2 = f * 8 * 4096 / 32000
 *                  = f * 1.024
 *       max f at PITCH 0x3FFF: ~15999 Hz; resolution ~0.98 Hz.
 *   Selection: use coarse while PITCH_coarse <= 0x3FFF (best resolution
 *   at low frequencies), else fine with bit14 set; clamp to 0x3FFF with
 *   a warning above ~16 kHz (Keen data reaches 19.9 kHz on a couple of
 *   ticks; those land at 16 kHz, still effectively inaudible chirps).
 *
 * BRR samples (filter 0 is exact for any waveform, 1 block = header +
 * 8 data bytes = 16 4-bit samples, two per byte, high nibble first):
 *   ck_brr_square32: 2 blocks (32 samples): 16 x +7 then 16 x -7,
 *                    shift 12, loop flag on both, end flag on the last.
 *   ck_brr_square8:  1 block: (4 x +7, 4 x -7) twice, shift 12, loop+end.
 *   Decoded amplitude: (7 << 12) >> 1 = 14336 (~44% full scale, no
 *   clipping headroom issues with the Gaussian interpolator).
 *
 * Usage: snes_sound_host <staged_dir> <ext> <episode 1|2|3> <outdir>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PC_PIT_RATE 1193182.0
#define DSP_RATE    32000.0
#define PITCH_MAX   0x3FFF

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    if (sz > 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return NULL;
    }
    fclose(fp);
    *size_out = (size_t)sz;
    return buf;
}

static int write_file(const char *path, const void *buf, size_t size) {
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror(path); return 1; }
    if (size > 0 && fwrite(buf, 1, size, fp) != size) {
        perror(path); fclose(fp); return 1;
    }
    fclose(fp);
    return 0;
}

/* Per-episode EXE sound-table offsets (src/episodes/episode{2,3}.h). */
static const uint32_t kSoundsOffset[3] = { 0, 0x12730, 0x13A70 };

/* Convert one PIT period tick to the output tick word. */
static uint16_t convert_tick(uint16_t period, int *clamped) {
    if (period == 0) return 0x0000;
    double f = PC_PIT_RATE / (double)period;
    /* Coarse sample: PITCH = f * 32 * 4096 / 32000. */
    double pc = f * 32.0 * 4096.0 / DSP_RATE;
    long pitch = (long)(pc + 0.5);
    if (pitch >= 1 && pitch <= PITCH_MAX)
        return (uint16_t)pitch;
    /* Fine sample: two 8-sample square cycles per 16-sample loop:
     * PITCH = f * 16 * 4096 / 32000 / 2. */
    double pf = f * 16.0 * 4096.0 / DSP_RATE / 2.0;
    pitch = (long)(pf + 0.5);
    if (pitch < 1) pitch = 1;
    if (pitch > PITCH_MAX) {
        (*clamped)++;
        pitch = PITCH_MAX;
    }
    return (uint16_t)(0x4000 | pitch);
}

static int emit_brr(const char *outdir) {
    /* BRR block header: (shift << 4) | (filter << 2) | (loop << 1) | end. */
    const uint8_t HDR_MID  = (12 << 4) | (0 << 2) | 0x02;        /* loop     */
    const uint8_t HDR_LAST = (12 << 4) | (0 << 2) | 0x02 | 0x01; /* loop+end */
    const uint8_t HI = 0x7, LO = 0x9;  /* +7 / -7 as signed nibbles */

    uint8_t sq32[18];
    sq32[0] = HDR_MID;
    memset(sq32 + 1, (HI << 4) | HI, 8);   /* 16 samples of +7 */
    sq32[9] = HDR_LAST;
    memset(sq32 + 10, (LO << 4) | LO, 8);  /* 16 samples of -7 */

    uint8_t sq8[9];
    sq8[0] = HDR_LAST;
    for (int rep = 0; rep < 2; rep++) {
        sq8[1 + rep * 4 + 0] = (HI << 4) | HI;   /* 4 x +7 */
        sq8[1 + rep * 4 + 1] = (HI << 4) | HI;
        sq8[1 + rep * 4 + 2] = (LO << 4) | LO;   /* 4 x -7 */
        sq8[1 + rep * 4 + 3] = (LO << 4) | LO;
    }

    char path[1024];
    snprintf(path, sizeof path, "%s/brr_square32.bin", outdir);
    if (write_file(path, sq32, sizeof sq32)) return 1;
    snprintf(path, sizeof path, "%s/brr_square8.bin", outdir);
    if (write_file(path, sq8, sizeof sq8)) return 1;
    fprintf(stderr, "snes_sound_host: BRR squares emitted (18 + 9 bytes)\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <staged_dir> <ext> <episode> <outdir>\n",
                argv[0]);
        return 2;
    }
    const char *staged = argv[1];
    const char *ext = argv[2];
    int episode = atoi(argv[3]);
    const char *outdir = argv[4];
    if (episode < 1 || episode > 3) {
        fprintf(stderr, "snes_sound_host: bad episode %s\n", argv[3]);
        return 2;
    }

    /* Locate the source sound blob. */
    uint8_t *owned = NULL;
    const uint8_t *blob = NULL;
    size_t blobSize = 0;
    if (episode == 1) {
        char path[1024];
        snprintf(path, sizeof path, "%s/SOUNDS.%s", staged, ext);
        owned = read_file(path, &blobSize);
        if (!owned) {
            fprintf(stderr, "snes_sound_host: cannot read %s\n", path);
            return 1;
        }
        blob = owned;
    } else {
        char path[1024];
        snprintf(path, sizeof path, "%s/exe_image.bin", outdir);
        size_t exeSize = 0;
        owned = read_file(path, &exeSize);
        if (!owned) {
            fprintf(stderr, "snes_sound_host: cannot read %s (run "
                            "snes_preprocess_misc_host first)\n", path);
            return 1;
        }
        uint32_t off = kSoundsOffset[episode - 1];
        if (off >= exeSize) {
            fprintf(stderr, "snes_sound_host: sound table offset out of "
                            "bounds\n");
            return 1;
        }
        blob = owned + off;
        /* Same bound the GBA runtime uses: the table plus every beep list
         * stays well inside 64 KiB after the offset. */
        blobSize = exeSize - off;
        if (blobSize > 0x10000) blobSize = 0x10000;
    }
    if (blobSize < 16) {
        fprintf(stderr, "snes_sound_host: sound blob truncated\n");
        return 1;
    }

    uint16_t count = le16(blob + 8);
    if (count == 0 || count > 128) {
        fprintf(stderr, "snes_sound_host: implausible sound count %u\n",
                (unsigned)count);
        return 1;
    }

    /* Pass 1: header size; streams appended after it. */
    size_t hdrSize = 2 + (size_t)count * 4;
    uint8_t *out = (uint8_t *)malloc(0x20000);
    if (!out) { fprintf(stderr, "oom\n"); return 1; }
    memset(out, 0, hdrSize);
    out[0] = (uint8_t)(count & 0xFF);
    out[1] = (uint8_t)(count >> 8);
    size_t o = hdrSize;
    int clamped = 0;
    size_t totalTicks = 0;

    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *entry = blob + 16u * ((size_t)i + 1);
        if ((size_t)(entry + 3 - blob) > blobSize) {
            fprintf(stderr, "snes_sound_host: header entry %u out of bounds\n",
                    (unsigned)i);
            free(out);
            return 1;
        }
        uint16_t srcOfs = le16(entry);
        uint8_t priority = entry[2];

        uint8_t *dirEnt = out + 2 + (size_t)i * 4;
        dirEnt[0] = (uint8_t)(o & 0xFF);
        dirEnt[1] = (uint8_t)(o >> 8);
        dirEnt[2] = priority;
        dirEnt[3] = 0;

        if (srcOfs >= blobSize) {
            fprintf(stderr, "snes_sound_host: WARNING sound %u beep list "
                            "offset 0x%X out of bounds; emitting empty\n",
                    (unsigned)i, srcOfs);
            out[o++] = 0xFF; out[o++] = 0xFF;
            continue;
        }
        size_t s = srcOfs;
        for (;;) {
            if (s + 1 >= blobSize) {
                fprintf(stderr, "snes_sound_host: WARNING sound %u beep list "
                                "ran off the blob; terminating\n", (unsigned)i);
                out[o++] = 0xFF; out[o++] = 0xFF;
                break;
            }
            uint16_t period = le16(blob + s);
            s += 2;
            if (period == 0xFFFF) {
                out[o++] = 0xFF; out[o++] = 0xFF;
                break;
            }
            uint16_t word = convert_tick(period, &clamped);
            out[o++] = (uint8_t)(word & 0xFF);
            out[o++] = (uint8_t)(word >> 8);
            totalTicks++;
        }
        if (o > 0xFFF0) {
            fprintf(stderr, "snes_sound_host: output exceeds 64 KiB\n");
            free(out);
            return 1;
        }
    }
    free(owned);

    char path[1024];
    snprintf(path, sizeof path, "%s/sounds.bin", outdir);
    int rc = write_file(path, out, o);
    free(out);
    if (rc) return rc;
    if (clamped)
        fprintf(stderr, "snes_sound_host: WARNING %d ticks above ~16 kHz "
                        "clamped to PITCH 0x3FFF\n", clamped);
    fprintf(stderr, "snes_sound_host: %s (%u sounds, %zu ticks, %zu bytes)\n",
            path, (unsigned)count, totalTicks, o);
    return emit_brr(outdir);
}
