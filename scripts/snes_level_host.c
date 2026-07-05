/*
 * snes_level_host.c — level copier + per-level tile preload closure.
 *
 * For every LEVEL??.<ext> in the staged dir:
 *
 *   lev<NN>.bin   byte-identical copy of the original CRLE-compressed
 *                 level (the SNES runtime expands into WRAM bank $7F,
 *                 because levels are mutated at runtime).
 *
 *   lev<NN>.tset  preload tile set: u16 count, then count u16 global tile
 *                 ids, sorted ascending. Closure =
 *                   static tiles in the tile plane
 *                   U tile-anim chains (animsetup walk over TILEINFO_Anim,
 *                     mirroring src/game/gameplay.c:163-204)
 *                   U hardcoded mutation constants {0x8F, 0x10E, 0x114}
 *                     (door/pickup/bridge writes, src/game/physics.c)
 *                   U pickup group tiles (t/13)*13 for present tiles whose
 *                     TILEINFO_Type is 6..16 (Keen 3 rewrites pickups to
 *                     the base of their 13-tile group, physics.c:580/622)
 *                   U worldmap "done" markers 0x34..0x38 and 0x4D..0x51
 *                     for level 80 (src/game/worldmap.c:91-110).
 *                 Count is asserted <= 256 (CK_VRAM_TILE_SLOTS).
 *
 * The CRLE stream format matches scripts/gba_preprocess_misc_host.c:70-90
 * and src/game/gameplay.c:444-508 (expand, then drop the leading word).
 *
 * Usage: snes_level_host <staged_dir> <ext> <episode 1|2|3> <outdir>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

#define CK_VRAM_TILE_SLOTS 256

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

/* Mirror of CRLE_expandSwapped (see gba_preprocess_misc_host.c). */
static void crle_expand_swapped(uint16_t *dst, const uint8_t *src, uint16_t key) {
    size_t finsize = ((size_t)src[1] << 8) | src[0];
    finsize /= 2;
    size_t elementnum = 0;
    size_t i = 2;
    while (elementnum < finsize) {
        uint16_t value = (uint16_t)(((uint16_t)src[i + 1] << 8) | src[i]);
        if (value == key) {
            size_t howmany = ((size_t)src[i + 3] << 8) | src[i + 2];
            value = (uint16_t)(((uint16_t)src[i + 5] << 8) | src[i + 4]);
            for (size_t j = 0; j < howmany; j++, elementnum++)
                dst[elementnum] = value;
            i += 6;
        } else {
            dst[elementnum] = value;
            elementnum++;
            i += 2;
        }
    }
}

/* Per-episode EXE constants (src/episodes/episode{1,2,3}.h). */
typedef struct {
    uint32_t tileinfoOffset;
    uint32_t tilenum;
} EpisodeExe_T;

static const EpisodeExe_T kEpisodeExe[3] = {
    { 0x130F8, 611 },
    { 0x17828, 689 },
    { 0x198C8, 715 },
};

/* anim_frame_tiles equivalent, identity-initialized then filled by the
 * animsetup walk. Sized for the max TILENUM (ep3: 715). */
static uint16_t g_animFrames[4][1024];
static int16_t  g_tileType[1024];
static uint16_t g_egaTileNum;    /* tile count in EGAHEAD / tiles.chr */
static uint32_t g_exeTilenum;    /* TILEINFO entry count */
static int      g_episode;       /* 1..3: gates mutation-constant sets */

/* Replicates the animsetup loop in src/game/gameplay.c:163-204 exactly
 * (scaleConst == 1: anim frames are tile indices). */
static void build_anim_frames(const uint16_t *animTab, uint16_t tileNum) {
    for (uint32_t t = 0; t < 1024; t++)
        g_animFrames[0][t] = g_animFrames[1][t] =
        g_animFrames[2][t] = g_animFrames[3][t] = (uint16_t)t;
    for (uint32_t v = 0; v < tileNum; v++) {
        switch (animTab[v]) {
            case 1:
                g_animFrames[3][v] = g_animFrames[2][v] =
                g_animFrames[1][v] = g_animFrames[0][v] = (uint16_t)v;
                break;
            case 2:
                g_animFrames[2][v] = g_animFrames[0][v] = (uint16_t)v;
                g_animFrames[3][v] = g_animFrames[1][v] = (uint16_t)(v + 1);
                v++;
                if (v >= tileNum) break;
                g_animFrames[2][v] = g_animFrames[0][v] = (uint16_t)v;
                g_animFrames[3][v] = g_animFrames[1][v] = (uint16_t)(v - 1);
                break;
            case 4:
                g_animFrames[0][v] = (uint16_t)v;
                g_animFrames[1][v] = (uint16_t)(v + 1);
                g_animFrames[2][v] = (uint16_t)(v + 2);
                g_animFrames[3][v] = (uint16_t)(v + 3);
                v++;
                if (v >= tileNum) break;
                g_animFrames[0][v] = (uint16_t)v;
                g_animFrames[1][v] = (uint16_t)(v + 1);
                g_animFrames[2][v] = (uint16_t)(v + 2);
                g_animFrames[3][v] = (uint16_t)(v - 1);
                v++;
                if (v >= tileNum) break;
                g_animFrames[0][v] = (uint16_t)v;
                g_animFrames[1][v] = (uint16_t)(v + 1);
                g_animFrames[2][v] = (uint16_t)(v - 2);
                g_animFrames[3][v] = (uint16_t)(v - 1);
                v++;
                if (v >= tileNum) break;
                g_animFrames[0][v] = (uint16_t)v;
                g_animFrames[1][v] = (uint16_t)(v - 3);
                g_animFrames[2][v] = (uint16_t)(v - 2);
                g_animFrames[3][v] = (uint16_t)(v - 1);
                break;
            default:
                break;
        }
    }
}

/* Tile set as a bitmap over global tile ids. */
static uint8_t g_inSet[1024];

static void set_add(uint16_t t) {
    if (t < g_egaTileNum)
        g_inSet[t] = 1;
    /* Tiles >= the EGAHEAD tile count have no chr data (ep3's TILEINFO is
     * sized 715 vs 624 real tiles) — skip them silently; the runtime LUT
     * maps unknown ids to slot 0. */
}

static int process_level(const char *inPath, int levelNum, const char *outdir) {
    size_t sz = 0;
    uint8_t *comp = read_file(inPath, &sz);
    if (!comp) { perror(inPath); return 1; }
    if (sz < 2) {
        fprintf(stderr, "%s: truncated\n", inPath);
        free(comp);
        return 1;
    }

    /* Byte-identical copy. */
    char outPath[1024];
    snprintf(outPath, sizeof outPath, "%s/lev%02d.bin", outdir, levelNum);
    if (write_file(outPath, comp, sz)) { free(comp); return 1; }

    /* Expand to find the static tile plane. */
    size_t finsize_bytes = ((size_t)comp[1] << 8) | comp[0];
    if (finsize_bytes < 2 || (finsize_bytes & 1) || finsize_bytes > 0x10000) {
        fprintf(stderr, "%s: bad finsize header (%zu)\n", inPath, finsize_bytes);
        free(comp);
        return 1;
    }
    size_t words = finsize_bytes / 2;
    uint16_t *exp = (uint16_t *)calloc(words, sizeof(uint16_t));
    if (!exp) { free(comp); fprintf(stderr, "oom\n"); return 1; }
    crle_expand_swapped(exp, comp, 0xFEFE);
    free(comp);

    /* Drop the leading word (gameplay.c:484); header then reads:
     * [0]=width, [1]=height, [7]=plane size in bytes; tiles at word 16. */
    uint16_t *hdr = exp + 1;
    uint16_t w = hdr[0], h = hdr[1];
    if (w < 2 || w > 512 || h < 2 || h > 512) {
        fprintf(stderr, "%s: implausible dimensions %ux%u\n", inPath, w, h);
        free(exp);
        return 1;
    }
    uint16_t *tiles = hdr + 16;
    size_t tileCount = (size_t)w * h;
    if (16 + tileCount > words - 1) {
        fprintf(stderr, "%s: tile plane overruns expansion\n", inPath);
        free(exp);
        return 1;
    }

    memset(g_inSet, 0, sizeof g_inSet);
    size_t oob = 0;
    for (size_t i = 0; i < tileCount; i++) {
        uint16_t t = tiles[i];
        if (t >= g_egaTileNum) { oob++; continue; }
        g_inSet[t] = 1;
    }
    if (oob) {
        fprintf(stderr, "snes_level_host: WARNING LEVEL%02d has %zu tile ids "
                        ">= tile bank size %u\n", levelNum, oob,
                (unsigned)g_egaTileNum);
    }

    /* Mutation constants (physics.c: doors/pickups 0x8F/0x114, bridges
     * 0x10E, switch on/off tiles). */
    set_add(0x8F);
    set_add(0x10E);
    set_add(0x114);
    set_add(0x1E0);
    set_add(0x1ED);

    if (g_episode == 1) {
        /* ep1: statue message tiles, shot-chain machine destruction
         * (CVort1_body_shot_chain), earth tiles. */
        set_add(0x13B);
        set_add(0x1B2);
        set_add(0x9B);
        for (uint16_t t = 0x1DC; t <= 0x1DE; t++) set_add(t);
        for (uint16_t t = 0x1E9; t <= 0x1EB; t++) set_add(t);
    } else if (g_episode == 2) {
        /* ep2: tantalus destruction (CVort2_body_destroy_tantalus /
         * CVort2_tantalus_explosion) + earth-explode patch (level 81,
         * 0x9B). */
        set_add(0x9B);
        set_add(0x1EC);
        set_add(0x1F9);
        set_add(0x1FA);
        set_add(0x215);
        for (uint16_t t = 0x222; t <= 0x225; t++) set_add(t);
    } else {
        /* ep3: Mangling Machine arm/leg redraw + destruction tiles
         * (CVort3_think_mangling_*). */
        set_add(0xA9);
        set_add(0x255);
        for (uint16_t t = 0x26A; t <= 0x26F; t++) set_add(t);
        if (levelNum == 80) {
            /* worldmap teleporter pads: flash frames 130..133 + resting
             * tile 0x86 (CVort3_worldmap_sprites). */
            for (uint16_t t = 0x82; t <= 0x86; t++) set_add(t);
        }
    }

    /* Pickup groups: (t/13)*13 for present pickup-type tiles (Keen 3
     * rewrites pickups to the base of their 13-tile group; ep3 also
     * does this for keys 18..21, the ankh 27 and single ammo 28). */
    for (uint16_t t = 0; t < g_egaTileNum; t++) {
        if (!g_inSet[t]) continue;
        if (t >= g_exeTilenum) continue;
        int16_t type = g_tileType[t];
        int inGroup = (type >= 6 && type <= 16);
        if (g_episode == 3) {
            if (type >= 18 && type <= 21) inGroup = 1;
            if (type == 27 || type == 28) inGroup = 1;
        }
        if (inGroup)
            set_add((uint16_t)((t / 13) * 13));
    }

    /* Worldmap completed-level markers. */
    if (levelNum == 80) {
        for (uint16_t t = 0x34; t <= 0x38; t++) set_add(t);
        for (uint16_t t = 0x4D; t <= 0x51; t++) set_add(t);
    }

    /* Status-box / ship-dialog icon tiles (drawn as BG1 overlays by the
     * SNES UI: ship parts, keycards, weapon, pogo/ankh). Every level can
     * open the status box; level 80 shows the ep1 ship dialog. */
    if (g_episode == 1) {
        for (uint16_t t = 0x141; t <= 0x144; t++) set_add(t); /* parts missing */
        for (uint16_t t = 0x1C0; t <= 0x1C3; t++) set_add(t); /* parts held    */
        for (uint16_t t = 0x1A8; t <= 0x1AB; t++) set_add(t); /* keycards      */
        set_add(0x19E);                                       /* raygun        */
        set_add(0x19F);                                       /* pogo          */
    } else if (g_episode == 2) {
        for (uint16_t t = 0x1A8; t <= 0x1AB; t++) set_add(t); /* keycards      */
        set_add(0x19E);                                       /* pistol        */
    } else {
        for (uint16_t t = 0xD9; t <= 0xDC; t++) set_add(t);   /* keycards      */
        set_add(0xD6);                                        /* ankh          */
        set_add(0xD8);                                        /* pistol        */
    }

    /* Anim chain closure to a fixpoint. */
    for (;;) {
        int changed = 0;
        for (uint16_t t = 0; t < g_egaTileNum; t++) {
            if (!g_inSet[t]) continue;
            for (int ph = 0; ph < 4; ph++) {
                uint16_t a = g_animFrames[ph][t];
                if (a < g_egaTileNum && !g_inSet[a]) {
                    g_inSet[a] = 1;
                    changed = 1;
                }
            }
        }
        if (!changed) break;
    }

    /* Emit sorted tset. */
    uint16_t count = 0;
    for (uint16_t t = 0; t < g_egaTileNum; t++)
        if (g_inSet[t]) count++;
    if (count > CK_VRAM_TILE_SLOTS) {
        fprintf(stderr, "snes_level_host: ASSERT FAILED: LEVEL%02d closure "
                        "%u > %d VRAM slots\n", levelNum, (unsigned)count,
                CK_VRAM_TILE_SLOTS);
        free(exp);
        return 1;
    }
    uint8_t *tset = (uint8_t *)malloc(2u * (count + 1));
    if (!tset) { free(exp); fprintf(stderr, "oom\n"); return 1; }
    tset[0] = (uint8_t)(count & 0xFF);
    tset[1] = (uint8_t)(count >> 8);
    size_t o = 2;
    for (uint16_t t = 0; t < g_egaTileNum; t++) {
        if (!g_inSet[t]) continue;
        tset[o++] = (uint8_t)(t & 0xFF);
        tset[o++] = (uint8_t)(t >> 8);
    }
    snprintf(outPath, sizeof outPath, "%s/lev%02d.tset", outdir, levelNum);
    int rc = write_file(outPath, tset, o);
    free(tset);
    free(exp);
    if (rc == 0)
        fprintf(stderr, "snes_level_host: LEVEL%02d %ux%u comp=%zuB "
                        "closure=%u tiles\n", levelNum, w, h, sz, count);
    return rc;
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
        fprintf(stderr, "snes_level_host: bad episode %s\n", argv[3]);
        return 2;
    }
    const EpisodeExe_T *ep = &kEpisodeExe[episode - 1];
    g_exeTilenum = ep->tilenum;
    g_episode = episode;

    /* EGAHEAD tile count bounds the chr bank / preload ids. */
    {
        char path[1024];
        snprintf(path, sizeof path, "%s/EGAHEAD.%s", staged, ext);
        size_t sz = 0;
        uint8_t *head = read_file(path, &sz);
        if (!head || sz < 48) {
            fprintf(stderr, "snes_level_host: cannot read %s\n", path);
            return 1;
        }
        g_egaTileNum = le16(head + 28);
        free(head);
        if (g_egaTileNum == 0 || g_egaTileNum > 1024) {
            fprintf(stderr, "snes_level_host: implausible tileNum %u\n",
                    (unsigned)g_egaTileNum);
            return 1;
        }
    }

    /* TILEINFO Anim + Type from the MZ-stripped EXE image. */
    {
        char path[1024];
        snprintf(path, sizeof path, "%s/KEEN%d.EXE", staged, episode);
        size_t sz = 0;
        uint8_t *exe = read_file(path, &sz);
        if (!exe || sz < 64 || exe[0] != 'M' || exe[1] != 'Z') {
            fprintf(stderr, "snes_level_host: cannot read MZ exe %s\n", path);
            return 1;
        }
        size_t hdrSz = 16u * ((size_t)exe[8] | ((size_t)exe[9] << 8));
        const uint8_t *img = exe + hdrSz;
        size_t imgSz = sz - hdrSz;
        if (ep->tileinfoOffset + (size_t)ep->tilenum * 12 > imgSz) {
            fprintf(stderr, "snes_level_host: TILEINFO out of EXE bounds\n");
            free(exe);
            return 1;
        }
        static uint16_t animTab[1024];
        for (uint32_t t = 0; t < ep->tilenum && t < 1024; t++) {
            animTab[t] = le16(img + ep->tileinfoOffset + 2u * t);
            g_tileType[t] = (int16_t)le16(img + ep->tileinfoOffset
                                          + 2u * ep->tilenum + 2u * t);
        }
        /* The animsetup loop runs over the EGAHEAD tile count (see
         * gameplay.c:164: loopVar < engine_egaHeadGeneral.tileNum). */
        uint16_t bound = g_egaTileNum;
        if (bound > ep->tilenum) bound = (uint16_t)ep->tilenum;
        build_anim_frames(animTab, bound);
        free(exe);
    }

    /* Walk LEVEL*.<ext> files. */
    char want_suffix[16];
    snprintf(want_suffix, sizeof want_suffix, ".%s", ext);
    DIR *d = opendir(staged);
    if (!d) { perror(staged); return 1; }
    struct dirent *entry;
    int rc = 0, nLevels = 0;
    /* Deterministic order: collect level numbers, then process sorted. */
    int levelNums[128];
    while ((entry = readdir(d)) != NULL) {
        const char *n = entry->d_name;
        if (strncasecmp(n, "LEVEL", 5) != 0) continue;
        const char *p = n + 5;
        int num = 0, digits = 0;
        while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; digits++; }
        if (!digits || strcasecmp(p, want_suffix) != 0) continue;
        if (nLevels < 128) levelNums[nLevels++] = num;
    }
    closedir(d);
    for (int i = 0; i < nLevels; i++)         /* insertion sort */
        for (int j = i + 1; j < nLevels; j++)
            if (levelNums[j] < levelNums[i]) {
                int t = levelNums[i]; levelNums[i] = levelNums[j]; levelNums[j] = t;
            }
    for (int i = 0; i < nLevels && rc == 0; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/LEVEL%02d.%s", staged, levelNums[i], ext);
        rc = process_level(path, levelNums[i], outdir);
    }
    if (rc == 0)
        fprintf(stderr, "snes_level_host: %d levels processed\n", nLevels);
    return rc;
}
