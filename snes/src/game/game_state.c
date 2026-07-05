/* Minimal GameState for M3 - see game_state.h. */
#include "game/game_state.h"
#include "engine/timer.h"
#include "engine/levelload.h"   /* ck_far_ofs */
#include "data_format.h"
#include "snes_data_gen.h"

CkGameState g_game;
CkGameProfile keen_gp;
CkGameInput ck_input_new, ck_input_old;

s32 scroll_x, scroll_y;
s32 scroll_x_min, scroll_y_min, scroll_x_max, scroll_y_max;
s32 ceiling_x, ceiling_y;
s32 map_width, map_height;
s16 scroll_x_tile, scroll_y_tile;
s16 keen_tileX, keen_tileY;

u16 ck_ticks_lo;

const u16 *TILEINFO_Anim;
const s16 *TILEINFO_Type;
const s16 *TILEINFO_UEdge;
const s16 *TILEINFO_REdge;
const s16 *TILEINFO_DEdge;
const s16 *TILEINFO_LEdge;

static const u8 *s_rnd_vals;

void ck_game_state_init(void)
{
    const s16 *ti = (const s16 *)ck_tileinfo;
    TILEINFO_Anim  = (const u16 *)ti;
    TILEINFO_Type  = ti + CK_TILENUM;
    TILEINFO_UEdge = ti + CK_TILENUM * 2;
    TILEINFO_REdge = ti + CK_TILENUM * 3;
    TILEINFO_DEdge = ti + CK_TILENUM * 4;
    TILEINFO_LEdge = ti + CK_TILENUM * 5;

    /* ck_exe_image spans multiple HiROM banks; ck_far_ofs does real
     * 32-bit pointer+offset (a plain "(u32)ptr + ofs" cast DROPS the
     * bank byte - see levelload.h). */
    s_rnd_vals = ck_far_ofs(ck_exe_image, CK_RND_VALS_OFFSET);

    g_game.rnd = 0;
    g_game.god_mode = 0;
    g_game.keen_invincible = 0;
    g_game.keen_switch = 0;
    g_game.on_world_map = 0;
    g_game.wmap_sprite_on = 0;
    g_game.wmap_col = 0x8000;
    g_game.lights = 1;
    g_game.spark_counter = 0;

    ck_profile_new();

    CVort_setup_jump_heights(0);
}

/* Fresh profile, as the desktop new-game path sets it up for KEEN1
 * (src/game/menus.c CVort_do_intro_and_menu: 4 lives, no ammo, no pogo,
 * nothing collected, no level done). */
void ck_profile_new(void)
{
    u8 i;
    keen_gp.lives = 4;
    keen_gp.ammo = 0;
    keen_gp.score = 0;
    keen_gp.extra_life_pts = 0;
    for (i = 0; i < 9; i++)
        keen_gp.stuff[i] = 0;
    for (i = 0; i < 16; i++)
        keen_gp.levels[i] = 0;
    for (i = 0; i < 8; i++)
        keen_gp.targets[i] = 0;
}

/* ---- gameplay.c: CVort_setup_jump_heights / CVort_calc_jump_height ----
 * jump_height_table[1..17] is seeded from the EXE's fibs table; the
 * desktop optionally mixes in wall-clock time (seed) which the SNES has
 * no source for, so seed==0 keeps the deterministic table (fine: the
 * DOS original allowed that too via setup_jump_heights(0)). */
static u16 jump_height_table[18];
static u16 spritejump_1, spritejump_2;

void CVort_setup_jump_heights(u16 seed)
{
    u8 i;
    const u8 *fibs;
#if CK_FIBS17_OFFSET
    fibs = ck_far_ofs(ck_exe_image, CK_FIBS17_OFFSET);
    for (i = 0; i < 17; i++)
        jump_height_table[i + 1] =
            (u16)((u16)fibs[i << 1] | ((u16)fibs[(i << 1) + 1] << 8));
#else
    for (i = 0; i < 17; i++)
        jump_height_table[i + 1] = (u16)(i * 89 + 1);
#endif
    jump_height_table[0] = 0;
    spritejump_1 = 0x22;
    spritejump_2 = 0xA;
    if (seed) {
        jump_height_table[17] = (u16)ck_ticks;
        jump_height_table[5] = (u16)(ck_ticks ^ (ck_ticks >> 16));
    }
    CVort_calc_jump_height(0xFFFF);
}

s16 CVort_calc_jump_height(u16 max_height)
{
    u16 loopVar1 = max_height, loopVar2 = 0xFFFF, result;
    while (!(loopVar1 & 0x8000)) {
        loopVar1 <<= 1;
        loopVar2 >>= 1;
    }
    result = (u16)(jump_height_table[spritejump_1 >> 1] +
                   jump_height_table[spritejump_2 >> 1] + 1);
    jump_height_table[spritejump_1 >> 1] = result;
    result += jump_height_table[0];
    jump_height_table[0] = result;
    if (spritejump_1 == 2)
        spritejump_1 = 0x22;
    else
        spritejump_1 -= 2;
    if (spritejump_2 == 2)
        spritejump_2 = 0x22;
    else
        spritejump_2 -= 2;

    result &= loopVar2;
    if (result > max_height)
        result >>= 1;
    return (s16)result;
}

s16 CVort_get_random(void)
{
    g_game.rnd++;
    return (s16)s_rnd_vals[g_game.rnd];
}

void CVort_add_score(s16 points)
{
    keen_gp.score += points;
    /* subtraction loop instead of the original 32-bit divide */
    while (keen_gp.score - keen_gp.extra_life_pts >= 20000L) {
        CVort_engine_setCurSound(0x1C);
        keen_gp.extra_life_pts += 20000L;
        keen_gp.lives++;
    }
}
