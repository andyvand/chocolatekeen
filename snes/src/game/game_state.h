/* Minimal SNES GameState for M3 (player physics/movement). Field names
 * follow src/core/globals.h GameState_T / GameProfile_T so the physics
 * and player code stays diffable against the desktop tree. sprite_sync
 * is not duplicated: game code reads ck_sprite_sync (engine/timer.h).
 */
#ifndef CK_SNES_GAME_STATE_H
#define CK_SNES_GAME_STATE_H

#include <snes.h>

/* ---- per-episode binding constants (src/episodes/episode<n>.h) ------- */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
#define CK_TILENUM            611
#define CK_RND_VALS_OFFSET    0x181A9UL
#define CK_FIBS17_OFFSET      0x182D1UL
#define CK_POINTS_TBL_OFFSET  0x15076UL
#define CK_OBJ_KEEN           1
#define CK_OBJ_KEENSHOT       10
#define CK_OBJ_ONEBEFORESHOT  9
#define CK_OBJ_ZAPZOT         13
#define CK_OBJ_DEAD           14
#define CK_SPR_KEENSHOT       108
#define CK_SPR_SHOTSPLASHR    110
#define CK_SPR_SHOTSPLASHL    111
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
#define CK_TILENUM            689
#define CK_RND_VALS_OFFSET    0x1C8B5UL
#define CK_FIBS17_OFFSET      0x1C9DDUL
#define CK_POINTS_TBL_OFFSET  0x19FC2UL /* CVort2_POINTS_TABLE_OFFSET */
#define CK_OBJ_KEEN           1
#define CK_OBJ_KEENSHOT       10
#define CK_OBJ_ONEBEFORESHOT  9
#define CK_OBJ_ZAPZOT         12
#define CK_OBJ_DEAD           13
#define CK_SPR_KEENSHOT       122
#define CK_SPR_SHOTSPLASHR    124
#define CK_SPR_SHOTSPLASHL    125
#else
#define CK_TILENUM            715
#define CK_RND_VALS_OFFSET    0x1EBA5UL
#define CK_FIBS17_OFFSET      0x1ECCDUL
#define CK_POINTS_TBL_OFFSET  0x1C083UL /* CVort3_POINTS_TABLE_OFFSET (odd ok) */
#define CK_OBJ_KEEN           1
#define CK_OBJ_KEENSHOT       15
#define CK_OBJ_ONEBEFORESHOT  14
#define CK_OBJ_ZAPZOT         18
#define CK_OBJ_DEAD           19
#define CK_SPR_KEENSHOT       102
#define CK_SPR_SHOTSPLASHR    105
#define CK_SPR_SHOTSPLASHL    106
#endif

/* Keen frame ids that differ between episodes: ep3 has no spr_keensback,
 * shifting keengetsup and all map-Keen frames down by one (see the
 * CVort{2,3}_spr_* enums in src/episodes/episode{2,3}.h). */
#define CK_SPR_KEENSICLE      0x1C
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
#define CK_SPR_KEENGETSUP     0x1E
#define CK_SPR_MAPKEEN_R      0x1F
#define CK_SPR_MAPKEEN_D      0x23
#define CK_SPR_MAPKEEN_L      0x27
#define CK_SPR_MAPKEEN_U      0x2B
#else
#define CK_SPR_KEENGETSUP     0x1F
#define CK_SPR_MAPKEEN_R      0x20
#define CK_SPR_MAPKEEN_D      0x24
#define CK_SPR_MAPKEEN_L      0x28
#define CK_SPR_MAPKEEN_U      0x2C
#endif

#define CK_OBJ_NULL 0

enum { CK_LEVEL_END_DIE = 0, CK_LEVEL_END_EXIT, CK_LEVEL_END_SECRET,
       CK_LEVEL_END_TANTALUS /* ep2: Keen pulled the tantalus switch */ };

/* PC-speaker sounds on the SPC700 (src/engine/audio.c). Keen sound
 * numbers are 1-based like the DOS engine; the priority gate lives in
 * ck_audio_play. finishCurSound blocked until completion on DOS; on
 * SNES the driver plays out asynchronously, so it is a no-op. */
#include "engine/audio.h"
#define CVort_engine_setCurSound(n)    ck_audio_play(n)
#define CVort_engine_finishCurSound()  ((void)0)

/* world units: 4096 per 16px tile, 256 per pixel */
#define CK_T2W(t) ((s32)(t) << 12)
#define CK_W2T(v) ((s16)((v) >> 12))

typedef struct CkGameInput_T {
    u16 direction;              /* 0=N 1=NE 2=E ... 7=NW, 8=center */
    u16 but1jump, but2pogo;
} CkGameInput;

typedef struct CkGameState_T {
    s16 god_mode, keen_invincible;
    s16 level_finished;         /* CK_LEVEL_END_* */
    u16 current_level;
    s16 keen_facing;
    u8 rnd;
    s16 keen_switch;
    /* world map (M5, desktop globals.h GameState_T names) */
    u16 on_world_map;
    u16 wmap_sprite_on;         /* raw sprite-plane value Keen triggered */
    u16 wmap_col;               /* 0x8000: sprite-plane blocking mask    */
    u16 lights;                 /* ep2 lights-out mechanic (1 = on)      */
    s16 spark_counter;          /* ep3 mangling machine sparks shot      */
} CkGameState;

typedef struct CkGameProfile_T {
    u16 stuff[9];               /* [3] = pogo stick, [5..8] = keys */
    u16 levels[16];             /* 1 = level completed (worldmap flow) */
    u16 targets[8];             /* ep2: cities saved (tantalus touched) */
    s16 lives;
    u16 ammo;
    s32 score;
    s32 extra_life_pts;
} CkGameProfile;

extern CkGameState g_game;
extern CkGameProfile keen_gp;
extern CkGameInput ck_input_new, ck_input_old;

/* scroll/camera and map bounds in world units (desktop globals.h names) */
extern s32 scroll_x, scroll_y;
extern s32 scroll_x_min, scroll_y_min, scroll_x_max, scroll_y_max;
extern s32 ceiling_x, ceiling_y;
extern s32 map_width, map_height;
extern s16 scroll_x_tile, scroll_y_tile;
extern s16 keen_tileX, keen_tileY;

/* low word of ck_ticks, latched once per frame (cheap 16-bit reads) */
extern u16 ck_ticks_lo;

/* TILEINFO arrays bound from the baked ck_tileinfo blob (order
 * Anim,Type,UEdge,REdge,DEdge,LEdge as in episode1_engine.c). */
extern const u16 *TILEINFO_Anim;
extern const s16 *TILEINFO_Type;
extern const s16 *TILEINFO_UEdge;
extern const s16 *TILEINFO_REdge;
extern const s16 *TILEINFO_DEdge;
extern const s16 *TILEINFO_LEdge;

/* Baked sprite hitboxes: 4 shift copies x {l,u,r,b} per logical frame
 * (emitted into sprites.frag.c by scripts/snes_gfx_host.c). */
extern const s16 ck_sprite_hitboxes[];

void ck_game_state_init(void);      /* once at boot */
void ck_profile_new(void);          /* fresh game profile (new game)   */
s16 CVort_get_random(void);
void CVort_add_score(s16 points);

/* gameplay.c: jump height PRNG (CVort_setup_jump_heights / _calc_...) */
void CVort_setup_jump_heights(u16 seed);
s16 CVort_calc_jump_height(u16 max_height);

#endif
