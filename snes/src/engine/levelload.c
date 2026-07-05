#include "engine/levelload.h"
#include "data_format.h"

u16 *map_data_tiles;
u16 *map_data_sprites;
u16 map_width_tile;
u16 map_height_tile;
u16 ck_rowofs[256];

/* See levelload.h for why this exists ((u32)ptr casts drop the bank). */
typedef union CkFarPtr_T {
    const u8 *p;
    u32 v;
} CkFarPtr;

const u8 *ck_far_ofs(const u8 *base, u32 ofs)
{
    CkFarPtr u;
    u.p = base;
    u.v += ofs;
    return u.p;
}

const CkLevelEntry *ck_level_find(u8 levelNum)
{
    const CkLevelEntry *e = ck_levels;
    while (e->levelNum != 0xFF) {
        if (e->levelNum == levelNum)
            return e;
        e++;
    }
    return 0;
}

/* CRLE expansion, transcribed from the desktop engine's
 * CRLE_expandSwapped. The compressed stream: [u32 expanded byte size]
 * [u16 words...], key 0xFEFE = run marker followed by count,value words.
 * Reading starts at byte 2, so the high word of the size dword becomes
 * the first expanded word; the engine drops that word afterwards. We
 * replicate exactly. 16-bit arithmetic only (816-tcc emulates 32-bit
 * ints), advancing the source pointer instead of indexing.
 */
static void ck_crle_expand(u16 *dst, const u8 *src)
{
    u16 finsize;   /* expanded size in words */
    u16 elementnum;
    u16 value, howmany, j;

    finsize = (u16)(((u16)src[1] << 8) | src[0]);
    finsize >>= 1;

    elementnum = 0;
    src += 2;
    while (elementnum < finsize) {
        value = (u16)(((u16)src[1] << 8) | src[0]);
        if (value == 0xFEFE) {
            howmany = (u16)(((u16)src[3] << 8) | src[2]);
            value = (u16)(((u16)src[5] << 8) | src[4]);
            for (j = 0; j < howmany; j++) {
                dst[elementnum] = value;
                elementnum++;
            }
            src += 6;
        } else {
            dst[elementnum] = value;
            elementnum++;
            src += 2;
        }
    }
}

u8 ck_level_load(u8 levelNum)
{
    const CkLevelEntry *e = ck_level_find(levelNum);
    u16 *md = ck_map_data;
    u16 w, h, y;
    u16 ofs;

    if (!e)
        return 1;

    /* Expand one word beyond, then drop the leading word (see above). */
    ck_crle_expand(md, e->data);
    {
        /* Shift down by one word; after the shift the header starts at
         * md[0]. Span = expanded word count from the blob header. */
        u16 words = (u16)((((u16)e->data[1] << 8) | e->data[0]) >> 1);
        u16 k;
        for (k = 0; k + 1 < words; k++)
            md[k] = md[k + 1];
    }

    w = md[0];
    h = md[1];
    map_width_tile = w;
    map_height_tile = h;
    map_data_tiles = md + 16;
    map_data_sprites = md + 16 + (md[7] >> 1);

    ofs = 0;
    for (y = 0; y < 256; y++) {
        ck_rowofs[y] = ofs;
        if (y < h)
            ofs += w;
    }
    return 0;
}
