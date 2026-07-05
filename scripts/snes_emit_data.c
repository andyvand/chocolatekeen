/*
 * snes_emit_data.c — final stage of the SNES bake. Takes the staged blobs
 * and C fragments produced by snes_{gfx,level,preprocess_misc,sound}_host
 * in <outdir> and writes:
 *
 *   data_ep<N>.asm    one `.SECTION "ck_<name>" SUPERFREE` + label +
 *                     `.incbin` per blob. Every superfree blob is asserted
 *                     <= 64 KiB (one HiROM bank). Two blobs legitimately
 *                     exceed 64 KiB and get explicit consecutive banks so
 *                     `label + offset` stays linear in HiROM:
 *                       ck_exe_image  at bank CK_EXE_START_BANK (8), i.e.
 *                                     $C80000 with hdr.asm's .BASE $C0
 *                       ck_tiles_chr  immediately after the EXE banks
 *                     (the global tile bank is 611..689 tiles x 128 B =
 *                     76..87 KiB, so it can never fit one bank; this is a
 *                     documented deviation from the "only ck_exe_image may
 *                     exceed 64 KiB" rule).
 *                     The asm `.include`s hdr.asm; the bake script copies
 *                     snes/hdr.asm into <outdir> and this tool emits
 *                     absolute paths for both the include and the incbins,
 *                     because wla-65816 resolves relative paths against
 *                     the CWD only (verified with v10.7a).
 *
 *   snes_data_gen.h   extern declarations for generated symbols that
 *                     data_format.h doesn't already declare.
 *
 *   snes_data_gen.c   typed directory tables per data_format.h: palette
 *                     literals (from palettes.bin), ck_levels[], the
 *                     bitmap/sprite/screen fragments, and the length
 *                     constants. Huge data stays in the .incbin blobs.
 *
 * Usage: snes_emit_data <outdir> <episode 1|2|3>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define BANK_SIZE      65536L
#define ROM_BANKS      16          /* keep in sync with snes/hdr.asm */
#define CK_EXE_START_BANK 8

static long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (long)st.st_size;
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

static char g_outAbs[PATH_MAX];

/* Emit one superfree blob section; returns 1 on missing/oversize. */
static int emit_superfree(FILE *asmf, const char *fname, const char *label,
                          int optional) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof path, "%s/%s", g_outAbs, fname);
    long sz = file_size(path);
    if (sz < 0) {
        if (optional) return 0;
        fprintf(stderr, "snes_emit_data: missing blob %s\n", path);
        return 1;
    }
    if (sz > BANK_SIZE) {
        fprintf(stderr, "snes_emit_data: ASSERT FAILED: %s is %ld bytes "
                        "(> 64 KiB superfree section)\n", fname, sz);
        return 1;
    }
    fprintf(asmf, ".SECTION \"%s\" SUPERFREE\n%s:\n.incbin \"%s\"\n.ENDS\n\n",
            label, label, path);
    return 0;
}

/* Emit a >64 KiB blob across explicit consecutive banks starting at
 * *bank; label goes at the start of the first bank. */
static int emit_multibank(FILE *asmf, const char *fname, const char *label,
                          int *bank) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof path, "%s/%s", g_outAbs, fname);
    long sz = file_size(path);
    if (sz < 0) {
        fprintf(stderr, "snes_emit_data: missing blob %s\n", path);
        return 1;
    }
    long remaining = sz;
    long skip = 0;
    int chunk = 0;
    fprintf(asmf, "; %s: %ld bytes across banks %d..%d (linear in HiROM: "
                  "$%02X0000+)\n",
            label, sz, *bank, (int)(*bank + (sz + BANK_SIZE - 1) / BANK_SIZE - 1),
            0xC0 + *bank);
    while (remaining > 0) {
        long take = remaining > BANK_SIZE ? BANK_SIZE : remaining;
        if (*bank >= ROM_BANKS) {
            fprintf(stderr, "snes_emit_data: ASSERT FAILED: %s needs bank %d "
                            "but hdr.asm has .ROMBANKS %d\n",
                    label, *bank, ROM_BANKS);
            return 1;
        }
        fprintf(asmf, ".BANK %d SLOT 0\n.ORG 0\n.SECTION \"%s_b%d\" FORCE\n",
                *bank, label, chunk);
        if (chunk == 0)
            fprintf(asmf, "%s:\n", label);
        fprintf(asmf, ".incbin \"%s\" SKIP %ld READ %ld\n.ENDS\n\n",
                path, skip, take);
        remaining -= take;
        skip += take;
        (*bank)++;
        chunk++;
    }
    return 0;
}

/* Copy a fragment file into the generated C file. */
static int paste_fragment(FILE *cf, const char *fname, int optional) {
    char path[PATH_MAX + 64];
    snprintf(path, sizeof path, "%s/%s", g_outAbs, fname);
    size_t sz = 0;
    uint8_t *buf = read_file(path, &sz);
    if (!buf) {
        if (optional) return 0;
        fprintf(stderr, "snes_emit_data: missing fragment %s\n", path);
        return 1;
    }
    fprintf(cf, "\n/* ---- %s ---- */\n", fname);
    fwrite(buf, 1, sz, cf);
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <outdir> <episode>\n", argv[0]);
        return 2;
    }
    int episode = atoi(argv[2]);
    if (episode < 1 || episode > 3) {
        fprintf(stderr, "snes_emit_data: bad episode %s\n", argv[2]);
        return 2;
    }
    if (!realpath(argv[1], g_outAbs)) {
        perror(argv[1]);
        return 1;
    }

    char path[PATH_MAX + 64];

    /* ---------------- data_ep<N>.asm ---------------- */
    snprintf(path, sizeof path, "%s/data_ep%d.asm", g_outAbs, episode);
    FILE *asmf = fopen(path, "w");
    if (!asmf) { perror(path); return 1; }
    fprintf(asmf,
        "; data_ep%d.asm - auto-generated by scripts/snes_emit_data.c.\n"
        "; Do not edit. One superfree section per blob (<= 64 KiB each);\n"
        "; ck_exe_image and ck_tiles_chr are placed in explicit consecutive\n"
        "; banks starting at bank %d so 24-bit pointer arithmetic is linear.\n"
        ".include \"%s/hdr.asm\"\n\n",
        episode, CK_EXE_START_BANK, g_outAbs);

    int rc = 0;
    rc |= emit_superfree(asmf, "font.chr", "ck_font_chr", 0);
    for (int c = 0; c < 4; c++) {
        char f[32], l[32];
        snprintf(f, sizeof f, "sprites%d.chr", c);
        snprintf(l, sizeof l, "ck_sprites_chr%d", c);
        rc |= emit_superfree(asmf, f, l, c > 0);
    }
    int bmpCount = 0;
    for (int i = 0; i < 64; i++) {
        char f[32], l[32];
        snprintf(f, sizeof f, "bmp_%02d.chr", i);
        snprintf(path, sizeof path, "%s/%s", g_outAbs, f);
        if (file_size(path) < 0) break;
        snprintf(l, sizeof l, "ck_bmp_%02d", i);
        rc |= emit_superfree(asmf, f, l, 0);
        bmpCount++;
    }
    static const char *screenNames[4] = {
        "title", "finale", "preview2", "preview3"
    };
    int haveScreen[4] = { 0, 0, 0, 0 };
    for (int s = 0; s < 4; s++) {
        char f[48], l[48];
        snprintf(f, sizeof f, "screen_%s.chr", screenNames[s]);
        snprintf(path, sizeof path, "%s/%s", g_outAbs, f);
        if (file_size(path) < 0) continue;
        haveScreen[s] = 1;
        snprintf(l, sizeof l, "ck_screen_%s_chr", screenNames[s]);
        rc |= emit_superfree(asmf, f, l, 0);
        snprintf(f, sizeof f, "screen_%s.map", screenNames[s]);
        snprintf(l, sizeof l, "ck_screen_%s_map", screenNames[s]);
        rc |= emit_superfree(asmf, f, l, 0);
    }
    rc |= emit_superfree(asmf, "tileinfo.bin", "ck_tileinfo", 0);
    rc |= emit_superfree(asmf, "text_story.bin", "ck_text_story", 0);
    rc |= emit_superfree(asmf, "text_help.bin", "ck_text_help", 0);
    rc |= emit_superfree(asmf, "text_end.bin", "ck_text_end", 0);
    rc |= emit_superfree(asmf, "text_previews.bin", "ck_text_previews", 0);
    rc |= emit_superfree(asmf, "sounds.bin", "ck_sounds_bin", 0);
    rc |= emit_superfree(asmf, "brr_square32.bin", "ck_brr_square32", 0);
    rc |= emit_superfree(asmf, "brr_square8.bin", "ck_brr_square8", 0);

    int levels[128], nLevels = 0;
    for (int i = 0; i < 100; i++) {
        char f[32], l[32];
        snprintf(f, sizeof f, "lev%02d.bin", i);
        snprintf(path, sizeof path, "%s/%s", g_outAbs, f);
        if (file_size(path) < 0) continue;
        levels[nLevels++] = i;
        snprintf(l, sizeof l, "ck_lev%02d_data", i);
        rc |= emit_superfree(asmf, f, l, 0);
        snprintf(f, sizeof f, "lev%02d.tset", i);
        snprintf(l, sizeof l, "ck_lev%02d_tset", i);
        rc |= emit_superfree(asmf, f, l, 0);
    }
    if (nLevels == 0) {
        fprintf(stderr, "snes_emit_data: no lev*.bin blobs found\n");
        rc = 1;
    }

    /* Multi-bank blobs at fixed consecutive banks. */
    int bank = CK_EXE_START_BANK;
    rc |= emit_multibank(asmf, "exe_image.bin", "ck_exe_image", &bank);
    rc |= emit_multibank(asmf, "tiles.chr", "ck_tiles_chr", &bank);
    fclose(asmf);
    if (rc) return 1;

    long exeLen = 0, soundsLen = 0;
    snprintf(path, sizeof path, "%s/exe_image.bin", g_outAbs);
    exeLen = file_size(path);
    snprintf(path, sizeof path, "%s/sounds.bin", g_outAbs);
    soundsLen = file_size(path);
    if (exeLen < 0 || soundsLen < 0) {
        fprintf(stderr, "snes_emit_data: exe_image.bin/sounds.bin missing\n");
        return 1;
    }

    /* ---------------- snes_data_gen.h ---------------- */
    snprintf(path, sizeof path, "%s/snes_data_gen.h", g_outAbs);
    FILE *hf = fopen(path, "w");
    if (!hf) { perror(path); return 1; }
    fprintf(hf,
        "/* snes_data_gen.h - auto-generated by scripts/snes_emit_data.c.\n"
        " * Externs for generated symbols not already declared by\n"
        " * snes/src/data_format.h. */\n"
        "#ifndef CK_SNES_DATA_GEN_H\n"
        "#define CK_SNES_DATA_GEN_H\n\n"
        "#include \"data_format.h\"\n\n"
        "/* Global 16x16 tile bank, 4bpp, 128 B per tile (TL,TR,BL,BR "
        "chars).\n * Spans consecutive HiROM banks; index with a 32-bit "
        "offset. */\n"
        "extern const u8 ck_tiles_chr[];\n\n"
        "/* 6 x TILENUM int16 TILEINFO arrays copied from the EXE\n"
        " * (Anim, Type, UEdge, REdge, DEdge, LEdge). */\n"
        "extern const u8 ck_tileinfo[];\n\n");
    for (int s = 1; s < 4; s++) {  /* title is declared in data_format.h */
        if (haveScreen[s])
            fprintf(hf, "extern const CkFullScreen ck_screen_%s;\n",
                    screenNames[s]);
    }
    fprintf(hf, "\n#endif /* CK_SNES_DATA_GEN_H */\n");
    fclose(hf);

    /* ---------------- snes_data_gen.c ---------------- */
    snprintf(path, sizeof path, "%s/snes_data_gen.c", g_outAbs);
    FILE *cf = fopen(path, "w");
    if (!cf) { perror(path); return 1; }
    fprintf(cf,
        "/* snes_data_gen.c - auto-generated by scripts/snes_emit_data.c "
        "(episode %d).\n"
        " * Typed directory tables per snes/src/data_format.h; the bulk\n"
        " * data lives in the .incbin blobs of data_ep%d.asm. */\n\n"
        "#include \"data_format.h\"\n"
        "#include \"snes_data_gen.h\"\n",
        episode, episode);

    /* Palettes (from palettes.bin: 16 EGA + 4*17 flash + 8*16 OBJ u16 LE). */
    {
        snprintf(path, sizeof path, "%s/palettes.bin", g_outAbs);
        size_t sz = 0;
        uint8_t *pal = read_file(path, &sz);
        if (!pal || sz != (16 + 4 * 17 + 8 * 16) * 2) {
            fprintf(stderr, "snes_emit_data: bad palettes.bin (%zu bytes)\n", sz);
            return 1;
        }
        const uint8_t *p = pal;
        fprintf(cf, "\n/* EGA identity palette (BG palette 0), bgr555. */\n");
        fprintf(cf, "const u16 ck_pal_ega[16] = {");
        for (int i = 0; i < 16; i++, p += 2)
            fprintf(cf, "%s0x%04X,", (i % 8) ? " " : "\n    ",
                    p[0] | (p[1] << 8));
        fprintf(cf, "\n};\n");
        fprintf(cf, "\n/* EXE flash palettes + border color. */\n");
        fprintf(cf, "const u16 ck_pal_flash[4][17] = {\n");
        for (int j = 0; j < 4; j++) {
            fprintf(cf, "    {");
            for (int i = 0; i < 17; i++, p += 2)
                fprintf(cf, " 0x%04X,", p[0] | (p[1] << 8));
            fprintf(cf, " },\n");
        }
        fprintf(cf, "};\n");
        fprintf(cf, "\n/* Bake-binned OBJ palettes; entry 0 unused. */\n");
        fprintf(cf, "const u16 ck_obj_pals[8][16] = {\n");
        for (int j = 0; j < 8; j++) {
            fprintf(cf, "    {");
            for (int i = 0; i < 16; i++, p += 2)
                fprintf(cf, " 0x%04X,", p[0] | (p[1] << 8));
            fprintf(cf, " },\n");
        }
        fprintf(cf, "};\n");
        free(pal);
    }

    fprintf(cf, "\nconst u32 ck_exe_image_len = %ldu;\n", exeLen);
    fprintf(cf, "const u16 ck_sounds_bin_len = %ldu;\n", soundsLen);

    rc |= paste_fragment(cf, "bitmaps.frag.c", 0);
    rc |= paste_fragment(cf, "sprites.frag.c", 0);
    rc |= paste_fragment(cf, "screens.frag.c", 0);

    /* Level directory. */
    fprintf(cf, "\n/* ---- level directory ---- */\n");
    for (int i = 0; i < nLevels; i++) {
        fprintf(cf, "extern const u8 ck_lev%02d_data[];\n", levels[i]);
        fprintf(cf, "extern const u8 ck_lev%02d_tset[];\n", levels[i]);
    }
    fprintf(cf, "const CkLevelEntry ck_levels[] = {\n");
    for (int i = 0; i < nLevels; i++) {
        char f[32];
        snprintf(f, sizeof f, "lev%02d.bin", levels[i]);
        snprintf(path, sizeof path, "%s/%s", g_outAbs, f);
        long sz = file_size(path);
        if (sz > 0xFFFF) {
            fprintf(stderr, "snes_emit_data: level %d compSize %ld > u16\n",
                    levels[i], sz);
            return 1;
        }
        fprintf(cf, "    { %d, 0, %ldu, ck_lev%02d_data, "
                    "(const u16 *)ck_lev%02d_tset },\n",
                levels[i], sz, levels[i], levels[i]);
    }
    fprintf(cf, "    { 0xFF, 0, 0, 0, 0 },\n};\n");
    fclose(cf);
    if (rc) return 1;

    fprintf(stderr, "snes_emit_data: wrote data_ep%d.asm + snes_data_gen.c/.h "
                    "(%d bitmaps, %d levels, exe %ld B in banks %d..%d, "
                    "tiles in banks up to %d)\n",
            episode, bmpCount, nLevels, exeLen, CK_EXE_START_BANK,
            CK_EXE_START_BANK + (int)((exeLen + BANK_SIZE - 1) / BANK_SIZE) - 1,
            bank - 1);
    return 0;
}
