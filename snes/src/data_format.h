/* Chocolate Keen SNES port - contract between the host asset pipeline
 * (scripts/snes_*_host.c, scripts/snes_emit_data.c) and the SNES runtime.
 *
 * The bake emits build/snes/generated-ep<N>/{data_ep<N>.asm, snes_data_gen.c/.h}
 * plus raw blobs. Everything in this header is fixed-format; change it only in
 * lockstep on both sides.
 *
 * All multi-byte values in binary blobs are little-endian (65816-native).
 */
#ifndef CK_SNES_DATA_FORMAT_H
#define CK_SNES_DATA_FORMAT_H

#include <snes.h>

/* ---- BG tiles ----------------------------------------------------------
 * ck_tiles_chr: full global tile bank, SNES 4bpp planar. Each 16x16 world
 * tile = 4 consecutive 8x8 chars in order TL,TR,BL,BR (128 bytes per tile).
 * Global tile index = Keen tile index (matches TILEINFO_* tables and all
 * hardcoded mutation constants in game logic).
 *
 * Per-level preload set (levNN.tset payload, emitted into ck_levels[]):
 * u16 count, then count u16 global tile indices, sorted ascending. The
 * runtime uploads those tiles' chars to VRAM slots 0..count-1 and builds
 * the global->VRAM LUT. Count is asserted <= CK_VRAM_TILE_SLOTS at bake.
 */
#define CK_VRAM_TILE_SLOTS 256

/* ---- Levels ------------------------------------------------------------
 * Level payloads are the original LEVEL??.CK<n> files, byte-identical
 * (CRLE-compressed, 16-bit words); the runtime expands them into WRAM.
 * World map is level 80. tset points at the preload list described above.
 */
typedef struct CkLevelEntry_T {
    u8 levelNum;          /* Keen level number (1..16, 80=world map, 81..) */
    u8 pad;
    u16 compSize;         /* bytes of CRLE payload */
    const u8 *data;       /* ROM pointer to CRLE payload */
    const u16 *tset;      /* ROM pointer: [0]=count, [1..count]=tile ids */
} CkLevelEntry;

extern const CkLevelEntry ck_levels[]; /* terminated by levelNum == 0xFF */

/* ---- Sprites -----------------------------------------------------------
 * Base frames only (EGA shift copies dropped). Each frame's char data is
 * laid out as consecutive OAM parts; a part is 16x16 (4 chars, 128 B) or
 * 32x32 (16 chars, 512 B), chars in name-table row order for
 * oamInitDynamicSprite streaming. Pixels are palette-local indices,
 * 0 = transparent.
 */
typedef struct CkSpritePart_T {
    s8 dx, dy;            /* pixel offset of part from frame origin       */
    u8 large;             /* 0 = 16x16, 1 = 32x32                         */
    u8 chrOfs;            /* offset into frame chr data, in 128-byte units */
} CkSpritePart;

typedef struct CkSpriteFrame_T {
    const u8 *chr;        /* ROM pointer to this frame's char data        */
    u16 chrLen;           /* bytes                                        */
    u8 wPx, hPx;          /* original EGA sprite pixel size               */
    u8 palId;             /* OBJ palette 0..7 assigned at bake            */
    u8 nParts;
    const CkSpritePart *parts;
} CkSpriteFrame;

extern const CkSpriteFrame ck_sprite_frames[];
extern const u16 ck_sprite_frame_count;

/* ---- Fonts / UI bitmaps -------------------------------------------------
 * ck_font_chr: 256 glyphs, SNES 2bpp, 16 B/glyph, glyph index == char index.
 * UI bitmaps: 4bpp char strips, row-major, plus dimensions in tiles.
 */
extern const u8 ck_font_chr[];        /* 4096 bytes, 2bpp */

typedef struct CkBitmap_T {
    const u8 *chr;        /* 4bpp chars, row-major */
    u8 wTiles, hTiles;
} CkBitmap;

extern const CkBitmap ck_bitmaps[];   /* indexed by Keen bmp index */
extern const u16 ck_bitmap_count;

/* Full screens (TITLE, FINALE, PREVIEW): deduped 4bpp chr + 64x32 wla map */
typedef struct CkFullScreen_T {
    const u8 *chr;
    u16 chrLen;
    const u16 *map;       /* 64x32 tilemap words (4096 bytes)             */
} CkFullScreen;

extern const CkFullScreen ck_screen_title;
/* finale/preview screens follow the same shape; emitted per episode */

/* ---- Palettes -----------------------------------------------------------
 * ck_pal_ega: 16 bgr555 words (EGA identity, BG palette 0).
 * ck_pal_flash: 4 x 17 bgr555 words (EXE flash palettes + border color).
 * ck_obj_pals: 8 x 16 bgr555 words (bake-binned OBJ palettes; entry 0 of
 *              each is unused/transparent).
 */
extern const u16 ck_pal_ega[16];
extern const u16 ck_pal_flash[4][17];
extern const u16 ck_obj_pals[8][16];

/* ---- EXE image ----------------------------------------------------------
 * Whole unlzexe'd KEEN<n>.EXE. Placed in consecutive HiROM banks by the
 * generated asm so 24-bit pointer arithmetic (exeImage + OFFSET) is linear.
 * TILEINFO/rnd/char_map/points/win/endscreen tables bind at fixed offsets
 * exactly as src/episodes/episode<n>_engine.c does.
 */
extern const u8 ck_exe_image[];
extern const u32 ck_exe_image_len;

/* ---- Sounds --------------------------------------------------------------
 * ck_sounds_bin: uploaded verbatim to SPC RAM at boot.
 *   header: u16 count; then count * {u16 tickStreamOfs, u8 priority, u8 pad}
 *   tick streams: u16 per 6.87ms tick: 0x0000 = rest, 0xFFFF = end,
 *   else bits13..0 = DSP VxPITCH value, bit14 = use 8-sample loop sample
 *   (else 32-sample loop).
 * ck_brr_square32/8: filter-0 BRR square-wave samples with loop flags.
 */
extern const u8 ck_sounds_bin[];
extern const u16 ck_sounds_bin_len;
extern const u8 ck_brr_square32[];
extern const u8 ck_brr_square8[];

/* ---- Texts ---------------------------------------------------------------
 * Preprocessed story/help/end texts (0x1F->CR transform applied, 0x1A
 * terminated), word-wrapped at runtime.
 */
extern const char ck_text_story[];
extern const char ck_text_help[];   /* may be empty for some episodes */
extern const char ck_text_end[];
extern const char ck_text_previews[]; /* ep1 only; empty otherwise */

#endif /* CK_SNES_DATA_FORMAT_H */
