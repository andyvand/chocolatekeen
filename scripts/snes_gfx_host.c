/*
 * snes_gfx_host.c — host-side EGA graphics converter for the SNES port.
 *
 * Reads the staged (unlzexe'd EXE, possibly LZ-compressed EGALATCH/EGASPRIT)
 * GAMEDATA tree and emits SNES-native graphics per snes/src/data_format.h:
 *
 *   tiles.chr           full global 16x16 tile bank, SNES 4bpp planar,
 *                       4 chars per tile in TL,TR,BL,BR order (128 B/tile).
 *   font.chr            256 glyphs, SNES 2bpp (16 B/glyph). Glyphs using
 *                       more than 3 non-zero colors are quantized + warned.
 *   bmp_NN.chr          per-bitmap 4bpp char strips (row-major chars).
 *   bitmaps.frag.c      ck_bitmaps[] directory fragment.
 *   sprites0.chr..      base sprite frames decomposed into 16x16/32x32 OAM
 *                       parts, split into <=64 KiB chunks at frame borders.
 *   sprites.frag.c      CkSpritePart/CkSpriteFrame tables fragment.
 *   screen_title.*      TITLE (EGALATCH bmp 0) as deduped chr + 64x32 map.
 *   screen_finale.*     FINALE.<ext> (Keen1-3 RLE) same shape.
 *   screen_preview2/3.* PREVIEW2/3.CK1 (episode 1 only).
 *   screens.frag.c      CkFullScreen definitions fragment.
 *   palettes.bin        16 EGA + 4x17 flash + 8x16 OBJ palettes, bgr555 LE.
 *   tileinfo.bin        6 x TILENUM int16 arrays copied from the EXE.
 *
 * EGA decode logic (EGAHEAD field offsets, 4/5-plane unpack loops, LZ
 * decompression) is adapted from scripts/gba_decomp_graphics_host.c and
 * src/render/gfx_gba.c; RLE full screens per src/decompression/imageRLE.c.
 *
 * Usage: snes_gfx_host <staged_dir> <ext> <episode 1|2|3> <outdir>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern char lz_decompress(FILE *lzfile, unsigned char *outbuffer);

/* ------------------------------------------------------------------ */
/* Small helpers shared with the other snes_*_host tools (kept local,  */
/* matching the existing scripts/ style).                              */
/* ------------------------------------------------------------------ */
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

/* Canonical EGA palette (src/render/gfx.c:1941). */
static const uint32_t kEgaRGB[16] = {
    0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
    0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
    0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
    0xff5555, 0xff55ff, 0xffff55, 0xffffff,
};

/* Same truncating conversion as rgb888_to_bgr555 in src/render/gfx_gba.c. */
static uint16_t bgr555(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(rgb & 0xFF);
    return (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

/* Per-episode EXE table offsets (src/episodes/episode{1,2,3}.h; relative
 * to the MZ-stripped exeImage origin). */
typedef struct {
    uint32_t tileinfoOffset;
    uint32_t tilenum;       /* TILEINFO entry count (ep3: 715 > EGAHEAD 624) */
    uint32_t palettesOffset;
} EpisodeExe_T;

static const EpisodeExe_T kEpisodeExe[3] = {
    { 0x130F8, 611, 0x15558 },  /* KEEN1 */
    { 0x17828, 689, 0x19BE8 },  /* KEEN2 */
    { 0x198C8, 715, 0x1BD84 },  /* KEEN3 */
};

/* ------------------------------------------------------------------ */
/* SNES char encoders                                                 */
/* ------------------------------------------------------------------ */

/* px: 64 pixel values (row-major 8x8), values 0..15 -> 32-byte 4bpp char.
 * SNES 4bpp planar: rows 0..7 as [plane0,plane1] byte pairs, then rows
 * 0..7 as [plane2,plane3]. Bit 7 = leftmost pixel. */
static void encode_char_4bpp(const uint8_t *px, uint8_t *out) {
    for (int row = 0; row < 8; row++) {
        uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        for (int x = 0; x < 8; x++) {
            uint8_t v = px[row * 8 + x] & 0x0F;
            uint8_t bit = (uint8_t)(1 << (7 - x));
            if (v & 1) b0 |= bit;
            if (v & 2) b1 |= bit;
            if (v & 4) b2 |= bit;
            if (v & 8) b3 |= bit;
        }
        out[row * 2]          = b0;
        out[row * 2 + 1]      = b1;
        out[16 + row * 2]     = b2;
        out[16 + row * 2 + 1] = b3;
    }
}

/* px: 64 pixel values 0..3 -> 16-byte 2bpp char. */
static void encode_char_2bpp(const uint8_t *px, uint8_t *out) {
    for (int row = 0; row < 8; row++) {
        uint8_t b0 = 0, b1 = 0;
        for (int x = 0; x < 8; x++) {
            uint8_t v = px[row * 8 + x] & 0x03;
            uint8_t bit = (uint8_t)(1 << (7 - x));
            if (v & 1) b0 |= bit;
            if (v & 2) b1 |= bit;
        }
        out[row * 2]     = b0;
        out[row * 2 + 1] = b1;
    }
}

/* Extract an 8x8 pixel block from a larger 8bpp image; out-of-bounds
 * pixels read as 0 (transparent / black). */
static void grab_block(const uint8_t *img, int imgW, int imgH,
                       int x0, int y0, uint8_t *out64) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int sx = x0 + x, sy = y0 + y;
            out64[y * 8 + x] = (sx >= 0 && sy >= 0 && sx < imgW && sy < imgH)
                             ? img[sy * imgW + sx] : 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* EGAHEAD state                                                      */
/* ------------------------------------------------------------------ */
static uint32_t g_latchPlaneSize, g_sprPlaneSize, g_imgDataStart, g_sprDataStart;
static uint16_t g_fontNum, g_tileNum, g_bmpNum, g_spriteNum;
static uint32_t g_fontLoc, g_tileLoc, g_bmpLoc, g_spriteLoc;

static uint8_t *g_latch;  static size_t g_latchSize;
static uint8_t *g_sprit;  static size_t g_spritSize;
static uint8_t *g_head;   static size_t g_headSize;
static uint8_t *g_exe;    static size_t g_exeSize;   /* MZ-stripped image */

static int g_warnings = 0;

/* Decode one 16x16 EGA tile from the 4-plane latch into 256 8bpp pixels.
 * Mirrors gba_decodeTile in src/render/gfx_gba.c:294. */
static void decode_tile(uint16_t num, uint8_t *dst) {
    const uint8_t *planeBase = g_latch + g_tileLoc + (uint32_t)num * 32;
    const uint8_t *p0 = planeBase;
    const uint8_t *p1 = planeBase + g_latchPlaneSize;
    const uint8_t *p2 = planeBase + 2 * g_latchPlaneSize;
    const uint8_t *p3 = planeBase + 3 * g_latchPlaneSize;
    for (int byteIdx = 0; byteIdx < 32; byteIdx++) {
        uint8_t b0 = p0[byteIdx], b1 = p1[byteIdx];
        uint8_t b2 = p2[byteIdx], b3 = p3[byteIdx];
        uint8_t *row = dst + byteIdx * 8;
        for (int bit = 7; bit >= 0; bit--) {
            row[7 - bit] = (uint8_t)(((b0 >> bit) & 1)
                                   | (((b1 >> bit) & 1) << 1)
                                   | (((b2 >> bit) & 1) << 2)
                                   | (((b3 >> bit) & 1) << 3));
        }
    }
}

/* Decode a 4-plane bitmap (wBytes*8 x h px) from the latch at bmpLoc+loc.
 * Mirrors CVort_engine_drawBitmap in src/render/gfx_gba.c:668. */
static void decode_bmp(uint32_t loc, int wBytes, int h, uint8_t *dst) {
    const uint8_t *p0 = g_latch + g_bmpLoc + loc;
    const uint8_t *p1 = p0 + g_latchPlaneSize;
    const uint8_t *p2 = p0 + 2 * g_latchPlaneSize;
    const uint8_t *p3 = p0 + 3 * g_latchPlaneSize;
    for (int yy = 0; yy < h; yy++) {
        for (int bx = 0; bx < wBytes; bx++) {
            uint8_t b0 = p0[yy * wBytes + bx];
            uint8_t b1 = p1[yy * wBytes + bx];
            uint8_t b2 = p2[yy * wBytes + bx];
            uint8_t b3 = p3[yy * wBytes + bx];
            for (int k = 0; k < 8; k++) {
                uint8_t mask = (uint8_t)(1 << (7 - k));
                dst[yy * wBytes * 8 + bx * 8 + k] =
                    (uint8_t)(((b0 & mask) ? 1 : 0)
                            | ((b1 & mask) ? 2 : 0)
                            | ((b2 & mask) ? 4 : 0)
                            | ((b3 & mask) ? 8 : 0));
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* tiles.chr                                                          */
/* ------------------------------------------------------------------ */
static int emit_tiles(const char *outdir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/tiles.chr", outdir);
    size_t outSize = (size_t)g_tileNum * 128;
    uint8_t *out = (uint8_t *)malloc(outSize);
    if (!out) { fprintf(stderr, "oom (tiles)\n"); return 1; }
    uint8_t px[256], blk[64];
    for (uint16_t t = 0; t < g_tileNum; t++) {
        decode_tile(t, px);
        /* 4 chars in TL,TR,BL,BR order. */
        static const int qx[4] = { 0, 8, 0, 8 };
        static const int qy[4] = { 0, 0, 8, 8 };
        for (int q = 0; q < 4; q++) {
            grab_block(px, 16, 16, qx[q], qy[q], blk);
            encode_char_4bpp(blk, out + (size_t)t * 128 + q * 32);
        }
    }
    int rc = write_file(path, out, outSize);
    free(out);
    if (rc == 0)
        fprintf(stderr, "snes_gfx_host: %s (%u tiles, %zu bytes)\n",
                path, (unsigned)g_tileNum, outSize);
    return rc;
}

/* ------------------------------------------------------------------ */
/* font.chr                                                           */
/* ------------------------------------------------------------------ */
static int emit_font(const char *outdir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/font.chr", outdir);
    uint8_t out[256 * 16];
    memset(out, 0, sizeof out);
    uint16_t nGlyphs = g_fontNum;
    if (nGlyphs > 256) {
        fprintf(stderr, "snes_gfx_host: WARNING fontNum %u > 256, truncating\n",
                (unsigned)nGlyphs);
        g_warnings++;
        nGlyphs = 256;
    }
    /* 4-plane glyph unpack, mirroring emit_fntunp in
     * scripts/gba_decomp_graphics_host.c:210. */
    const uint8_t *vanilla = g_latch + g_fontLoc;
    size_t fntBit = 0;
    int quantized = 0;
    for (uint32_t i = 0; i < nGlyphs; i++) {
        uint8_t glyph[64];
        for (uint32_t k = 0; k < 64; k++) {
            uint8_t v = 0;
            uint8_t mask = (uint8_t)(1 << (fntBit % 8 ^ 7));
            const uint8_t *bp = vanilla + fntBit / 8;
            if (bp[0] & mask) v |= 1;
            if (bp[g_latchPlaneSize] & mask) v |= 2;
            if (bp[2 * g_latchPlaneSize] & mask) v |= 4;
            if (bp[3 * g_latchPlaneSize] & mask) v |= 8;
            glyph[k] = v;
            fntBit++;
        }
        /* Map EGA colors -> 2bpp local indices: 0 stays 0; distinct
         * non-zero colors (ascending) become 1..3; overflow colors are
         * clamped to 3 with a warning (SNES 2bpp = 3 opaque colors). */
        uint8_t colorOf[16];
        memset(colorOf, 0, sizeof colorOf);
        uint8_t next = 1;
        int overflow = 0;
        for (int c = 1; c < 16; c++) {
            int used = 0;
            for (int k = 0; k < 64; k++) if (glyph[k] == c) { used = 1; break; }
            if (!used) continue;
            if (next <= 3) colorOf[c] = next++;
            else { colorOf[c] = 3; overflow = 1; }
        }
        if (overflow) { quantized++; }
        uint8_t px[64];
        for (int k = 0; k < 64; k++) px[k] = colorOf[glyph[k]];
        encode_char_2bpp(px, out + (size_t)i * 16);
    }
    if (quantized) {
        fprintf(stderr, "snes_gfx_host: WARNING %d font glyphs used >3 colors, "
                        "quantized to 2bpp\n", quantized);
        g_warnings++;
    }
    int rc = write_file(path, out, sizeof out);
    if (rc == 0)
        fprintf(stderr, "snes_gfx_host: %s (256 glyphs, %zu bytes)\n",
                path, sizeof out);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Bitmaps                                                            */
/* ------------------------------------------------------------------ */
static int emit_bitmaps(const char *outdir) {
    char path[1024];
    FILE *frag;
    snprintf(path, sizeof path, "%s/bitmaps.frag.c", outdir);
    frag = fopen(path, "w");
    if (!frag) { perror(path); return 1; }
    fprintf(frag,
        "/* ck_bitmaps[] fragment - generated by snes_gfx_host. 4bpp char\n"
        " * strips, row-major chars, %u entries indexed by Keen bmp id. */\n",
        (unsigned)g_bmpNum);
    for (uint16_t i = 0; i < g_bmpNum; i++)
        fprintf(frag, "extern const u8 ck_bmp_%02u[];\n", (unsigned)i);
    fprintf(frag, "const CkBitmap ck_bitmaps[] = {\n");

    const uint8_t *entries = g_head + g_imgDataStart;
    for (uint16_t i = 0; i < g_bmpNum; i++) {
        uint16_t wBytes = le16(entries + i * 16 + 0);
        uint16_t v      = le16(entries + i * 16 + 2);
        uint32_t loc    = le32(entries + i * 16 + 4);
        int wTiles = wBytes;
        int hTiles = (v + 7) / 8;
        uint8_t *img = (uint8_t *)calloc((size_t)wBytes * 8 * v + 1, 1);
        if (!img) { fclose(frag); fprintf(stderr, "oom (bmp)\n"); return 1; }
        decode_bmp(loc, wBytes, v, img);

        size_t chrLen = (size_t)wTiles * hTiles * 32;
        uint8_t *chr = (uint8_t *)malloc(chrLen ? chrLen : 1);
        if (!chr) { free(img); fclose(frag); fprintf(stderr, "oom\n"); return 1; }
        uint8_t blk[64];
        size_t o = 0;
        for (int ty = 0; ty < hTiles; ty++) {
            for (int tx = 0; tx < wTiles; tx++) {
                grab_block(img, wBytes * 8, v, tx * 8, ty * 8, blk);
                encode_char_4bpp(blk, chr + o);
                o += 32;
            }
        }
        free(img);
        snprintf(path, sizeof path, "%s/bmp_%02u.chr", outdir, (unsigned)i);
        int rc = write_file(path, chr, chrLen);
        free(chr);
        if (rc) { fclose(frag); return 1; }
        fprintf(frag, "    { ck_bmp_%02u, %d, %d },\n", (unsigned)i, wTiles, hTiles);
    }
    fprintf(frag, "};\nconst u16 ck_bitmap_count = %u;\n", (unsigned)g_bmpNum);
    fclose(frag);
    fprintf(stderr, "snes_gfx_host: %u bitmaps emitted\n", (unsigned)g_bmpNum);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sprites                                                            */
/* ------------------------------------------------------------------ */

/* OBJ palette binning state: up to 8 palettes of <=15 opaque EGA colors. */
static uint8_t g_objPal[8][15];
static int     g_objPalLen[8];
static int     g_objPalCount = 0;

static int pal_find_color(int p, uint8_t c) {
    for (int i = 0; i < g_objPalLen[p]; i++)
        if (g_objPal[p][i] == c) return i;
    return -1;
}

/* Assign a frame's opaque color set (nCol <= 15) to an OBJ palette,
 * greedily preferring the palette needing the fewest new colors (0 new =
 * full containment). A new palette is opened only when no existing one
 * can absorb the set. */
static int assign_palette(const uint8_t *cols, int nCol, int frameIdx) {
    int best = -1, bestNew = 999;
    for (int p = 0; p < g_objPalCount; p++) {
        int newc = 0;
        for (int i = 0; i < nCol; i++)
            if (pal_find_color(p, cols[i]) < 0) newc++;
        if (g_objPalLen[p] + newc <= 15 && newc < bestNew) {
            best = p; bestNew = newc;
        }
    }
    if (best < 0) {
        if (g_objPalCount >= 8) {
            fprintf(stderr, "snes_gfx_host: ERROR frame %d cannot fit any OBJ "
                            "palette (all 8 full)\n", frameIdx);
            exit(1);
        }
        best = g_objPalCount++;
        g_objPalLen[best] = 0;
    }
    for (int i = 0; i < nCol; i++) {
        if (pal_find_color(best, cols[i]) < 0)
            g_objPal[best][g_objPalLen[best]++] = cols[i];
    }
    return best;
}

/* Squared RGB distance between two EGA colors. */
static long ega_dist(uint8_t a, uint8_t b) {
    long dr = (long)((kEgaRGB[a] >> 16) & 0xFF) - (long)((kEgaRGB[b] >> 16) & 0xFF);
    long dg = (long)((kEgaRGB[a] >> 8) & 0xFF) - (long)((kEgaRGB[b] >> 8) & 0xFF);
    long db = (long)(kEgaRGB[a] & 0xFF) - (long)(kEgaRGB[b] & 0xFF);
    return dr * dr + dg * dg + db * db;
}

typedef struct {
    int wPx, hPx;
    int chunk;          /* sprites<chunk>.chr this frame lives in */
    uint32_t chrOfs;    /* byte offset inside that chunk */
    uint32_t chrLen;
    int palId;
    int nParts;
    int8_t partDx[16], partDy[16];
    uint8_t partLarge[16], partChrOfs[16];
} FrameInfo_T;

static int emit_sprites(const char *outdir) {
    const uint8_t *entries = g_head + g_sprDataStart;
    uint16_t nFrames = g_spriteNum;

    FrameInfo_T *frames = (FrameInfo_T *)calloc(nFrames ? nFrames : 1,
                                                sizeof(FrameInfo_T));
    if (!frames) { fprintf(stderr, "oom (frames)\n"); return 1; }

    /* Chunked chr pool: split at frame borders so every superfree asm
     * section stays under 64 KiB while `chunkLabel + ofs` stays in-bank. */
    enum { MAX_CHUNKS = 4, CHUNK_CAP = 65536 };
    uint8_t *chunks[MAX_CHUNKS];
    size_t chunkLen[MAX_CHUNKS];
    int nChunks = 1;
    chunks[0] = (uint8_t *)malloc(CHUNK_CAP);
    chunkLen[0] = 0;
    if (!chunks[0]) { free(frames); fprintf(stderr, "oom\n"); return 1; }

    int remapped16 = 0;

    for (uint16_t f = 0; f < nFrames; f++) {
        /* Base entry = every 4th MaskedSpriteEntry (shift copies dropped). */
        const uint8_t *e = entries + (size_t)f * 4 * 32;
        uint16_t wBytes     = le16(e + 0);
        uint16_t hRows      = le16(e + 2);
        uint16_t locOffset  = le16(e + 4);
        uint16_t location   = le16(e + 6);
        int wPx = wBytes * 8, hPx = hRows;
        size_t base = (size_t)g_spriteLoc + locOffset + 16u * location;
        size_t nPix = (size_t)wPx * hPx;
        size_t planeBytes = (nPix + 7) / 8;

        uint8_t *pix = (uint8_t *)calloc(nPix ? nPix : 1, 1);
        if (!pix) { fprintf(stderr, "oom (sprite pix)\n"); return 1; }

        if (base + planeBytes > g_sprPlaneSize) {
            fprintf(stderr, "snes_gfx_host: WARNING sprite frame %u overruns "
                            "plane, zero-filled\n", (unsigned)f);
            g_warnings++;
        } else {
            /* 5-plane unpack (bit4 = mask), as in gba_decomp_graphics_host
             * emit_sprunp / gfx_gba.c drawSprite. */
            size_t sprBit = 0;
            for (size_t p = 0; p < nPix; p++) {
                uint8_t v = 0;
                uint8_t mask = (uint8_t)(1 << (sprBit % 8 ^ 7));
                const uint8_t *bp = g_sprit + base + sprBit / 8;
                if (bp[0] & mask) v |= 1;
                if (bp[g_sprPlaneSize] & mask) v |= 2;
                if (bp[2 * g_sprPlaneSize] & mask) v |= 4;
                if (bp[3 * g_sprPlaneSize] & mask) v |= 8;
                if (bp[4 * g_sprPlaneSize] & mask) v |= 16;
                pix[p] = v;
                sprBit++;
            }
        }

        /* Blit rule (gfx_gba.c:637): mask=1,color=0 -> transparent;
         * mask=0 -> opaque color (including black). Collapse to:
         * transparent = 0xFF sentinel, else opaque EGA color. */
        long freq[16];
        memset(freq, 0, sizeof freq);
        for (size_t p = 0; p < nPix; p++) {
            uint8_t v = pix[p];
            if ((v & 0x10) && (v & 0x0F) == 0) { pix[p] = 0xFF; continue; }
            /* Exotic mask=1/color!=0 pixels don't occur in Keen 1-3 data
             * (verified plan fact); treat them as opaque color. */
            pix[p] = (uint8_t)(v & 0x0F);
            freq[pix[p]]++;
        }

        /* Distinct opaque color list. */
        uint8_t cols[16];
        int nCol = 0;
        for (int c = 0; c < 16; c++) if (freq[c]) cols[nCol++] = (uint8_t)c;

        /* 16 opaque colors cannot fit 15 palette slots: remap the least
         * frequent color to its nearest EGA neighbor in the frame. */
        if (nCol == 16) {
            int victim = 0;
            for (int c = 1; c < 16; c++)
                if (freq[c] < freq[victim]) victim = c;
            int target = -1;
            long bestD = 0;
            for (int c = 0; c < 16; c++) {
                if (c == victim) continue;
                long d = ega_dist((uint8_t)victim, (uint8_t)c);
                if (target < 0 || d < bestD) { target = c; bestD = d; }
            }
            fprintf(stderr, "snes_gfx_host: WARNING frame %u uses all 16 EGA "
                            "colors; remapping color %d -> %d (%ld px)\n",
                    (unsigned)f, victim, target, freq[victim]);
            g_warnings++;
            for (size_t p = 0; p < nPix; p++)
                if (pix[p] == (uint8_t)victim) pix[p] = (uint8_t)target;
            nCol = 0;
            memset(freq, 0, sizeof freq);
            for (size_t p = 0; p < nPix; p++)
                if (pix[p] != 0xFF) freq[pix[p]]++;
            for (int c = 0; c < 16; c++) if (freq[c]) cols[nCol++] = (uint8_t)c;
        }

        int palId = assign_palette(cols, nCol, f);

        /* Convert to palette-local indices: 0 transparent, 1+slot. */
        for (size_t p = 0; p < nPix; p++) {
            if (pix[p] == 0xFF) { pix[p] = 0; continue; }
            pix[p] = (uint8_t)(1 + pal_find_color(palId, pix[p]));
        }

        /* Decompose into 16x16 / 32x32 parts over a 16px cell grid: use a
         * 32x32 part whenever a full 2x2 cell block is available. */
        int cw = (wPx + 15) / 16, ch = (hPx + 15) / 16;
        uint8_t covered[8][8];
        memset(covered, 0, sizeof covered);
        FrameInfo_T *fi = &frames[f];
        fi->wPx = wPx; fi->hPx = hPx; fi->palId = palId;

        uint8_t frameChr[16 * 512];  /* plenty for 56x24 worst case */
        uint32_t frameChrLen = 0;
        for (int cy = 0; cy < ch; cy++) {
            for (int cx = 0; cx < cw; cx++) {
                if (covered[cy][cx]) continue;
                int large = (cy + 1 < ch && cx + 1 < cw &&
                             !covered[cy][cx + 1] && !covered[cy + 1][cx] &&
                             !covered[cy + 1][cx + 1]);
                int size = large ? 32 : 16;
                int span = size / 16;
                for (int a = 0; a < span; a++)
                    for (int b = 0; b < span; b++)
                        covered[cy + a][cx + b] = 1;
                int pi = fi->nParts++;
                fi->partDx[pi] = (int8_t)(cx * 16);
                fi->partDy[pi] = (int8_t)(cy * 16);
                fi->partLarge[pi] = (uint8_t)large;
                fi->partChrOfs[pi] = (uint8_t)(frameChrLen / 128);
                /* Chars in name-table row order inside the part. */
                int nchar = size / 8;
                uint8_t blk[64];
                for (int ry = 0; ry < nchar; ry++) {
                    for (int rx = 0; rx < nchar; rx++) {
                        grab_block(pix, wPx, hPx,
                                   cx * 16 + rx * 8, cy * 16 + ry * 8, blk);
                        encode_char_4bpp(blk, frameChr + frameChrLen);
                        frameChrLen += 32;
                    }
                }
            }
        }
        free(pix);
        fi->chrLen = frameChrLen;

        if (chunkLen[nChunks - 1] + frameChrLen > CHUNK_CAP) {
            if (nChunks >= MAX_CHUNKS) {
                fprintf(stderr, "snes_gfx_host: ERROR too many sprite chunks\n");
                return 1;
            }
            chunks[nChunks] = (uint8_t *)malloc(CHUNK_CAP);
            chunkLen[nChunks] = 0;
            if (!chunks[nChunks]) { fprintf(stderr, "oom\n"); return 1; }
            nChunks++;
        }
        fi->chunk = nChunks - 1;
        fi->chrOfs = (uint32_t)chunkLen[nChunks - 1];
        memcpy(chunks[nChunks - 1] + chunkLen[nChunks - 1], frameChr, frameChrLen);
        chunkLen[nChunks - 1] += frameChrLen;
    }

    /* Write chunk blobs. */
    char path[1024];
    size_t totalChr = 0;
    for (int c = 0; c < nChunks; c++) {
        snprintf(path, sizeof path, "%s/sprites%d.chr", outdir, c);
        if (write_file(path, chunks[c], chunkLen[c])) return 1;
        totalChr += chunkLen[c];
        free(chunks[c]);
    }

    /* Fragment. */
    snprintf(path, sizeof path, "%s/sprites.frag.c", outdir);
    FILE *frag = fopen(path, "w");
    if (!frag) { perror(path); return 1; }
    fprintf(frag,
        "/* ck_sprite_frames[] fragment - generated by snes_gfx_host.\n"
        " * %u base frames (every 4th MaskedSpriteEntry), %d chr chunk(s). */\n",
        (unsigned)nFrames, nChunks);
    for (int c = 0; c < nChunks; c++)
        fprintf(frag, "extern const u8 ck_sprites_chr%d[];\n", c);
    for (uint16_t f = 0; f < nFrames; f++) {
        FrameInfo_T *fi = &frames[f];
        fprintf(frag, "static const CkSpritePart ck_sprf_%03u_parts[] = {",
                (unsigned)f);
        for (int p = 0; p < fi->nParts; p++)
            fprintf(frag, " {%d,%d,%u,%u},", fi->partDx[p], fi->partDy[p],
                    (unsigned)fi->partLarge[p], (unsigned)fi->partChrOfs[p]);
        fprintf(frag, " };\n");
    }
    fprintf(frag, "const CkSpriteFrame ck_sprite_frames[] = {\n");
    for (uint16_t f = 0; f < nFrames; f++) {
        FrameInfo_T *fi = &frames[f];
        fprintf(frag, "    { ck_sprites_chr%d + %uu, %uu, %d, %d, %d, %d, "
                      "ck_sprf_%03u_parts },\n",
                fi->chunk, fi->chrOfs, fi->chrLen, fi->wPx, fi->hPx,
                fi->palId, fi->nParts, (unsigned)f);
    }
    fprintf(frag, "};\nconst u16 ck_sprite_frame_count = %u;\n",
            (unsigned)nFrames);
    /* Hitboxes for the physics core: 4 shift copies x {l,u,r,b} (world
     * units, s16) per logical frame, straight from the raw
     * MaskedSpriteEntry records (offsets 8/10/12/14, see gfx.c). The
     * game indexes this flat as (frame << 4) + (shiftCopy << 2). */
    fprintf(frag, "const s16 ck_sprite_hitboxes[] = {\n");
    for (uint16_t f = 0; f < nFrames; f++) {
        fprintf(frag, "   ");
        for (int c = 0; c < 4; c++) {
            const uint8_t *e = entries + ((size_t)f * 4 + (size_t)c) * 32;
            fprintf(frag, " %d,%d,%d,%d,",
                    (int)(int16_t)le16(e + 8), (int)(int16_t)le16(e + 10),
                    (int)(int16_t)le16(e + 12), (int)(int16_t)le16(e + 14));
        }
        fprintf(frag, "\n");
    }
    fprintf(frag, "};\n");
    fclose(frag);
    free(frames);
    (void)remapped16;
    fprintf(stderr, "snes_gfx_host: %u sprite frames, %zu chr bytes, "
                    "%d OBJ palettes\n",
            (unsigned)nFrames, totalChr, g_objPalCount);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Full screens                                                       */
/* ------------------------------------------------------------------ */

/* Mirror of do_image_file_decomp (src/decompression/imageRLE.c). */
static void image_rle_decomp(const uint8_t *src, size_t srcLen, uint8_t *dst,
                             size_t dstCap) {
    int32_t dataLength = (int32_t)le32(src);
    const uint8_t *s = src + 4;
    const uint8_t *sEnd = src + srcLen;
    uint8_t *d = dst, *dEnd = dst + dstCap;
    while (dataLength >= 0 && s < sEnd) {
        uint16_t currAction = *s++;
        if (currAction >= 0x80) {
            currAction -= 0x7F;
            for (uint16_t i = 0; i < currAction && s < sEnd && d < dEnd; i++)
                *d++ = *s++;
        } else {
            currAction += 3;
            for (uint16_t i = 0; i < currAction && d < dEnd; i++)
                *d++ = *s;
            s++;
        }
        dataLength -= currAction;
    }
}

/* Dedup an 8bpp image into 4bpp chars + a 64x32 SNES tilemap (emitted in
 * VRAM order for SC_64x32: 32x32 screen A then screen B). Char 0 is a
 * forced blank so unused map cells show backdrop. */
static int emit_screen(const char *outdir, const char *name,
                       const uint8_t *img, int imgW, int imgH,
                       FILE *frag) {
    int cw = (imgW + 7) / 8, chh = (imgH + 7) / 8;
    if (cw > 64) cw = 64;
    if (chh > 32) chh = 32;
    enum { MAX_CHARS = 2048 };
    static uint8_t chr[MAX_CHARS * 32];
    int nChars = 1;
    memset(chr, 0, 32);          /* char 0 = blank */
    uint16_t map[2048];
    memset(map, 0, sizeof map);

    for (int cy = 0; cy < chh; cy++) {
        for (int cx = 0; cx < cw; cx++) {
            uint8_t blk[64], enc[32];
            grab_block(img, imgW, imgH, cx * 8, cy * 8, blk);
            encode_char_4bpp(blk, enc);
            int idx = -1;
            for (int i = 0; i < nChars; i++) {
                if (memcmp(chr + (size_t)i * 32, enc, 32) == 0) { idx = i; break; }
            }
            if (idx < 0) {
                if (nChars >= MAX_CHARS) {
                    fprintf(stderr, "snes_gfx_host: ERROR screen %s has too "
                                    "many unique chars\n", name);
                    return 1;
                }
                memcpy(chr + (size_t)nChars * 32, enc, 32);
                idx = nChars++;
            }
            /* SC_64x32 VRAM layout: cols 0..31 = screen A, 32..63 = B. */
            int word = (cx < 32) ? (cy * 32 + cx) : (1024 + cy * 32 + cx - 32);
            map[word] = (uint16_t)idx;   /* palette 0, priority 0 */
        }
    }

    char path[1024];
    snprintf(path, sizeof path, "%s/screen_%s.chr", outdir, name);
    if (write_file(path, chr, (size_t)nChars * 32)) return 1;
    uint8_t mapLE[4096];
    for (int i = 0; i < 2048; i++) {
        mapLE[i * 2] = (uint8_t)(map[i] & 0xFF);
        mapLE[i * 2 + 1] = (uint8_t)(map[i] >> 8);
    }
    snprintf(path, sizeof path, "%s/screen_%s.map", outdir, name);
    if (write_file(path, mapLE, sizeof mapLE)) return 1;

    fprintf(frag, "extern const u8 ck_screen_%s_chr[];\n", name);
    fprintf(frag, "extern const u8 ck_screen_%s_map[];\n", name);
    fprintf(frag, "const CkFullScreen ck_screen_%s = { ck_screen_%s_chr, "
                  "%uu, (const u16 *)ck_screen_%s_map };\n",
            name, name, (unsigned)(nChars * 32), name);
    fprintf(stderr, "snes_gfx_host: screen_%s (%d unique chars, %d bytes chr)\n",
            name, nChars, nChars * 32);
    return 0;
}

static int emit_rle_screen(const char *staged, const char *fname,
                           const char *outdir, const char *name, FILE *frag,
                           int optional) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", staged, fname);
    size_t sz = 0;
    uint8_t *raw = read_file(path, &sz);
    if (!raw) {
        if (optional) return 0;
        fprintf(stderr, "snes_gfx_host: missing %s\n", path);
        return 1;
    }
    /* Full screen = 4 planes at 0x2000-byte stride (gfx.c:3607). */
    uint8_t *planar = (uint8_t *)calloc(0x8000, 1);
    uint8_t *img = (uint8_t *)malloc(320 * 200);
    if (!planar || !img) { fprintf(stderr, "oom\n"); return 1; }
    image_rle_decomp(raw, sz, planar, 0x8000);
    free(raw);
    uint32_t bit = 0;
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 320; x++) {
            uint8_t v = 0;
            uint8_t mask = (uint8_t)(1 << (bit % 8 ^ 7));
            const uint8_t *bp = planar + bit / 8;
            if (bp[0] & mask) v |= 1;
            if (bp[0x2000] & mask) v |= 2;
            if (bp[0x4000] & mask) v |= 4;
            if (bp[0x6000] & mask) v |= 8;
            img[y * 320 + x] = v;
            bit++;
        }
    }
    free(planar);
    int rc = emit_screen(outdir, name, img, 320, 200, frag);
    free(img);
    return rc;
}

static int emit_screens(const char *staged, const char *ext, int episode,
                        const char *outdir) {
    char path[1024];
    snprintf(path, sizeof path, "%s/screens.frag.c", outdir);
    FILE *frag = fopen(path, "w");
    if (!frag) { perror(path); return 1; }
    fprintf(frag, "/* CkFullScreen fragment - generated by snes_gfx_host. "
                  "Maps are 64x32 in SC_64x32 VRAM order. */\n");

    /* TITLE = EGALATCH bitmap 0 (CVort<n>_bmp_title). */
    {
        const uint8_t *e = g_head + g_imgDataStart;
        uint16_t wBytes = le16(e + 0);
        uint16_t v      = le16(e + 2);
        uint32_t loc    = le32(e + 4);
        uint8_t *img = (uint8_t *)calloc((size_t)wBytes * 8 * v + 1, 1);
        if (!img) { fclose(frag); return 1; }
        decode_bmp(loc, wBytes, v, img);
        int rc = emit_screen(outdir, "title", img, wBytes * 8, v, frag);
        free(img);
        if (rc) { fclose(frag); return 1; }
    }

    char fin[32];
    snprintf(fin, sizeof fin, "FINALE.%s", ext);
    if (emit_rle_screen(staged, fin, outdir, "finale", frag, 0)) {
        fclose(frag); return 1;
    }
    if (episode == 1) {
        if (emit_rle_screen(staged, "PREVIEW2.CK1", outdir, "preview2", frag, 1) ||
            emit_rle_screen(staged, "PREVIEW3.CK1", outdir, "preview3", frag, 1)) {
            fclose(frag); return 1;
        }
    }
    fclose(frag);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Palettes + tileinfo                                                */
/* ------------------------------------------------------------------ */
static int emit_palettes(const char *outdir, int episode) {
    const EpisodeExe_T *ep = &kEpisodeExe[episode - 1];
    /* Layout: 16 EGA + 4*17 flash + 8*16 OBJ = 212 u16 LE. */
    uint16_t words[16 + 4 * 17 + 8 * 16];
    int w = 0;
    for (int i = 0; i < 16; i++) words[w++] = bgr555(kEgaRGB[i]);
    for (int p = 0; p < 4; p++) {
        const uint8_t *pal = g_exe + ep->palettesOffset + (size_t)p * 17;
        if (ep->palettesOffset + (size_t)(p + 1) * 17 > g_exeSize) {
            fprintf(stderr, "snes_gfx_host: flash palette %d out of EXE bounds\n", p);
            return 1;
        }
        for (int i = 0; i < 17; i++)
            words[w++] = bgr555(kEgaRGB[pal[i] & 0x0F]);
    }
    for (int p = 0; p < 8; p++) {
        words[w++] = 0;  /* entry 0 unused/transparent */
        for (int i = 0; i < 15; i++) {
            words[w++] = (p < g_objPalCount && i < g_objPalLen[p])
                       ? bgr555(kEgaRGB[g_objPal[p][i]]) : 0;
        }
    }
    uint8_t out[sizeof words];
    for (int i = 0; i < w; i++) {
        out[i * 2] = (uint8_t)(words[i] & 0xFF);
        out[i * 2 + 1] = (uint8_t)(words[i] >> 8);
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/palettes.bin", outdir);
    int rc = write_file(path, out, (size_t)w * 2);
    if (rc == 0)
        fprintf(stderr, "snes_gfx_host: %s (%d words; EGA[1]=0x%04X)\n",
                path, w, words[1]);
    return rc;
}

static int emit_tileinfo(const char *outdir, int episode) {
    const EpisodeExe_T *ep = &kEpisodeExe[episode - 1];
    size_t bytes = (size_t)ep->tilenum * 6 * 2;
    if (ep->tileinfoOffset + bytes > g_exeSize) {
        fprintf(stderr, "snes_gfx_host: TILEINFO out of EXE bounds\n");
        return 1;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/tileinfo.bin", outdir);
    int rc = write_file(path, g_exe + ep->tileinfoOffset, bytes);
    if (rc == 0)
        fprintf(stderr, "snes_gfx_host: %s (6 x %u int16, %zu bytes)\n",
                path, (unsigned)ep->tilenum, bytes);
    return rc;
}

/* ------------------------------------------------------------------ */
static int decompress_in_place(const char *path, size_t decsize,
                               const char *label) {
    FILE *in = fopen(path, "rb");
    if (!in) { perror(path); return 1; }
    uint8_t *buf = (uint8_t *)malloc(decsize);
    if (!buf) { fclose(in); fprintf(stderr, "oom\n"); return 1; }
    char rc = lz_decompress(in, buf);
    fclose(in);
    if (rc) {
        fprintf(stderr, "snes_gfx_host: lz_decompress(%s) failed\n", path);
        free(buf);
        return 1;
    }
    int wrc = write_file(path, buf, decsize);
    free(buf);
    if (wrc == 0)
        fprintf(stderr, "snes_gfx_host: %s (%s, %zu bytes uncompressed)\n",
                path, label, decsize);
    return wrc;
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
        fprintf(stderr, "snes_gfx_host: bad episode %s\n", argv[3]);
        return 2;
    }

    char head_path[1024], latch_path[1024], sprit_path[1024], exe_path[1024];
    snprintf(head_path, sizeof head_path, "%s/EGAHEAD.%s", staged, ext);
    snprintf(latch_path, sizeof latch_path, "%s/EGALATCH.%s", staged, ext);
    snprintf(sprit_path, sizeof sprit_path, "%s/EGASPRIT.%s", staged, ext);
    snprintf(exe_path, sizeof exe_path, "%s/KEEN%d.EXE", staged, episode);

    g_head = read_file(head_path, &g_headSize);
    if (!g_head || g_headSize < 48) {
        fprintf(stderr, "snes_gfx_host: cannot read %s\n", head_path);
        return 1;
    }
    /* EGAHEAD general section (fixed offsets, see gba_decomp_graphics_host). */
    g_latchPlaneSize = le32(g_head + 0);
    g_sprPlaneSize   = le32(g_head + 4);
    g_imgDataStart   = le32(g_head + 8);
    g_sprDataStart   = le32(g_head + 12);
    g_fontNum        = le16(g_head + 16);
    g_fontLoc        = le32(g_head + 18);
    g_tileNum        = le16(g_head + 28);
    g_tileLoc        = le32(g_head + 30);
    g_bmpNum         = le16(g_head + 34);
    g_bmpLoc         = le32(g_head + 36);
    g_spriteNum      = le16(g_head + 40);
    g_spriteLoc      = le32(g_head + 42);
    uint16_t compression = le16(g_head + 46);

    if (compression & 2) {
        if (decompress_in_place(latch_path, (size_t)g_latchPlaneSize * 4,
                                "EGALATCH")) return 1;
    }
    if (compression & 1) {
        if (decompress_in_place(sprit_path, (size_t)g_sprPlaneSize * 5,
                                "EGASPRIT")) return 1;
    }

    g_latch = read_file(latch_path, &g_latchSize);
    g_sprit = read_file(sprit_path, &g_spritSize);
    if (!g_latch || !g_sprit) {
        fprintf(stderr, "snes_gfx_host: cannot read EGALATCH/EGASPRIT\n");
        return 1;
    }
    if (g_latchSize < (size_t)g_latchPlaneSize * 4 ||
        g_spritSize < (size_t)g_sprPlaneSize * 5) {
        fprintf(stderr, "snes_gfx_host: latch/sprit smaller than plane sizes\n");
        return 1;
    }

    /* MZ-stripped EXE image (offsets in kEpisodeExe are exeImage-relative). */
    {
        size_t raw = 0;
        uint8_t *exe = read_file(exe_path, &raw);
        if (!exe || raw < 64 || exe[0] != 'M' || exe[1] != 'Z') {
            fprintf(stderr, "snes_gfx_host: cannot read MZ exe %s\n", exe_path);
            return 1;
        }
        size_t hdr = 16u * ((size_t)exe[8] | ((size_t)exe[9] << 8));
        g_exe = exe + hdr;
        g_exeSize = raw - hdr;
    }

    if (emit_tiles(outdir)) return 1;
    if (emit_font(outdir)) return 1;
    if (emit_bitmaps(outdir)) return 1;
    if (emit_sprites(outdir)) return 1;
    if (emit_screens(staged, ext, episode, outdir)) return 1;
    if (emit_palettes(outdir, episode)) return 1;
    if (emit_tileinfo(outdir, episode)) return 1;

    fprintf(stderr, "snes_gfx_host: done (%d warnings)\n", g_warnings);
    return 0;
}
