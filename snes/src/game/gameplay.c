/* gameplay.c - SNES transcription of the in-level loop from
 * src/game/gameplay.c (CVort_draw_level and its static helpers) plus
 * the pieces of CVort_load_level_data / CVort1_init_level /
 * CVort_update_sprite_hitbox the player logic needs.
 *
 * Think/contact function pointers are u8 ids dispatched through
 * switches (816-tcc: no function pointers).
 */
#include "game/game_state.h"
#include "game/sprites.h"
#include "game/physics.h"
#include "game/keen.h"
#include "game/episode1.h"
#include "game/episode2.h"
#include "game/episode3.h"
#include "game/gameplay.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/msprite.h"
#include "engine/input.h"
#include "engine/timer.h"
#include "data_format.h"
#include "snes_data_gen.h"

CkEntities g_entities;

#define TS g_entities.temp_sprite

/* ---- small struct helpers ------------------------------------------- */
extern void *memcpy(void *s1, const void *s2, u16 n);  /* tcc libc, asm */

static void ck_sprite_copy(CkSprite *dst, const CkSprite *src)
{
    memcpy(dst, src, sizeof(CkSprite));
}

static void ck_sprite_zero(CkSprite *s)
{
    u8 i;
    u8 *d = (u8 *)s;
    for (i = 0; i < sizeof(CkSprite); i++)
        d[i] = 0;
}

static void ck_body_zero(CkBody *b)
{
    u8 i;
    u8 *d = (u8 *)b;
    for (i = 0; i < sizeof(CkBody); i++)
        d[i] = 0;
}

/* ---- gameplay.c: CVort_add_sprite ------------------------------------ */
s16 CVort_add_sprite(void)
{
    s16 spriteNum;
    for (spriteNum = 1;
         g_entities.sprites[spriteNum].type_ &&
         (spriteNum < g_entities.num_sprites);
         spriteNum++)
        ;
    if (spriteNum >= g_entities.num_sprites)
        g_entities.num_sprites++;
    ck_sprite_zero(&g_entities.sprites[spriteNum]);
    g_entities.sprites[spriteNum].think = CK_THINK_NOP;
    g_entities.sprites[spriteNum].contact = CK_CONTACT_NOP;
    g_entities.sprites[spriteNum].active = 1;
    return spriteNum;
}

/* ---- gameplay.c: CVort_add_body --------------------------------------- */
s16 CVort_add_body(void)
{
    s16 bodyNum;
    for (bodyNum = 0;
         g_entities.bodies[bodyNum].type_ &&
         (bodyNum < g_entities.num_bodies);
         bodyNum++)
        ;
    if (bodyNum >= CK_MAX_BODIES) /* SNES safety: cap at the array end */
        bodyNum = CK_MAX_BODIES - 1;
    else if (bodyNum >= g_entities.num_bodies)
        g_entities.num_bodies++;
    ck_body_zero(&g_entities.bodies[bodyNum]);
    g_entities.bodies[bodyNum].think = CK_BODY_NOP;
    return bodyNum;
}

/* ---- gameplay.c: CVort_update_sprite_hitbox -------------------------- */
volatile u8 ck_dbg_stage; /* live stage tracer (read via emulator RAM) */
volatile u16 ck_dbg_hits;  /* sticky counter: think_keen_ground entries  */

void CVort_update_sprite_hitbox(void)
{
    /* entry = frame*4 + (pos_x>>9)%4 (the EGA shift copy), 4 s16 each,
     * order l,u,r,b (see scripts/snes_gfx_host.c).
     * All arithmetic in split u16 halves with an explicit carry: 816-tcc
     * compiles s32 adds/shifts into stack-spill storms and per-bit shift
     * loops (measured ~11 scanlines per call for the plain-s32 body). */
    u16 idx;
    const s16 *h;
    u16 lo, hi, o, nlo, nhi;

    idx = (u16)((TS.frame << 4) + ((((u16 *)&TS.pos_x)[0] >> 7) & 0x0C));
    h = ck_sprite_hitboxes + idx;

    lo = ((u16 *)&TS.pos_x)[0];
    hi = ((u16 *)&TS.pos_x)[1];

    o = (u16)h[0];
    nlo = (u16)(lo + o);
    nhi = hi;
    if (o & 0x8000)
        nhi--;
    if (nlo < lo)
        nhi++;
    ((u16 *)&TS.box_x1)[0] = nlo;
    ((u16 *)&TS.box_x1)[1] = nhi;

    o = (u16)h[2];
    nlo = (u16)(lo + o);
    nhi = hi;
    if (o & 0x8000)
        nhi--;
    if (nlo < lo)
        nhi++;
    ((u16 *)&TS.box_x2)[0] = nlo;
    ((u16 *)&TS.box_x2)[1] = nhi;

    lo = ((u16 *)&TS.pos_y)[0];
    hi = ((u16 *)&TS.pos_y)[1];

    o = (u16)h[1];
    nlo = (u16)(lo + o);
    nhi = hi;
    if (o & 0x8000)
        nhi--;
    if (nlo < lo)
        nhi++;
    ((u16 *)&TS.box_y1)[0] = nlo;
    ((u16 *)&TS.box_y1)[1] = nhi;

    o = (u16)h[3];
    nlo = (u16)(lo + o);
    nhi = hi;
    if (o & 0x8000)
        nhi--;
    if (nlo < lo)
        nhi++;
    ((u16 *)&TS.box_y2)[0] = nlo;
    ((u16 *)&TS.box_y2)[1] = nhi;
}

/* ---- think/contact dispatch (replaces function pointers) ------------- */
static void ck_game_dispatch_think(void)
{
    switch (TS.think) {
        case CK_THINK_KEEN_GROUND:   CVort_think_keen_ground(); break;
        case CK_THINK_KEEN_JUMP:     CVort_think_keen_jump_ground(); break;
        case CK_THINK_KEEN_JUMP_AIR: CVort_think_keen_jump_air(); break;
        case CK_THINK_KEEN_POGO:     CVort_think_keen_pogo_ground(); break;
        case CK_THINK_KEEN_POGO_AIR: CVort_think_keen_pogo_air(); break;
        case CK_THINK_KEEN_SHOOT:    CVort_think_keen_shoot(); break;
        case CK_THINK_KEEN_EXIT:     CVort_think_keen_exit(); break;
        case CK_THINK_KEEN_DEATH:    CVort_think_keen_death(); break;
        case CK_THINK_DEAD_SPRITE:   CVort_think_dead_sprite(); break;
        case CK_THINK_KILL_SPRITE:   CVort_think_kill_sprite(); break;
        case CK_THINK_REMOVE_SPRITE: CVort_think_remove_sprite(); break;
        case CK_THINK_SHOT:          CVort_think_keengun(); break;
        case CK_THINK_ZAPZOT:        CVort_think_zapzot(); break;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
        case CK_THINK_KEEN_FROZEN:   CVort1_think_keen_frozen(); break;
        case CK_THINK_YORP_WALK:     CVort1_think_yorp_walk(); break;
        case CK_THINK_YORP_LOOK:     CVort1_think_yorp_look(); break;
        case CK_THINK_YORP_STUNNED:  CVort1_think_yorp_stunned(); break;
        case CK_THINK_GARG_MOVE:     CVort1_think_garg_move(); break;
        case CK_THINK_GARG_LOOK:     CVort1_think_garg_look(); break;
        case CK_THINK_BUTLER_WALK:   CVort1_think_butler_walk(); break;
        case CK_THINK_BUTLER_TURN:   CVort1_think_butler_turn(); break;
        case CK_THINK_TANK_MOVE:     CVort1_think_tankbot_move(); break;
        case CK_THINK_TANK_SPAWN:    CVort1_think_tankbot_spawn(); break;
        case CK_THINK_TANK_TURN:     CVort1_think_tankbot_turn(); break;
        case CK_THINK_TANK_SHOOT:    CVort1_think_tankbot_shoot(); break;
        case CK_THINK_CUBE:          CVort1_think_cube(); break;
        case CK_THINK_CUBETTE:       CVort1_think_cubette_flight(); break;
        case CK_THINK_VORT_WALK:     CVort_think_vorticon_walk(); break;
        case CK_THINK_VORT_JUMP:     CVort_think_vorticon_jump(); break;
        case CK_THINK_VORT_SEARCH:   CVort_think_vorticon_search(); break;
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
        case CK_THINK_KEEN_STUNNED:  CVort_think_keen_stunned(); break;
        case CK_THINK_VORT_WALK:     CVort_think_vorticon_walk(); break;
        case CK_THINK_VORT_JUMP:     CVort_think_vorticon_jump(); break;
        case CK_THINK_VORT_SEARCH:   CVort_think_vorticon_search(); break;
        case CK_THINK_YOUTH_WALK:    CVort_think_youth_walk(); break;
        case CK_THINK_YOUTH_JUMP:    CVort_think_youth_jump(); break;
        case CK_THINK_ELITE_WALK:    CVort2_think_elite_walk(); break;
        case CK_THINK_ELITE_SHOOT:   CVort2_think_elite_shoot(); break;
        case CK_THINK_ELITE_JUMP:    CVort2_think_elite_jump(); break;
        case CK_THINK_GUARD_MOVE:    CVort2_think_guardbot_move(); break;
        case CK_THINK_GUARD_SHOOT:   CVort2_think_guardbot_shoot(); break;
        case CK_THINK_GUARD_TURN:    CVort2_think_guardbot_turn(); break;
        case CK_THINK_SCRUB_LEFT:    CVort2_think_scrub_walk_left(); break;
        case CK_THINK_SCRUB_DOWN:    CVort2_think_scrub_walk_down(); break;
        case CK_THINK_SCRUB_RIGHT:   CVort2_think_scrub_walk_right(); break;
        case CK_THINK_SCRUB_UP:      CVort2_think_scrub_walk_up(); break;
        case CK_THINK_SCRUB_FALL:    CVort2_think_scrub_fall(); break;
        case CK_THINK_PLATFORM_MOVE: CVort2_think_platform_move(); break;
        case CK_THINK_PLATFORM_TURN: CVort2_think_platform_turn(); break;
        case CK_THINK_TANTALUS:      CVort2_think_tantalus(); break;
#else
        case CK_THINK_KEEN_STUNNED:  CVort_think_keen_stunned(); break;
        case CK_THINK_VORT_WALK:     CVort_think_vorticon_walk(); break;
        case CK_THINK_VORT_JUMP:     CVort_think_vorticon_jump(); break;
        case CK_THINK_VORT_SEARCH:   CVort_think_vorticon_search(); break;
        case CK_THINK_YOUTH_WALK:    CVort_think_youth_walk(); break;
        case CK_THINK_YOUTH_JUMP:    CVort_think_youth_jump(); break;
        case CK_THINK_MOM_WALK:      CVort3_think_vortimom_walk(); break;
        case CK_THINK_MOM_SHOOT:     CVort3_think_vortimom_shoot(); break;
        case CK_THINK_MOMSHOT:       CVort3_think_vortimomshot(); break;
        case CK_THINK_MEEP_WALK:     CVort3_think_meep_walk(); break;
        case CK_THINK_MEEP_SHOOT:    CVort3_think_meep_shoot(); break;
        case CK_THINK_MEEPSHOT:      CVort3_think_meepshot(); break;
        case CK_THINK_NINJA_STAND:   CVort3_think_vortininja_stand(); break;
        case CK_THINK_NINJA_JUMP:    CVort3_think_vortininja_jump(); break;
        case CK_THINK_FOOB_WALK:     CVort3_think_foob_walk(); break;
        case CK_THINK_FOOB_RUN:      CVort3_think_foob_run(); break;
        case CK_THINK_FOOB_SCARED:   CVort3_think_foob_scared(); break;
        case CK_THINK_JACK:          CVort3_think_jack(); break;
        case CK_THINK_BALL:          CVort3_think_ball(); break;
        case CK_THINK_PLATFORM_MOVE: CVort3_think_platform_move(); break;
        case CK_THINK_PLATFORM_TURN: CVort3_think_platform_turn(); break;
        case CK_THINK_ENEMYSHOT:     CVort3_think_enemyshot(); break;
        case CK_THINK_SPARK:         CVort3_think_spark(); break;
        case CK_THINK_HEART:         CVort3_think_heart(); break;
#endif
        default:                     break; /* CK_THINK_NOP */
    }
}

static void ck_game_dispatch_contact(CkSprite *curr, CkSprite *other)
{
    switch (curr->contact) {
        case CK_CONTACT_KEENGUN:  CVort_contact_keengun(curr, other); break;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
        case CK_CONTACT_KEEN:     CVort1_contact_keen(curr, other); break;
        case CK_CONTACT_VORTICON: CVort_contact_vorticon(curr, other); break;
        case CK_CONTACT_TANKSHOT: CVort_contact_tankshot(curr, other); break;
        case CK_CONTACT_YORP:     CVort1_contact_yorp(curr, other); break;
        case CK_CONTACT_GARG:     CVort1_contact_garg(curr, other); break;
        case CK_CONTACT_BUTLER:   CVort1_contact_butler(curr, other); break;
        case CK_CONTACT_CUBE:     CVort1_contact_cube(curr, other); break;
        case CK_CONTACT_CHAIN:    CVort1_contact_chain(curr, other); break;
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
        case CK_CONTACT_KEEN:     CVort2_contact_keen(curr, other); break;
        case CK_CONTACT_VORTICON: CVort_contact_vorticon(curr, other); break;
        case CK_CONTACT_TANKSHOT: CVort_contact_tankshot(curr, other); break;
        case CK_CONTACT_YOUTH:    CVort_contact_youth(curr, other); break;
        case CK_CONTACT_ELITE:    CVort2_contact_elite(curr, other); break;
        case CK_CONTACT_GUARD:    CVort2_contact_guardbot(curr, other); break;
        case CK_CONTACT_SCRUB:    CVort2_contact_scrub(curr, other); break;
        case CK_CONTACT_TANTALUS: CVort2_contact_tantalus(curr, other); break;
#else
        case CK_CONTACT_KEEN:     CVort3_contact_keen(curr, other); break;
        case CK_CONTACT_VORTICON: CVort_contact_vorticon(curr, other); break;
        case CK_CONTACT_YOUTH:    CVort_contact_youth(curr, other); break;
        case CK_CONTACT_MOM:      CVort3_contact_vortimom(curr, other); break;
        case CK_CONTACT_MOMSHOT:  CVort3_contact_vortimomshot(curr, other); break;
        case CK_CONTACT_MEEP:     CVort3_contact_meep(curr, other); break;
        case CK_CONTACT_NINJA:    CVort3_contact_vortininja(curr, other); break;
        case CK_CONTACT_FOOB:     CVort3_contact_foob(curr, other); break;
        case CK_CONTACT_SPARK:    CVort3_contact_spark(curr, other); break;
        case CK_CONTACT_HEART:    CVort3_contact_heart(curr, other); break;
#endif
        default:                  break;
    }
}

/* ---- body think dispatch ---------------------------------------------- */
static void ck_game_dispatch_body(CkBody *b)
{
    switch (b->think) {
        case CK_BODY_SLIDE_DOOR:     CVort_body_slide_door(b); break;
        case CK_BODY_BRIDGE_EXTEND:  CVort_body_bridge_extend(b); break;
        case CK_BODY_BRIDGE_RETRACT: CVort_body_bridge_retract(b); break;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
        case CK_BODY_ICE_CANNON:     CVort1_body_ice_cannon(b); break;
        case CK_BODY_SHOT_CHAIN:     CVort1_body_shot_chain(b); break;
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
        case CK_BODY_DESTROY_TANTALUS: CVort2_body_destroy_tantalus(b); break;
#else
        case CK_BODY_ENEMYGUN_R:     CVort3_think_enemygun_right(b); break;
        case CK_BODY_ENEMYGUN_D:     CVort3_think_enemygun_down(b); break;
        case CK_BODY_MANGLING_ARM:   CVort3_think_mangling_arm(b); break;
        case CK_BODY_MANGLING_LEG_MOVE:
            CVort3_think_mangling_leg_moving(b); break;
        case CK_BODY_MANGLING_LEG_WAIT:
            CVort3_think_mangling_leg_awaiting(b); break;
        case CK_BODY_MANGLING_ARM_DESTRUCT:
            CVort3_think_mangling_arm_destruct(b); break;
        case CK_BODY_MANGLING_DESTRUCT:
            CVort3_think_mangling_destruct(b); break;
#endif
        default:                     break; /* CK_BODY_NOP */
    }
}

static void level_update_bodies(void)
{
    s16 i;
    for (i = 0; i < g_entities.num_bodies; i++)
        if (g_entities.bodies[i].type_)
            ck_game_dispatch_body(&g_entities.bodies[i]);
}

/* ---- gameplay.c: run_sprite_think / activate_and_think_sprite -------- */
static u16 s_frameAtThink;   /* TS.frame before dispatch (elision check) */

static void run_sprite_think(void)
{
    ck_dbg_stage = 0x20;
    CVort_update_sprite_hitbox();
    TS.del_x = TS.del_y = 0;
    s_frameAtThink = TS.frame;
    ck_dbg_stage = 0x21;
    ck_game_dispatch_think();
    ck_dbg_stage = 0x22;
    {
        /* second hitbox pass only if the think moved or re-animated the
         * sprite (box depends only on pos and frame) */
        u8 redo = 0;
        if (TS.del_x)
            redo = 1;
        if (TS.del_y)
            redo = 1;
        if (TS.frame != s_frameAtThink)
            redo = 1;
        if (redo) {
            TS.pos_x += TS.del_x;
            TS.pos_y += TS.del_y;
            CVort_update_sprite_hitbox();
        }
    }
    ck_dbg_stage = 0x23;
}

static void activate_and_think_sprite(s16 sprite_counter)
{
    s16 var_4, var_6;
    if (!TS.active) {
        var_4 = CK_W2T(TS.pos_x);
        var_6 = CK_W2T(TS.pos_y);
        if ((scroll_x_tile - 2 < var_4) && (scroll_y_tile - 2 < var_6) &&
            (scroll_x_tile + 23 > var_4) && (scroll_y_tile + 12 > var_6)) {
            TS.active = 1;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
            /* ep1 on-wake adjustment: a stunned Yorp wakes up looking */
            if (TS.think == CK_THINK_YORP_STUNNED) {
                TS.think = CK_THINK_YORP_LOOK;
                TS.time = 0;
            }
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
            /* ep3 on-wake adjustment: a fleeing Foob calms down */
            if (TS.think == CK_THINK_FOOB_RUN) {
                TS.think = CK_THINK_FOOB_WALK;
                if (TS.pos_x > g_entities.sprites[0].pos_x)
                    TS.vel_x = 50;
                else
                    TS.vel_x = -50;
            }
#endif
            run_sprite_think();
        }
    } else {
        /* 816-tcc miscompiles "!a || !b" short-circuits (the true case
         * jumps past the body) - keep these as separate ifs. */
        u8 shouldThink = 0;
        if (sprite_counter == 0)
            shouldThink = 1;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
        else if (TS.type_ == 7)   /* CVort2_obj_platform always moves */
            shouldThink = 1;
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
        else if (TS.type_ == 10)  /* CVort3_obj_platform always moves */
            shouldThink = 1;
#endif
        else if (!CVort_sprite_active_screen())
            shouldThink = 1;
        if (shouldThink)
            run_sprite_think();
    }
}

/* CK_W2T on an s32 lvalue without 816-tcc's 32-bit shift helper call:
 * positions are positive and < 2^24, so tile = bits 12..27 via two
 * 16-bit constant shifts. */
#define CK_TILE_OF(f) \
    ((s16)((((u16 *)&(f))[0] >> 12) | (((u16 *)&(f))[1] << 4)))

static u8 s_wakePhase;   /* alternate-frame stagger for sleeper checks */

static void level_update_sprites(void)
{
    s16 sprite_counter;
    CkSprite *s = g_entities.sprites;   /* pointer walk: [i] indexing costs
                                           a mul-helper call per access */
    s_wakePhase ^= 1;
    for (sprite_counter = 0; sprite_counter < g_entities.num_sprites;
         sprite_counter++, s++) {
        if (!s->type_)
            continue;
        if (!s->active) {
            /* stagger: scan half the sleepers per frame (a sprite wakes
             * at most one frame late; the scan itself costs scanlines) */
            if (((u8)sprite_counter & 1) != s_wakePhase)
                continue;
            /* Sleeping off-screen sprite: the wake check reads only
             * pos, so run it here and skip the two 52-byte temp copies
             * unless it actually wakes (big win with many enemies). */
            s16 tx = CK_TILE_OF(s->pos_x);
            s16 ty = CK_TILE_OF(s->pos_y);
            u8 wake = 1;
            if (scroll_x_tile - 2 >= tx)
                wake = 0;
            else if (scroll_y_tile - 2 >= ty)
                wake = 0;
            else if (scroll_x_tile + 23 <= tx)
                wake = 0;
            else if (scroll_y_tile + 12 <= ty)
                wake = 0;
            if (!wake)
                continue;
        }
        ck_sprite_copy(&TS, s);
        activate_and_think_sprite(sprite_counter);
        ck_sprite_copy(s, &TS);
    }
}

static void level_detect_collisions(void)
{
    s16 sprite_counter, var_A;
    CkSprite *a = g_entities.sprites;   /* pointer walk (see above) */
    CkSprite *b;
    for (sprite_counter = 0; sprite_counter < g_entities.num_sprites;
         sprite_counter++, a++) {
        /* separate ifs: 816-tcc miscompiles "!a || !b" (see above) */
        if (!a->type_)
            continue;
        if (!a->active)
            continue;
        b = a + 1;
        for (var_A = sprite_counter + 1; var_A < g_entities.num_sprites;
             var_A++, b++) {
            if (!b->type_)
                continue;
            if (!b->active)
                continue;
            if (!CVort_detect_sprite_col(a, b))
                continue;
            ck_game_dispatch_contact(a, b);
            ck_game_dispatch_contact(b, a);
        }
    }
}

/* Draw in reversed order, like the original (Keen last = on top). */
static void level_draw_sprites(void)
{
    s16 sprite_counter;
    s16 camPx = (s16)ck_cam_px, camPy = (s16)ck_cam_py;
    CkSprite *s = &g_entities.sprites[g_entities.num_sprites - 1];
    ck_msprite_begin();
    for (sprite_counter = g_entities.num_sprites - 1; sprite_counter >= 0;
         sprite_counter--, s--) {
        if (s->type_ && s->active) {
            s16 px = (s16)((((u16 *)&s->pos_x)[0] >> 8) |
                           (((u16 *)&s->pos_x)[1] << 8));
            s16 py = (s16)((((u16 *)&s->pos_y)[0] >> 8) |
                           (((u16 *)&s->pos_y)[1] << 8));
            ck_msprite_draw(s->frame, (s16)(px - camPx), (s16)(py - camPy));
        }
    }
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    /* level_draw_invincibility (desktop gameplay.c:806): halo over Keen
     * while the ankh is active, blinking as it runs out. */
    {
        u8 haloOn = 0;
        if (g_game.keen_invincible)
            haloOn = 1;
        if (g_game.god_mode)
            haloOn = 1;
        if (haloOn) {
            u16 haloFrame = 0xFFFF;
            if (g_game.keen_invincible > 250)
                haloFrame = (u16)(((ck_ticks_lo >> 4) & 1) + 0x3D);
            else if ((ck_ticks_lo >> 4) & 1)
                haloFrame = (u16)(((ck_ticks_lo >> 5) & 1) + 0x3D);
            if (haloFrame != 0xFFFF)
                ck_msprite_draw(haloFrame,
                    (s16)((s16)((g_entities.sprites[0].pos_x - 0x800) >> 8) - camPx),
                    (s16)((s16)((g_entities.sprites[0].pos_y - 0x800) >> 8) - camPy));
            g_game.keen_invincible -= (s16)ck_sprite_sync;
            if (g_game.keen_invincible < 0)
                g_game.keen_invincible = 0;
        }
    }
#endif
    ck_msprite_end();
}

/* ---- level session ---------------------------------------------------- */

/* CVort_load_level_data tail: scroll bounds from the map header. */
void ck_level_setup_bounds(void)
{
    scroll_x_min = scroll_y_min = 0x2000;
    scroll_x_max = CK_T2W(map_width_tile - 0x16);
    map_width = CK_T2W(map_width_tile - 2);
    map_height = CK_T2W(map_height_tile);
    scroll_y_max = CK_T2W(map_height_tile - 0xF) + 0x800;
    ceiling_x = CK_T2W(map_width_tile - 3);
    ceiling_y = map_height;
}

/* gameplay.c level_init(): player slot 0 */
static void level_init(void)
{
    ck_sprite_zero(&g_entities.sprites[0]);
    g_entities.sprites[0].type_ = CK_OBJ_KEEN;
    g_entities.sprites[0].think = CK_THINK_KEEN_GROUND;
    g_entities.sprites[0].contact = CK_CONTACT_KEEN;
    g_entities.sprites[0].frame = 4;
    g_entities.sprites[0].active = 1;
    g_entities.num_sprites = 1;
    g_entities.num_bodies = 0;
    g_game.keen_facing = 1;
    g_game.level_finished = 0;
    g_game.god_mode = 0;
    g_game.keen_invincible = 0;
    g_game.keen_switch = 0;
}

/* CVort1_init_level spawn pass: map every sprite-plane value to its
 * enemy/body spawn (the value->type dispatch lives in episode1.c). */
static void level_spawn_pass(void)
{
    u16 currX, currY;
    u16 *sp = map_data_sprites;
    for (currY = 0; currY < map_height_tile; currY++)
        for (currX = 0; currX < map_width_tile; currX++) {
            u16 currSprite = *sp++;
            if (!currSprite)
                continue;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
            CVort1_spawn_plane_value(currSprite, currX, currY);
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
            CVort2_spawn_plane_value(currSprite, currX, currY);
#else
            CVort3_spawn_plane_value(currSprite, currX, currY);
#endif
        }
}

/* gameplay.c level_setup_camera() */
static void level_setup_camera(void)
{
    scroll_x = g_entities.sprites[0].pos_x - 0xA000L;
    scroll_y = g_entities.sprites[0].pos_y - 0x5000L;
    if (scroll_x < scroll_x_min)
        scroll_x = scroll_x_min;
    if (scroll_y < scroll_y_min)
        scroll_y = scroll_y_min;
    if (scroll_x > scroll_x_max)
        scroll_x = scroll_x_max;
    if (scroll_y > scroll_y_max)
        scroll_y = scroll_y_max;
    scroll_x_tile = CK_W2T(scroll_x);
    scroll_y_tile = CK_W2T(scroll_y);
    ck_cam_px = (u16)(scroll_x >> 8);
    ck_cam_py = (u16)(scroll_y >> 8);
}

void ck_gameplay_level_start(u8 levelnum)
{
    g_game.current_level = levelnum;
    level_init();
    g_game.lights = 1;           /* level_init_screen: lights back on */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    ck_lights_reset();           /* restore bright palettes if needed */
#endif
    ck_level_setup_bounds();
    level_spawn_pass();
    level_setup_camera();
    ck_input_old.direction = 8;
    ck_input_old.but1jump = ck_input_old.but2pogo = 0;
}

/* g_ck_input (engine pad state) -> the Vorticons GameInput shape. The
 * fire flag maps to both buttons pressed, as on DOS. */
void ck_build_game_input(void)
{
    static const u8 dirLut[16] = {
        /* index bits: 1=up 2=down 4=left 8=right */
        8, 0, 4, 8,   /* -,   U,   D,   UD  */
        6, 7, 5, 6,   /* L,   UL,  DL,  UDL */
        2, 1, 3, 2,   /* R,   UR,  DR,  UDR */
        8, 0, 4, 8    /* LR combos fall back */
    };
    u8 bits = 0;
    if (g_ck_input.up)    bits |= 1;
    if (g_ck_input.down)  bits |= 2;
    if (g_ck_input.left)  bits |= 4;
    if (g_ck_input.right) bits |= 8;
    ck_input_new.direction = dirLut[bits];
    ck_input_new.but1jump = (u16)(g_ck_input.jump | g_ck_input.fire);
    ck_input_new.but2pogo = (u16)(g_ck_input.pogo | g_ck_input.fire);
}

/* gameplay.c level_handle_death (ep1 branch): Keen just died - forget
 * any ship part collected in this level. */
static void level_handle_death(void)
{
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    switch (g_game.current_level) {
        case 8:  keen_gp.stuff[1] = 0; break;
        case 16: keen_gp.stuff[2] = 0; break;
        case 3:  keen_gp.stuff[4] = 0; break;
        case 4:  keen_gp.stuff[0] = 0; break;
        default: break;
    }
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    /* un-rescue the city if it was rescued in this attempt */
    switch (g_game.current_level) {
        case 4:  keen_gp.targets[0] = 0; break;
        case 6:  keen_gp.targets[1] = 0; break;
        case 7:  keen_gp.targets[2] = 0; break;
        case 9:  keen_gp.targets[3] = 0; break;
        case 11: keen_gp.targets[4] = 0; break;
        case 13: keen_gp.targets[5] = 0; break;
        case 15: keen_gp.targets[6] = 0; break;
        case 16: keen_gp.targets[7] = 0; break;
        default: break;
    }
#endif
}

u8 ck_gameplay_frame(void)
{
    ck_ticks_lo = (u16)ck_ticks;
    ck_dbg_stage = 1;
    ck_input_update();
    ck_build_game_input();

    ck_dbg_stage = 2;
    level_update_sprites();
    ck_dbg_stage = 3;
    CVort_do_scrolling();
    ck_dbg_stage = 4;
    level_detect_collisions();
    scroll_x_tile = CK_W2T(scroll_x);
    scroll_y_tile = CK_W2T(scroll_y);
    ck_dbg_stage = 5;
    level_draw_sprites();
    ck_dbg_stage = 6;
    CVort_keen_bgtile_col();
    ck_dbg_stage = 7;
    level_update_bodies();
    ck_dbg_stage = 8;

    /* tile animation phase (drawScreen's anim_plane_i, anim_speed = 7) */
    ck_render_set_anim_phase((u8)(((ck_ticks_lo >> 7) & 6) >> 1));

    ck_input_old = ck_input_new;

    if (g_game.level_finished != CK_LEVEL_END_DIE) {
        /* CVort_draw_level epilogue: drop the level's keycards */
        keen_gp.stuff[5] = keen_gp.stuff[6] = 0;
        keen_gp.stuff[7] = keen_gp.stuff[8] = 0;
        return 1;
    }
    if (!g_entities.sprites[0].type_) { /* Keen died and left the screen */
        keen_gp.stuff[5] = keen_gp.stuff[6] = 0;
        keen_gp.stuff[7] = keen_gp.stuff[8] = 0;
        level_handle_death();
        /* level_finished stays CK_LEVEL_END_DIE; lives / game-over
         * policy is handled by the flow driver (gameflow.c). */
        return 1;
    }
    return 0;
}
