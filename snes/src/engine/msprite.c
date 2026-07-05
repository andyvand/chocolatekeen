#include "engine/msprite.h"
#include "data_format.h"
#include "snes_data_gen.h"

/* OBJ name space: 16 chars wide x 32 rows at CK_OBJ_CHR_VRAM (512 chars,
 * 16 KB). Partitioned into fixed slot pools:
 *   rows 0..11 : 48 slots of 16x16 (2x2 chars), 8 per row-pair
 *   rows 12..31: 20 slots of 32x32 (4x4 chars), 4 per 4-row group
 * A slot's OAM tile number is its top-left char index. Part chr data is
 * stored linearly (row pairs of 64B/128B); uploads go row-by-row with a
 * 256-word stride (16 chars x 16 words).
 */
#define CK_SLOTS16 48
#define CK_SLOTS32 20

/* cache key: frame*8 + part (parts <= 6 for the largest frames) */
#define CK_KEY(frameIdx, part) (((frameIdx) << 3) | (part))
#define CK_KEY_NONE 0xFFFF

/* Direct-mapped memo in front of the linear slot scans: the scans cost
 * ~whole scanlines in 816-tcc code and run for every part every frame.
 * memoKey[key & 63] == key -> memoTile is the slot's OAM tile. Stays
 * warm across animation cycles; a miss falls back to the full scan. */
static u16 s_memoKey[64];
static u16 s_memoTile[64];

static u16 s_key16[CK_SLOTS16];
static u16 s_key32[CK_SLOTS32];
static u8 s_stamp16[CK_SLOTS16];   /* frame counter of last use */
static u8 s_stamp32[CK_SLOTS32];
static u8 s_frameStamp;

/* Upload queue: a PERSISTENT ring drained with a fixed per-vblank row
 * budget. VRAM writes outside vblank are silently dropped by the PPU,
 * and pvsneslib's NMI handler consumes a large slice of vblank before
 * main code resumes - so only a few transfers fit per frame. Entries
 * stay queued across frames until sent. rows: 2 for 16x16, 4 for 32x32
 * (row = 64B/128B to VRAM, +256 words per row: OBJ name matrix). */
typedef struct {
    const u8 *src;
    u16 vramWord;
    u8 rows;
    u8 rowBytes;
} CkUpload;
#define CK_MAX_UPLOADS 24          /* ring capacity                     */
#define CK_UPLOAD_ROWS_PER_VBLANK 16
static CkUpload s_uploads[CK_MAX_UPLOADS];
static u8 s_upHead;                /* next entry to send                */
static u8 s_nUploads;              /* entries queued                    */

static u8 s_oamNext;               /* next OAM entry this frame */

/* OAM high table shadow, PERSISTENT across frames (clearing 32 bytes +
 * copying 32 bytes every frame costs whole scanlines in 816-tcc code).
 * oam_put clears-then-sets its entry's two bits ([size, x-msb]) via
 * constant lookup tables - no variable shifts, and no read-modify-write
 * on oamMemory itself (816-tcc has miscompiled both patterns in this
 * project; RMW on our own array is the pattern already proven by the
 * previous |= code). ck_msprite_end copies only the bytes covering the
 * entries used this frame; hidden entries keep stale bits, which is
 * harmless at y=224 (offscreen for both OBJ sizes). */
static u8 s_hiShadow[32];
static u8 s_prevMax;               /* s_oamNext high-water, last frame  */
static const u8 s_hiSize[4]  = { 0x02, 0x08, 0x20, 0x80 };
static const u8 s_hiXmsb[4]  = { 0x01, 0x04, 0x10, 0x40 };
static const u8 s_hiInv[4]   = { 0xFC, 0xF3, 0xCF, 0x3F };

static u16 slot16_tile(u8 slot)
{
    /* 8 slots per row-pair; row-pair r, column c -> char r*32 + c*2 */
    return (u16)(((slot >> 3) << 5) + ((slot & 7) << 1));
}

static u16 slot32_tile(u8 slot)
{
    /* 4 slots per 4-row group starting at row 12 -> char 12*16 = 192 */
    return (u16)(192 + ((slot >> 2) << 6) + ((slot & 3) << 2));
}

/* Returns 0 when the frame's upload budget is exhausted. The caller
 * must then leave the slot key at CK_KEY_NONE so it re-misses next
 * frame - marking it as holding `key` anyway would permanently show
 * whatever chr was in the slot before (stale-sprite artifact). */
static u8 queue_upload(const u8 *src, u16 tile, u8 rows, u8 rowBytes)
{
    CkUpload *u;
    u8 slot;
    if (s_nUploads >= CK_MAX_UPLOADS)
        return 0;
    slot = (u8)(s_upHead + s_nUploads);
    if (slot >= CK_MAX_UPLOADS)
        slot -= CK_MAX_UPLOADS;
    u = &s_uploads[slot];
    u->src = src;
    u->vramWord = (u16)(CK_OBJ_CHR_VRAM + (tile << 4));
    u->rows = rows;
    u->rowBytes = rowBytes;
    s_nUploads++;
    return 1;
}

/* One chr row to VRAM via raw channel-0 DMA (a dmaCopyVram call is too
 * slow to fit more than ~4 in the post-ISR vblank remnant). Source bank
 * comes from the pointer bytes through a union - the (u32)ptr cast is
 * miscompiled by 816-tcc (see project notes). */
typedef union {
    const u8 *p;
    u8 b[4];                       /* b[0..1] addr, b[2] bank           */
} CkFarPtr;

static void vram_row_dma(const u8 *src, u16 vramWord, u8 bytes)
{
    CkFarPtr fp;
    fp.p = src;
    REG_VMAIN = 0x80;
    REG_VMADDLH = vramWord;
    REG_DMAP0 = 0x01;              /* 2 regs write once (2118/19)       */
    REG_BBAD0 = 0x18;
    REG_A1T0LH = (u16)(((u16)fp.b[1] << 8) | fp.b[0]);
    REG_A1B0 = fp.b[2];
    REG_DAS0LH = bytes;
    REG_MDMAEN = 0x01;
}

/* Find or allocate a slot for (key); returns OAM tile number and queues
 * the chr upload on a miss. Simple aging: steal the slot least recently
 * used (not used this frame). */
static u16 cache16(u16 key, const u8 *chr)
{
    u8 i, victim = 0;
    u8 best = 0;
    u8 m = (u8)(key & 63);
    u16 *k;
    if (s_memoKey[m] == key) {
        /* memo hit: refresh the LRU stamp via the tile's slot index */
        u16 t = s_memoTile[m];
        s_stamp16[(u8)(((t >> 5) << 3) | ((t & 31) >> 1))] = s_frameStamp;
        return t;
    }
    k = s_key16;
    for (i = 0; i < CK_SLOTS16; i++, k++) {
        if (*k == key) {
            s_stamp16[i] = s_frameStamp;
            s_memoKey[m] = key;
            s_memoTile[m] = slot16_tile(i);
            return s_memoTile[m];
        }
    }
    for (i = 0; i < CK_SLOTS16; i++) {
        u8 age = (u8)(s_frameStamp - s_stamp16[i]);
        if (age > best || s_key16[i] == CK_KEY_NONE) {
            if (s_key16[i] == CK_KEY_NONE) { victim = i; break; }
            best = age;
            victim = i;
        }
    }
    {
        /* the victim slot may be memoized under its old key */
        u8 om = (u8)(s_key16[victim] & 63);
        if (s_memoKey[om] == s_key16[victim])
            s_memoKey[om] = CK_KEY_NONE;
    }
    if (queue_upload(chr, slot16_tile(victim), 2, 64)) {
        s_key16[victim] = key;
        s_memoKey[m] = key;
        s_memoTile[m] = slot16_tile(victim);
    } else {
        s_key16[victim] = CK_KEY_NONE; /* re-miss next frame */
        s_memoKey[m] = CK_KEY_NONE;
    }
    s_stamp16[victim] = s_frameStamp;
    return slot16_tile(victim);
}

static u16 cache32(u16 key, const u8 *chr)
{
    u8 i, victim = 0;
    u8 best = 0;
    u8 m = (u8)(key & 63);
    u16 *k;
    if (s_memoKey[m] == key) {
        u16 t = s_memoTile[m];
        s_stamp32[(u8)((((t - 192) >> 6) << 2) | (((t - 192) & 63) >> 2))] =
            s_frameStamp;
        return t;
    }
    k = s_key32;
    for (i = 0; i < CK_SLOTS32; i++, k++) {
        if (*k == key) {
            s_stamp32[i] = s_frameStamp;
            s_memoKey[m] = key;
            s_memoTile[m] = slot32_tile(i);
            return s_memoTile[m];
        }
    }
    for (i = 0; i < CK_SLOTS32; i++) {
        u8 age = (u8)(s_frameStamp - s_stamp32[i]);
        if (age > best || s_key32[i] == CK_KEY_NONE) {
            if (s_key32[i] == CK_KEY_NONE) { victim = i; break; }
            best = age;
            victim = i;
        }
    }
    {
        u8 om = (u8)(s_key32[victim] & 63);
        if (s_memoKey[om] == s_key32[victim])
            s_memoKey[om] = CK_KEY_NONE;
    }
    if (queue_upload(chr, slot32_tile(victim), 4, 128)) {
        s_key32[victim] = key;
        s_memoKey[m] = key;
        s_memoTile[m] = slot32_tile(victim);
    } else {
        s_key32[victim] = CK_KEY_NONE; /* re-miss next frame */
        s_memoKey[m] = CK_KEY_NONE;
    }
    s_stamp32[victim] = s_frameStamp;
    return slot32_tile(victim);
}

/* Write one OAM entry directly into pvsneslib's oamMemory (mirrored to
 * the PPU by the library VBlank ISR). */
static void oam_put(u8 id, s16 x, s16 y, u16 tile, u8 palId, u8 large)
{
    u16 o = (u16)id << 2;
    u8 sub = (u8)(id & 3);
    u8 byteIdx = (u8)(id >> 2);
    oamMemory[o + 0] = (u8)(x & 0xFF);
    oamMemory[o + 1] = (u8)(y & 0xFF);
    oamMemory[o + 2] = (u8)(tile & 0xFF);
    /* attr: vhoopppc - no flips, priority 2, palette, tile bit 8 */
    oamMemory[o + 3] = (u8)(0x20 | ((palId & 7) << 1) | ((tile >> 8) & 1));

    {
        u8 v = (u8)(s_hiShadow[byteIdx] & s_hiInv[sub]);
        if (large)
            v |= s_hiSize[sub];
        if (x < 0)
            v |= s_hiXmsb[sub];
        else if (x > 255)
            v |= s_hiXmsb[sub];
        s_hiShadow[byteIdx] = v;
    }
}

static void oam_hide(u8 id)
{
    u16 o = (u16)id << 2;
    /* y=224: fully below the 224-line display for both 16x16 and 32x32
     * OBJs. (y=240 leaks a 32x32 OBJ's bottom half onto the top of the
     * screen via Y wraparound - stale "ghost sprite" artifact.) */
    oamMemory[o + 1] = 224;
}

void ck_msprite_init(void)
{
    u16 i;
    for (i = 0; i < CK_SLOTS16; i++) { s_key16[i] = CK_KEY_NONE; s_stamp16[i] = 0; }
    for (i = 0; i < CK_SLOTS32; i++) { s_key32[i] = CK_KEY_NONE; s_stamp32[i] = 0; }
    for (i = 0; i < 64; i++) { s_memoKey[i] = CK_KEY_NONE; s_memoTile[i] = 0; }
    s_nUploads = 0;
    s_upHead = 0;
    s_frameStamp = 0;
    s_oamNext = 0;
    s_prevMax = 0;

    /* OBJ palettes from the bake (CGRAM 128..255). */
    dmaCopyCGram((u8 *)ck_obj_pals, 128, 8 * 16 * 2);

    /* OBJSEL: small 16x16 / large 32x32, name base at CK_OBJ_CHR_VRAM. */
    REG_OBSEL = (u8)(OBJ_SIZE16_L32 | (CK_OBJ_CHR_VRAM >> 13));

    for (i = 0; i < 128; i++)
        oam_hide((u8)i);
    for (i = 0; i < 32; i++) {
        s_hiShadow[i] = 0;
        oamMemory[512 + i] = 0;
    }
}

void ck_msprite_begin(void)
{
    s_frameStamp++;
    s_oamNext = 0;
}

extern volatile u8 ck_dbg_stage;

void ck_msprite_draw(u16 frameIdx, s16 screenX, s16 screenY)
{
    const CkSpriteFrame *f;
    const CkSpritePart *p;
    u8 i;
    s16 px, py;
    u16 tile;

    ck_dbg_stage = 0x30;
    if (frameIdx >= ck_sprite_frame_count)
        return;
    ck_dbg_stage = 0x31;
    f = &ck_sprite_frames[frameIdx];

    p = f->parts;
    for (i = 0; i < f->nParts; i++, p++) {
        ck_dbg_stage = 0x32;
        if (s_oamNext >= 128)
            return;
        px = (s16)(screenX + p->dx);
        py = (s16)(screenY + p->dy);
        if (px <= -32 || px >= 256 || py <= -32 || py >= 224)
            continue;
        ck_dbg_stage = 0x33;
        if (p->large)
            tile = cache32(CK_KEY(frameIdx, i), f->chr + ((u16)p->chrOfs << 7));
        else
            tile = cache16(CK_KEY(frameIdx, i), f->chr + ((u16)p->chrOfs << 7));
        ck_dbg_stage = 0x34;
        oam_put(s_oamNext, px, py, tile, f->palId, p->large);
        ck_dbg_stage = 0x35;
        s_oamNext++;
    }
    ck_dbg_stage = 0x36;
}

void ck_msprite_end(void)
{
    u8 i, n;
    /* hide only entries that were in use last frame */
    for (i = s_oamNext; i < s_prevMax; i++)
        oam_hide(i);
    s_prevMax = s_oamNext;
    /* hi-table bytes for the entries put this frame */
    n = (u8)((s_oamNext + 3) >> 2);
    for (i = 0; i < n; i++)
        oamMemory[512 + i] = s_hiShadow[i];
}

/* Send one queued part (all its rows). Caller checks s_nUploads. */
static void send_one_upload(void)
{
    const CkUpload *u = &s_uploads[s_upHead];
    u8 r;
    for (r = 0; r < u->rows; r++) {
        vram_row_dma(u->src + (u16)r * u->rowBytes,
                     (u16)(u->vramWord + ((u16)r << 8)),
                     u->rowBytes);
    }
    s_upHead++;
    if (s_upHead >= CK_MAX_UPLOADS)
        s_upHead = 0;
    s_nUploads--;
}

void ck_msprite_vblank(void)
{
    /* VRAM writes outside vblank are dropped by the PPU, and the
     * pvsneslib NMI handler consumes most of the vblank before
     * WaitForVBlank returns. Gate each part on the live vblank flag
     * ($4212 bit 7) so we send exactly as much as still fits - never
     * more (silent drops), never a fixed guess. */
    while (s_nUploads) {
        if (!(REG_HVBJOY & 0x80))
            break;                 /* vblank over: resume next frame    */
        send_one_upload();
    }
}

/* Unbounded drain for forced-blank contexts (level/session init):
 * VRAM is writable the whole time, so the ring empties in one call. */
void ck_msprite_flush_all(void)
{
    while (s_nUploads)
        send_one_upload();
}
