/*
 * snes_preprocess_misc_host.c — host-side text pre-bake for the SNES port.
 *
 * Text transforms are factored from scripts/gba_preprocess_misc_host.c
 * (which mirrors CVort_process_text_file in src/game/menus.c):
 *   0x1F -> 0x0D; each lone CR (not part of a CR..CR pair two bytes later)
 *   becomes a space with the tail shifted up; 0x1A terminates.
 *
 * Outputs (in <outdir>):
 *   text_story.bin / text_help.bin / text_end.bin / text_previews.bin
 *       processed text blobs, each ending with the 0x1A terminator
 *       (missing/unavailable texts become a lone 0x1A byte). Episode 1
 *       sources the STORYTXT/HELPTEXT/ENDTEXT/PREVIEWS files; episodes
 *       2/3 extract from the EXE-embedded regions (kExeTextTables, same
 *       offsets as the GBA bake / src/episodes/episode{2,3}.h).
 *       Per the data contract, ck_text_previews is ep1-only; ep2/3
 *       gameplay reads previews straight from ck_exe_image.
 *   exe_image.bin
 *       the staged (already unlzexe'd) KEEN<n>.EXE with the MZ header
 *       stripped and (ep2/3) all four embedded text regions transformed
 *       in place — the exact bytes ck_exe_image binds to at runtime.
 *
 * Levels are intentionally NOT touched: the SNES runtime CRLE-expands the
 * original LEVEL??.CK<n> payloads itself (they are mutated in WRAM).
 *
 * Usage: snes_preprocess_misc_host <staged_dir> <ext> <episode 1|2|3> <outdir>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

/* Mirror of CVort_process_text_file (src/game/menus.c), identical to the
 * GBA host tool's copy. Operates in place; buffer must hold a 0x1A. */
static void process_text_file(uint8_t *buffer, size_t len) {
    uint16_t numOfChars = 0, charCounter = 0;
    while (numOfChars < len && buffer[numOfChars] != 0x1A) numOfChars++;
    if (numOfChars >= len) return;

    while (buffer[charCounter] != 0x1A) {
        if (buffer[charCounter] == 0x1F) {
            buffer[charCounter] = 0x0D;
            charCounter++;
        } else if (buffer[charCounter] == 0x0D) {
            if (buffer[charCounter + 2] == 0x0D) {
                do {
                    charCounter += 2;
                } while (buffer[charCounter] == 0x0D);
            } else {
                buffer[charCounter] = 0x20;
                memmove(buffer + charCounter + 1, buffer + charCounter + 2,
                        (size_t)(numOfChars - charCounter - 1));
                numOfChars--;
                charCounter++;
            }
        } else {
            charCounter++;
        }
    }
}

/* Write a processed text blob truncated just past its 0x1A terminator. */
static int emit_text_blob(const char *outdir, const char *name,
                          const uint8_t *buf, size_t len) {
    char path[1024];
    snprintf(path, sizeof path, "%s/text_%s.bin", outdir, name);
    size_t n = 0;
    while (n < len && buf[n] != 0x1A) n++;
    if (n < len) n++;             /* include the terminator */
    if (n == 0 || buf[n - 1] != 0x1A) {
        /* No terminator found — emit one so the runtime always stops. */
        uint8_t *tmp = (uint8_t *)malloc(n + 1);
        if (!tmp) { fprintf(stderr, "oom\n"); return 1; }
        memcpy(tmp, buf, n);
        tmp[n] = 0x1A;
        int rc = write_file(path, tmp, n + 1);
        free(tmp);
        if (rc == 0)
            fprintf(stderr, "snes_preprocess_misc_host: %s (%zu bytes, "
                            "terminator appended)\n", path, n + 1);
        return rc;
    }
    int rc = write_file(path, buf, n);
    if (rc == 0)
        fprintf(stderr, "snes_preprocess_misc_host: %s (%zu bytes)\n", path, n);
    return rc;
}

static int emit_empty_text(const char *outdir, const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/text_%s.bin", outdir, name);
    uint8_t term = 0x1A;
    int rc = write_file(path, &term, 1);
    if (rc == 0)
        fprintf(stderr, "snes_preprocess_misc_host: %s (empty)\n", path);
    return rc;
}

/* Episode-1 external text file, processed. Missing files become empty. */
static int emit_text_from_file(const char *staged, const char *fname,
                               const char *outdir, const char *name) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", staged, fname);
    size_t sz = 0;
    uint8_t *buf = read_file(path, &sz);
    if (!buf) return emit_empty_text(outdir, name);
    process_text_file(buf, sz);
    int rc = emit_text_blob(outdir, name, buf, sz);
    free(buf);
    return rc;
}

/* EXE-embedded text regions, exeImage-relative offsets — same values as
 * scripts/gba_preprocess_misc_host.c kExeTextTables and
 * src/episodes/episode{2,3}.h. Order: HELP, STORY, END, PREVIEWS. */
static const size_t kExeTextOffsets[2][4] = {
    { 0x15BC0, 0x16AC0, 0x15840, 0x163A0 },  /* KEEN2 */
    { 0x179D0, 0x18BD0, 0x181A0, 0x184E0 },  /* KEEN3 */
};

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
        fprintf(stderr, "snes_preprocess_misc_host: bad episode %s\n", argv[3]);
        return 2;
    }

    /* Load the staged unlzexe'd EXE. */
    char exe_path[1024];
    snprintf(exe_path, sizeof exe_path, "%s/KEEN%d.EXE", staged, episode);
    size_t exe_sz = 0;
    uint8_t *exe = read_file(exe_path, &exe_sz);
    if (!exe || exe_sz < 64 || exe[0] != 'M' || exe[1] != 'Z') {
        fprintf(stderr, "snes_preprocess_misc_host: cannot read MZ exe %s\n",
                exe_path);
        return 1;
    }
    size_t hdr = 16u * ((size_t)exe[8] | ((size_t)exe[9] << 8));
    if (hdr >= exe_sz) {
        fprintf(stderr, "%s: MZ header size %zu >= file size %zu\n",
                exe_path, hdr, exe_sz);
        return 1;
    }
    uint8_t *img = exe + hdr;
    size_t img_sz = exe_sz - hdr;

    int rc = 0;
    if (episode == 1) {
        char fname[32];
        snprintf(fname, sizeof fname, "STORYTXT.%s", ext);
        rc |= emit_text_from_file(staged, fname, outdir, "story");
        snprintf(fname, sizeof fname, "HELPTEXT.%s", ext);
        rc |= emit_text_from_file(staged, fname, outdir, "help");
        snprintf(fname, sizeof fname, "ENDTEXT.%s", ext);
        rc |= emit_text_from_file(staged, fname, outdir, "end");
        snprintf(fname, sizeof fname, "PREVIEWS.%s", ext);
        rc |= emit_text_from_file(staged, fname, outdir, "previews");
    } else {
        const size_t *offs = kExeTextOffsets[episode - 2];
        /* Transform all four embedded regions in place so ck_exe_image
         * carries pre-processed text (runtime points straight into ROM). */
        for (int i = 0; i < 4; i++) {
            if (offs[i] >= img_sz) {
                fprintf(stderr, "snes_preprocess_misc_host: text region %d "
                                "(0x%zX) out of bounds\n", i, offs[i]);
                return 1;
            }
            process_text_file(img + offs[i], img_sz - offs[i]);
        }
        rc |= emit_text_blob(outdir, "help", img + offs[0], img_sz - offs[0]);
        rc |= emit_text_blob(outdir, "story", img + offs[1], img_sz - offs[1]);
        rc |= emit_text_blob(outdir, "end", img + offs[2], img_sz - offs[2]);
        /* ck_text_previews is ep1-only per the data contract; ep2/3 read
         * previews from ck_exe_image at CVort{2,3}_PREVIEWS_TEXT_OFFSET. */
        rc |= emit_empty_text(outdir, "previews");
        /* Write the transformed EXE back so any later stage (and manual
         * inspection) sees the same bytes the ROM will carry. */
        if (write_file(exe_path, exe, exe_sz)) rc = 1;
    }

    /* MZ-stripped image blob for ck_exe_image. */
    {
        char path[1024];
        snprintf(path, sizeof path, "%s/exe_image.bin", outdir);
        if (write_file(path, img, img_sz)) rc = 1;
        else
            fprintf(stderr, "snes_preprocess_misc_host: %s (%zu bytes, "
                            "MZ header %zu stripped)\n", path, img_sz, hdr);
    }
    free(exe);
    return rc;
}
