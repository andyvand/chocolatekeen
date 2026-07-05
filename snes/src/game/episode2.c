/* episode2.c - SNES transcription of src/episodes/episode2.c (sprite
 * spawns, Vortelite / Guard robot / Scrub / Platform / Tantalus thinks
 * and contacts, contact_keen, the tantalus destruction body) plus the
 * ep2-relevant parts of src/game/enemies.c (Vorticon, Vorticon youth,
 * tank shot - GAMEVER_KEEN2 paths), the lights-out palette mechanic
 * (src/game/ui.c CVort_lights_on/out) and a simplified
 * CVort2_draw_earth_explode ending.
 *
 * 816-tcc rules observed: no negated short-circuit ORs, no zero-init
 * assumptions, positions s32, everything else s16/u16, map writes via
 * ck_map_set, CGRAM writes only under live vblank (ck_lights_vblank).
 */
#include "game/game_state.h"
#include "game/sprites.h"
#include "game/physics.h"
#include "game/keen.h"
#include "game/gameplay.h"
#include "game/episode2.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/msprite.h"
#include "engine/timer.h"
#include "data_format.h"
#include "snes_data_gen.h"

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2

#define TS g_entities.temp_sprite

/* object types (CVort2_objtype, src/episodes/episode2.h) */
#define CK2_OBJ_VORTICON  2
#define CK2_OBJ_YOUTH     3
#define CK2_OBJ_ELITE     4
#define CK2_OBJ_SCRUB     5
#define CK2_OBJ_GUARDBOT  6
#define CK2_OBJ_PLATFORM  7
#define CK2_OBJ_TANTALUS  8
#define CK2_OBJ_ENEMYSHOT 11

/* sprite frames (CVort2_spr_*, src/episodes/episode2.h) */
#define SPR2_TANTALUS1    58
#define SPR2_FIREARTH1    60
#define SPR2_EARTHCHUNK1  64
#define SPR2_LILCHUNK1    68
#define SPR2_YOUTHLEFT1   48
#define SPR2_YOUTHRIGHT1  52
#define SPR2_YOUTHLEFT4   51
#define SPR2_YOUTHRIGHT4  55
#define SPR2_YOUTHDIE1    56
#define SPR2_VORTLEFT1    74
#define SPR2_VORTRIGHT1   78
#define SPR2_VORTSTAND1   82
#define SPR2_VORTJUMPL    84
#define SPR2_VORTJUMPR    85
#define SPR2_VORTDIE1     86
#define SPR2_ELITELEFT1   88
#define SPR2_ELITERIGHT1  92
#define SPR2_ELITEFIREL   96
#define SPR2_ELITEFIRER   97
#define SPR2_ELITEJUMPR   98
#define SPR2_ELITEJUMPL   99
#define SPR2_ELITEDIE1    100
#define SPR2_SCRUBL1      102
#define SPR2_SCRUBU1      104
#define SPR2_SCRUBR1      106
#define SPR2_SCRUBD1      108
#define SPR2_SCRUBSHOT    110
#define SPR2_GUARDRIGHT1  112
#define SPR2_GUARDLEFT1   116
#define SPR2_GUARDSTAND1  120
#define SPR2_TANKSHOT     123
#define SPR2_PLATFORM1    126
#define SPR2_SPARK1       128

/* ---- shared spawns (enemies.c, GAMEVER_KEEN2 paths) -------------------- */

static void CVort_add_sprite_vorticon(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK2_OBJ_VORTICON;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_VORT_WALK;
    s->contact = CK_CONTACT_VORTICON;
    s->health = 1;               /* GAMEVER != KEEN1 */
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -90;
    else
        s->vel_x = 90;
    s->frame = SPR2_VORTSTAND1;
}

static void CVort_add_sprite_youth(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK2_OBJ_YOUTH;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_YOUTH_WALK;
    s->contact = CK_CONTACT_YOUTH;
    s->health = 1;
    if (g_entities.sprites[0].pos_x > s->pos_x)
        s->vel_x = 250;
    else
        s->vel_x = -250;
    s->frame = SPR2_YOUTHLEFT1;
}

/* CVort_add_sprite_tankshot (enemies.c:44, KEEN2: pos_y + 0x900) */
static void CVort_add_sprite_tankshot(s32 pos_x, s32 pos_y, s16 vel_x)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    u16 tx, ty;
    s->type_ = CK2_OBJ_ENEMYSHOT;
    s->pos_x = pos_x;
    s->pos_y = pos_y + 0x900;
    s->think = CK_THINK_SHOT;
    s->vel_x = vel_x;
    s->contact = CK_CONTACT_TANKSHOT;
    s->frame = SPR2_TANKSHOT;
    tx = (u16)CK_W2T(pos_x);
    ty = (u16)CK_W2T(pos_y);
    if (vel_x >= 0) {
        if (!TILEINFO_REdge[map_data_tiles[tx + ck_rowofs[ty + 1] + 1]])
            return;
    } else {
        if (!TILEINFO_LEdge[map_data_tiles[ck_rowofs[ty + 1] + tx]])
            return;
    }
    CVort_engine_setCurSound(0x25); /* snd_shothit */
    s->type_ = CK_OBJ_ZAPZOT;
    s->think = CK_THINK_ZAPZOT;
    s->time = 0;
    if (CVort_get_random() > 0x80)
        s->frame = CK_SPR_SHOTSPLASHL;
    else
        s->frame = CK_SPR_SHOTSPLASHR;
}

/* ---- episode spawns (CVort2_add_sprite_*) ------------------------------- */

static void CVort2_add_sprite_elite(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK2_OBJ_ELITE;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_ELITE_WALK;
    s->contact = CK_CONTACT_ELITE;
    s->health = 2;
    if (g_entities.sprites[0].pos_x > s->pos_x)
        s->vel_x = 100;
    else
        s->vel_x = -100;
    s->frame = SPR2_ELITELEFT1;
}

static void CVort2_add_sprite_scrub(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK2_OBJ_SCRUB;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_SCRUB_LEFT;
    s->contact = CK_CONTACT_SCRUB;
    s->frame = SPR2_SCRUBL1;
}

static void CVort2_add_sprite_guardbot(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK2_OBJ_GUARDBOT;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->vel_x = 100;
    s->think = CK_THINK_GUARD_MOVE;
    s->contact = CK_CONTACT_GUARD;
    s->health = 99;
    s->frame = SPR2_GUARDRIGHT1;
}

static void CVort2_add_sprite_platform(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK2_OBJ_PLATFORM;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY) - 0x400;
    s->think = CK_THINK_PLATFORM_MOVE;
    s->contact = CK_CONTACT_NOP;
    s->frame = SPR2_PLATFORM1;
    s->vel_x = 75;
}

static void CVort2_add_sprite_tantalus(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK2_OBJ_TANTALUS;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_TANTALUS;
    s->contact = CK_CONTACT_TANTALUS;
    s->frame = SPR2_SPARK1;
}

/* CVort2_init_level's per-cell spawn dispatch. */
void CVort2_spawn_plane_value(u16 currSprite, u16 tileX, u16 tileY)
{
    switch (currSprite) {
        case 1:
            CVort_add_sprite_vorticon(tileX, tileY);
            break;
        case 2:
            CVort_add_sprite_youth(tileX, tileY);
            break;
        case 3:
            CVort2_add_sprite_elite(tileX, tileY);
            break;
        case 4:
            CVort2_add_sprite_scrub(tileX, tileY);
            break;
        case 5:
            CVort2_add_sprite_guardbot(tileX, tileY);
            break;
        case 6:
            CVort2_add_sprite_platform(tileX, tileY);
            break;
        case 7:
            CVort2_add_sprite_tantalus(tileX, tileY);
            break;
        case 0xFF: /* Keen */
            g_entities.sprites[0].pos_x = CK_T2W(tileX);
            g_entities.sprites[0].pos_y = CK_T2W(tileY) + 0x800;
            break;
        default:
            break;
    }
}

/* ---- contact_keen ------------------------------------------------------- */

void CVort2_contact_keen(CkSprite *keen, CkSprite *contacted)
{
    switch (contacted->type_) {
        case CK2_OBJ_VORTICON:
        case CK2_OBJ_ELITE:
        case CK2_OBJ_GUARDBOT:
        case CK2_OBJ_ENEMYSHOT:
            CVort_kill_keen();
            break;
        case CK2_OBJ_YOUTH: /* the youth knocks Keen flat */
            if (keen->think == CK_THINK_KEEN_STUNNED)
                return;
            keen->think = CK_THINK_KEEN_STUNNED;
            keen->vel_x = contacted->vel_x;
            keen->vel_y = contacted->vel_y;
            keen->time = 400;
            break;
        case CK2_OBJ_SCRUB:
        case CK2_OBJ_PLATFORM:
            CVort_carry_keen(keen, contacted);
            break;
        default:
            break;
    }
}

/* ---- Vorticon (enemies.c, GAMEVER_KEEN2 paths) -------------------------- */

void CVort_think_vorticon_walk(void)
{
    s16 currDelta;
    u8 jumped;
    if (TS.vel_x > 0)
        TS.frame = SPR2_VORTRIGHT1;
    else
        TS.frame = SPR2_VORTLEFT1;
    TS.frame += (ck_ticks_lo >> 4) & 3;
    /* rand-consumption order matches the desktop && / else-if chain */
    jumped = 0;
    if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
        if (g_game.lights) { /* Vorticons never jump in the dark */
            TS.vel_y = -CVort_calc_jump_height(300);
            TS.think = CK_THINK_VORT_JUMP;
            jumped = 1;
        }
    }
    if (!jumped) {
        if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
            if (TS.vel_x == 90)
                TS.vel_x = 120;
            else if (TS.vel_x > 90) {
                if (TS.vel_x == 120)
                    TS.vel_x = 90;
            } else if (TS.vel_x == -120)
                TS.vel_x = -90;
            else if (TS.vel_x == -90)
                TS.vel_x = -120;
        }
    }
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (currDelta & 4)
        TS.vel_x = -90;
    if (currDelta & 1)
        TS.vel_x = 90;
    if (!(currDelta & 2)) /* KEEN2/3: walked off a ledge */
        TS.think = CK_THINK_VORT_JUMP;
}

void CVort_think_vorticon_jump(void)
{
    s16 currDelta;
    if (TS.vel_x > 0)
        TS.frame = SPR2_VORTJUMPL; /* sic - vanilla swaps them */
    else
        TS.frame = SPR2_VORTJUMPR;
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (currDelta & 2) {
        TS.think = CK_THINK_VORT_SEARCH;
        TS.time = 0;
    }
    if (currDelta & 4)
        TS.vel_x = -90;
    if (currDelta & 1)
        TS.vel_x = 90;
}

void CVort_think_vorticon_search(void)
{
    TS.vel_x = 0;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 3) + SPR2_VORTSTAND1);
    TS.time += (s16)ck_sprite_sync;
    if (TS.time >= 80) {
        TS.vel_x = 90;
        if (TS.pos_x > g_entities.sprites[0].pos_x)
            TS.vel_x = -TS.vel_x;
        TS.think = CK_THINK_VORT_WALK;
    }
    CVort_do_fall();
    CVort_compute_sprite_delta();
}

void CVort_contact_vorticon(CkSprite *vorticon, CkSprite *contacted)
{
    if (contacted->type_ != 10) {
        if (contacted->type_ != 11)
            return;
    }
    /* GAMEVER != KEEN1: pre-decrement pattern */
    vorticon->health--;
    if (vorticon->health)
        return;
    CVort_engine_setCurSound(0x27);
    vorticon->time = 0;
    vorticon->varB = 2;
    vorticon->frame = SPR2_VORTDIE1;
    vorticon->contact = CK_CONTACT_NOP;
    vorticon->think = CK_THINK_KILL_SPRITE;
}

void CVort_contact_tankshot(CkSprite *tankshot, CkSprite *contacted)
{
    /* KEEN2 exclusions: guard robot, dead things, elites */
    if (contacted->type_ == CK2_OBJ_GUARDBOT)
        return;
    if (contacted->type_ == CK_OBJ_DEAD)
        return;
    if (contacted->type_ == CK2_OBJ_ELITE)
        return;
    CVort_engine_setCurSound(0x25);
    tankshot->think = CK_THINK_ZAPZOT;
    tankshot->time = 0;
    tankshot->contact = CK_CONTACT_NOP;
    if (CVort_get_random() > 0x80)
        tankshot->frame = CK_SPR_SHOTSPLASHR;
    else
        tankshot->frame = CK_SPR_SHOTSPLASHL;
}

/* ---- Vorticon youth ------------------------------------------------------ */

void CVort_think_youth_walk(void)
{
    u16 blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR2_YOUTHRIGHT1;
    else
        TS.frame = SPR2_YOUTHLEFT1;
    TS.frame += (ck_ticks_lo >> 4) & 3;
    if (CVort_get_random() < (s16)(ck_sprite_sync * 3)) {
        TS.vel_y = -CVort_calc_jump_height(400);
        TS.think = CK_THINK_YOUTH_JUMP;
    }
    CVort_do_fall();
    blocking = (u16)CVort_compute_sprite_delta();
    if (!(blocking & 2))
        TS.think = CK_THINK_YOUTH_JUMP;
    if (blocking & 4)
        TS.vel_x = -250;
    if (blocking & 1)
        TS.vel_x = 250;
}

void CVort_think_youth_jump(void)
{
    u16 blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR2_YOUTHRIGHT4;
    else
        TS.frame = SPR2_YOUTHLEFT4;
    CVort_do_fall();
    blocking = (u16)CVort_compute_sprite_delta();
    if (blocking & 2)
        TS.think = CK_THINK_YOUTH_WALK;
    if (blocking & 4)
        TS.vel_x = -250;
    if (blocking & 1)
        TS.vel_x = 250;
}

void CVort_contact_youth(CkSprite *youth, CkSprite *contacted)
{
    /* keenshot or (KEEN2) enemyshot */
    if (contacted->type_ != 10) {
        if (contacted->type_ != 11)
            return;
    }
    CVort_engine_setCurSound(0x27); /* snd_vortscream */
    youth->time = 0;
    youth->varB = 2;
    youth->frame = SPR2_YOUTHDIE1;
    youth->contact = CK_CONTACT_NOP;
    youth->think = CK_THINK_KILL_SPRITE;
}

/* ---- Vortelite ----------------------------------------------------------- */

void CVort2_think_elite_walk(void)
{
    u16 blocking;
    u8 jumped;
    if (TS.vel_x > 0)
        TS.frame = SPR2_ELITERIGHT1;
    else
        TS.frame = SPR2_ELITELEFT1;
    TS.frame += (ck_ticks_lo >> 4) & 3;

    jumped = 0;
    if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
        if (g_game.lights) { /* even elites won't jump in the dark */
            TS.vel_y = -CVort_calc_jump_height(300);
            TS.think = CK_THINK_ELITE_JUMP;
            jumped = 1;
        }
    }
    if (!jumped) {
        if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
            /* charge at Keen */
            if (TS.pos_y + 0x800 == g_entities.sprites[0].pos_y)
                TS.vel_x = 200;
            else
                TS.vel_x = 100;
            if (g_entities.sprites[0].pos_x < TS.pos_x)
                TS.vel_x = -TS.vel_x;
        } else if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
            /* fire gun */
            TS.think = CK_THINK_ELITE_SHOOT;
            TS.varB = 0;
            TS.time = 0;
        }
    }

    CVort_do_fall();
    blocking = (u16)CVort_compute_sprite_delta();
    if (blocking & 4)
        TS.vel_x = -100;
    if (blocking & 1)
        TS.vel_x = 100;
    if (!(blocking & 2))
        TS.think = CK_THINK_ELITE_JUMP;
}

void CVort2_think_elite_shoot(void)
{
    if (TS.vel_x > 0)
        TS.frame = SPR2_ELITEFIRER;
    else
        TS.frame = SPR2_ELITEFIREL;
    TS.time += (s16)ck_sprite_sync;
    if (TS.time >= 30) {
        if (!TS.varB) {
            TS.varB = 1;
            CVort_engine_setCurSound(0x26); /* snd_tankfire */
            if (TS.vel_x > 0)
                CVort_add_sprite_tankshot(TS.pos_x, TS.pos_y - 0x100, 350);
            else
                CVort_add_sprite_tankshot(TS.pos_x, TS.pos_y - 0x100, -350);
        }
        if (TS.time > 50)
            TS.think = CK_THINK_ELITE_WALK;
    }
}

void CVort2_think_elite_jump(void)
{
    u16 blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR2_ELITEJUMPR;
    else
        TS.frame = SPR2_ELITEJUMPL;
    CVort_do_fall();
    blocking = (u16)CVort_compute_sprite_delta();
    if (blocking & 2)
        TS.think = CK_THINK_ELITE_WALK;
    if (blocking & 4)
        TS.vel_x = -100;
    if (blocking & 1)
        TS.vel_x = 100;
}

void CVort2_contact_elite(CkSprite *elite, CkSprite *contacted)
{
    s16 currHealth;
    if (contacted->type_ != 10) /* CVort2_obj_keenshot */
        return;
    /* post-decrement pattern of the original (health-- == 0) */
    currHealth = elite->health;
    elite->health--;
    if (currHealth)
        return;
    CVort_engine_setCurSound(0x27); /* snd_vortscream */
    elite->time = 0;
    elite->varB = 2;
    elite->frame = SPR2_ELITEDIE1;
    elite->contact = CK_CONTACT_NOP;
    elite->think = CK_THINK_KILL_SPRITE;
}

/* ---- Guard robot --------------------------------------------------------- */

void CVort2_think_guardbot_move(void)
{
    u16 blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR2_GUARDRIGHT1;
    else
        TS.frame = SPR2_GUARDLEFT1;
    TS.frame += (ck_ticks_lo >> 4) & 3;

    if (CVort_get_random() < (s16)ck_sprite_sync) {
        TS.think = CK_THINK_GUARD_SHOOT;
        TS.varB = 0;
        TS.time = 0;
    }
    blocking = (u16)CVort_compute_sprite_delta(); /* no gravity: it hovers */
    if (blocking & 1) {
        TS.varB = 100;
        TS.time = 0;
        TS.think = CK_THINK_GUARD_TURN;
    } else if (blocking & 4) {
        TS.varB = -100;
        TS.time = 0;
        TS.think = CK_THINK_GUARD_TURN;
    }
}

void CVort2_think_guardbot_shoot(void)
{
    if (TS.vel_x > 0)
        TS.frame = SPR2_GUARDRIGHT1;
    else
        TS.frame = SPR2_GUARDLEFT1;
    TS.frame += (ck_ticks_lo >> 4) & 3;
    TS.time += (s16)ck_sprite_sync;
    if (TS.time >= 50) {
        if (TS.time > 150)
            TS.think = CK_THINK_GUARD_MOVE;
        TS.varB += (s16)ck_sprite_sync;
        if (TS.varB > 20) {
            TS.varB = 0;
            CVort_engine_setCurSound(0x26); /* snd_tankfire */
            if (TS.vel_x > 0)
                CVort_add_sprite_tankshot(TS.pos_x, TS.pos_y - 0x400, 350);
            else
                CVort_add_sprite_tankshot(TS.pos_x, TS.pos_y - 0x400, -350);
        }
    }
}

void CVort2_think_guardbot_turn(void)
{
    TS.time += (s16)ck_sprite_sync;
    if (TS.time > 50) {
        TS.think = CK_THINK_GUARD_MOVE;
        TS.vel_x = TS.varB;
    }
    TS.frame = (u16)(SPR2_GUARDSTAND1 + ((ck_ticks_lo >> 4) & 1));
}

void CVort2_contact_guardbot(CkSprite *guardbot, CkSprite *contacted)
{
    /* nothing here (the guard robot is indestructible) */
    (void)guardbot;
    (void)contacted;
}

/* ---- Scrub (wall crawler) ------------------------------------------------ */

void CVort2_think_scrub_walk_left(void)
{
    u16 blocking;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 1) + SPR2_SCRUBL1);
    TS.vel_x = -80;
    TS.vel_y = 80;
    if (TS.time == 0)
        TS.del_y += 0x400;

    blocking = (u16)CVort_compute_sprite_delta();

    if (blocking & 1) {
        TS.think = CK_THINK_SCRUB_UP;
        TS.time = 0;
        return;
    }
    if (!(blocking & 2)) {
        if (TS.time) {
            TS.think = CK_THINK_SCRUB_DOWN;
            TS.time = 0;
            TS.pos_y += 0x100;
            return;
        }
        TS.think = CK_THINK_SCRUB_FALL;
    }
    if (!TS.time)
        TS.time = 1;
}

void CVort2_think_scrub_walk_down(void)
{
    u16 blocking;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 1) + SPR2_SCRUBD1);
    TS.vel_x = 80;
    TS.vel_y = 80;
    if (TS.time == 0)
        TS.del_x += 0x400;

    blocking = (u16)CVort_compute_sprite_delta();

    if (blocking & 2) {
        TS.think = CK_THINK_SCRUB_LEFT;
        TS.time = 0;
        return;
    }
    if (!(blocking & 4)) {
        if (TS.time) {
            TS.think = CK_THINK_SCRUB_RIGHT;
            TS.time = 0;
            TS.pos_x += 0x100;
            return;
        }
        TS.think = CK_THINK_SCRUB_FALL;
    }
    if (!TS.time)
        TS.time = 1;
}

void CVort2_think_scrub_walk_right(void)
{
    u16 blocking;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 1) + SPR2_SCRUBR1);
    TS.vel_x = 80;
    TS.vel_y = -80;
    if (TS.time == 0)
        TS.del_y -= 0x400;

    blocking = (u16)CVort_compute_sprite_delta();

    if (blocking & 4) {
        TS.think = CK_THINK_SCRUB_DOWN;
        TS.time = 0;
        return;
    }
    if (!(blocking & 8)) {
        if (TS.time) {
            TS.think = CK_THINK_SCRUB_UP;
            TS.time = 0;
            TS.pos_y -= 0x100;
            return;
        }
        TS.think = CK_THINK_SCRUB_FALL;
    }
    if (!TS.time)
        TS.time = 1;
}

void CVort2_think_scrub_walk_up(void)
{
    u16 blocking;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 1) + SPR2_SCRUBU1);
    TS.vel_x = -80;
    TS.vel_y = -80;
    if (TS.time == 0)
        TS.del_x -= 0x400;

    blocking = (u16)CVort_compute_sprite_delta();

    if (blocking & 8) {
        TS.think = CK_THINK_SCRUB_RIGHT;
        TS.time = 0;
        return;
    }
    if (!(blocking & 1)) {
        if (TS.time) {
            TS.think = CK_THINK_SCRUB_LEFT;
            TS.time = 0;
            TS.pos_x -= 0x100;
            return;
        }
        TS.think = CK_THINK_SCRUB_FALL;
    }
    if (!TS.time)
        TS.time = 1;
}

void CVort2_think_scrub_fall(void)
{
    u16 blocking;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 1) + SPR2_SCRUBL1);
    TS.vel_x = 0;
    CVort_do_fall();
    blocking = (u16)CVort_compute_sprite_delta();
    if (blocking & 2) {
        TS.think = CK_THINK_SCRUB_LEFT;
        TS.time = 0;
    }
}

void CVort2_contact_scrub(CkSprite *scrub, CkSprite *contacted)
{
    if (contacted->type_ != 10) {
        if (contacted->type_ != 11)
            return;
    }
    scrub->time = 0;
    scrub->varB = 2;
    scrub->frame = SPR2_SCRUBSHOT;
    scrub->contact = CK_CONTACT_NOP;
    scrub->think = CK_THINK_KILL_SPRITE;
}

/* ---- Platform ------------------------------------------------------------ */

void CVort2_think_platform_move(void)
{
    u16 blocking;
    TS.frame = (u16)(SPR2_PLATFORM1 + ((ck_ticks_lo >> 5) & 1));
    blocking = (u16)CVort_compute_sprite_delta();
    if (blocking & 5) {
        if (blocking & 1)
            TS.varB = 75;
        else
            TS.varB = -75;
        TS.vel_x = 0;
        TS.time = 0;
        TS.think = CK_THINK_PLATFORM_TURN;
    }
}

void CVort2_think_platform_turn(void)
{
    TS.time += (s16)ck_sprite_sync;
    if (TS.time > 75) {
        TS.vel_x = TS.varB;
        TS.think = CK_THINK_PLATFORM_MOVE;
    }
}

/* ---- Tantalus ray -------------------------------------------------------- */

void CVort2_think_tantalus(void)
{
    u16 blocking;
    TS.frame = (u16)(SPR2_SPARK1 + ((ck_ticks_lo >> 3) & 3));
    blocking = (u16)CVort_compute_sprite_delta();
    /* odd... they don't move anyway (kept from vanilla) */
    if (blocking & 1)
        TS.vel_x = 75;
    else if (blocking & 4)
        TS.vel_x = -75;
}

static void CVort2_tantalus_explosion(u16 tileX, u16 tileY, u16 tilenum)
{
    s16 i;
    CkSprite *s;
    CVort_engine_setCurSound(0x25); /* snd_shothit */
    i = CVort_add_sprite();
    s = &g_entities.sprites[i];
    s->think = CK_THINK_ZAPZOT;
    s->type_ = CK_OBJ_DEAD;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->time = 0;
    s->contact = CK_CONTACT_NOP;
    if (CVort_get_random() > 0x80)
        s->frame = CK_SPR_SHOTSPLASHL;
    else
        s->frame = CK_SPR_SHOTSPLASHR;
    ck_map_set(tileX, tileY, tilenum);
}

void CVort2_contact_tantalus(CkSprite *tantalus, CkSprite *contacted)
{
    /* Unexpected but vanilla: ANYTHING touching the tantalus saves the
     * city! */
    switch (g_game.current_level) {
        case 4:  keen_gp.targets[0] = 1; break;
        case 6:  keen_gp.targets[1] = 1; break;
        case 7:  keen_gp.targets[2] = 1; break;
        case 9:  keen_gp.targets[3] = 1; break;
        case 11: keen_gp.targets[4] = 1; break;
        case 13: keen_gp.targets[5] = 1; break;
        case 15: keen_gp.targets[6] = 1; break;
        case 16: keen_gp.targets[7] = 1; break;
        default: break;
    }

    /* ...but only Keen's shot causes an explosion */
    if (contacted->type_ == 10) {
        s16 bodyNum;
        tantalus->type_ = 0;
        CVort_add_score(10000);
        bodyNum = CVort_add_body();
        g_entities.bodies[bodyNum].type_ = 5; /* CVort2_bod_tantalus_explosion */
        g_entities.bodies[bodyNum].think = CK_BODY_DESTROY_TANTALUS;
        g_entities.bodies[bodyNum].tile_x = CK_W2T(tantalus->pos_x);
        g_entities.bodies[bodyNum].tile_y = CK_W2T(tantalus->pos_y);
        g_entities.bodies[bodyNum].field_C = 0;
        g_entities.bodies[bodyNum].variant = 0;
        /* Desktop also adds a border-flash body; the SNES has no EGA
         * border color, so the flash is skipped (as in ep1). */
    }
}

void CVort2_body_destroy_tantalus(CkBody *tantalus)
{
    u16 var4, var_si;
    u16 tx = (u16)tantalus->tile_x;
    u16 ty = (u16)tantalus->tile_y;
    s16 step;

    /* wait 40 ticks per step */
    tantalus->variant += (s16)ck_sprite_sync;
    if (tantalus->variant < 40)
        return;
    tantalus->variant -= 40;

    step = tantalus->field_C;
    tantalus->field_C++;
    switch (step) {
        case 0:
            ck_map_set((u16)(tx - 1), (u16)(ty - 1), 0x8F);
            ck_map_set(tx, (u16)(ty - 1), 0x8F);
            ck_map_set((u16)(tx + 1), (u16)(ty - 1), 0x8F);
            ck_map_set((u16)(tx - 1), ty, 0x222);
            ck_map_set(tx, ty, 0x223);
            ck_map_set((u16)(tx + 1), ty, 0x224);
            ck_map_set((u16)(tx - 3), (u16)(ty + 4), 0x1FA);
            return;
        case 1:
            CVort2_tantalus_explosion((u16)(tx - 2), ty, 0x1EC);
            return;
        case 2:
            CVort2_tantalus_explosion((u16)(tx + 2), ty, 0x1EC);
            return;
        case 3:
        case 4:
        case 5:
            CVort2_tantalus_explosion(tx, (u16)(ty + step - 1), 0x1F9);
            return;
        case 6:
        case 7:
        case 8:
        case 9:
            for (var_si = (u16)(ty + 3); var_si < (u16)(ty + 6); var_si++)
                CVort2_tantalus_explosion((u16)(step - 4 + tx), var_si, 0x225);
            return;
        default:
            for (var4 = (u16)(tx - 7); var4 < (u16)(tx - 4); var4++)
                for (var_si = (u16)(ty + 2); var_si < (u16)(ty + 5); var_si++)
                    ck_map_set(var4, var_si, 0x215);
            tantalus->type_ = 0;
            return;
    }
}

/* ---- lights-out (src/game/ui.c CVort_lights_on / CVort_lights_out) ------
 *
 * The DOS engine remaps the EGA palette through the EXE's fade palette 1
 * when the lights go out (setPaletteAndBorderColor(palettes[1])). On the
 * SNES both the BG identity palette (CGRAM 0..15) and the 8 bake-binned
 * OBJ palettes (CGRAM 128..255) are rewritten with the same EGA index ->
 * palettes[1] remap: every OBJ color is an exact EGA color, so its index
 * is recovered by matching against ck_pal_ega and pushed through
 * ck_pal_flash[1] (= bgr555(EGA[palettes[1][i]]), baked). The CGRAM
 * writes only happen inside vblank (ck_lights_vblank, gated on
 * REG_HVBJOY bit7 - landmine #5).
 */
static u16 s_lightsBg[16];
static u16 s_lightsObj[128];
static u8 s_lightsPending;      /* runtime-initialized (landmine #2) */

static void ck_lights_queue(u8 dark)
{
    u8 i, j;
    const u16 *obj = (const u16 *)ck_obj_pals;
    if (dark) {
        for (i = 0; i < 16; i++)
            s_lightsBg[i] = ck_pal_flash[1][i];
        for (i = 0; i < 128; i++) {
            u16 w = obj[i];
            u16 dw = 0;
            for (j = 0; j < 16; j++) {
                if (ck_pal_ega[j] == w) {
                    dw = ck_pal_flash[1][j];
                    break;
                }
            }
            s_lightsObj[i] = dw;
        }
    } else {
        for (i = 0; i < 16; i++)
            s_lightsBg[i] = ck_pal_ega[i];
        for (i = 0; i < 128; i++)
            s_lightsObj[i] = obj[i];
    }
    s_lightsPending = 1;
}

void CVort_lights_on(void)
{
    g_game.lights = 1;
    ck_lights_queue(0);
}

void CVort_lights_out(void)
{
    g_game.lights = 0;
    ck_lights_queue(1);
}

/* Level/map (re)entry: lights are on (level_init_screen sets lights=1).
 * Queue a bright rewrite so OBJ palettes recover if the previous level
 * ended in the dark. */
void ck_lights_reset(void)
{
    g_game.lights = 1;
    ck_lights_queue(0);
}

/* Right after WaitForVBlank: flush the queued palettes while the PPU is
 * still in vblank; otherwise retry next frame (CGRAM writes outside
 * vblank are unreliable). */
void ck_lights_vblank(void)
{
    if (!s_lightsPending)
        return;
    if (!(REG_HVBJOY & 0x80))
        return;
    dmaCopyCGram((u8 *)s_lightsBg, 0, 32);
    dmaCopyCGram((u8 *)s_lightsObj, 128, 256);
    s_lightsPending = 0;
}

/* ---- the earth explodes (CVort2_draw_earth_explode, simplified) ---------
 *
 * Level 81 backdrop; the tantalus ray flies southeast with the camera
 * following, then the earth blows apart (tile patch + chunk sprites).
 * The desktop's nine EXE-table fire overlays are skipped.
 */
static void ep2_scene_frame(void)
{
    ck_render_update();
    WaitForVBlank();
    ck_render_vblank();
    ck_msprite_vblank();
    ck_lights_vblank();
    ck_timer_frame(1);
}

void ck_ep2_earth_explode(void)
{
    const CkLevelEntry *e = ck_level_find(81);
    u16 f, explodeTick;
    s32 var1C;
    s16 camPx, camPy;
    if (!e)
        return;

    ck_lights_reset();

    setScreenOff();
    ck_level_load(81);
    ck_render_tset = e->tset;
    ck_level_setup_bounds();
    scroll_x = scroll_y = 0;
    ck_cam_px = ck_cam_py = 0;
    ck_render_level_init();
    ck_msprite_flush_all();
    setScreenOn();

    /* hold the start view briefly */
    for (f = 0; f < 50; f++) {
        ck_msprite_begin();
        ck_msprite_end();
        ep2_scene_frame();
    }

    /* Tantalus ray flies southeast: world (0x6000,0x5000) ->
     * (0x2F000,0x16000) in 128 steps of (0x520,0x220); the camera
     * follows at pos - (0x6000,0x5000), so the sprite keeps its screen
     * offset (0x60,0x50) while the map pans under it. */
    CVort_engine_setCurSound(0x26); /* snd_tankfire */
    for (f = 0; f <= 128; f++) {
        ck_cam_px = (u16)((f * 0x52) >> 4);   /* = (f*0x520)>>8, u16-safe */
        ck_cam_py = (u16)((f * 0x22) >> 4);   /* = (f*0x220)>>8           */
        ck_msprite_begin();
        ck_msprite_draw((u16)(SPR2_TANTALUS1 + ((f >> 2) & 1)),
                        0x60, 0x50);
        ck_msprite_end();
        ep2_scene_frame();
    }

    /* The earth explodes! */
    CVort_engine_setCurSound(0x2A); /* snd_earthpow */
    camPx = 0x290;
    camPy = 0x110;
    for (explodeTick = 0; explodeTick < 60; explodeTick++) {
        u16 chunkFrame = (u16)((explodeTick >> 1) & 3);
        u16 sub;
        for (sub = 0; sub < 2; sub++) {   /* 2 video frames per tick */
            ck_msprite_begin();
            if (explodeTick >= 15) {
                s16 h, hp, v;
                var1C = (s32)(explodeTick - 15) * 0xA00;
                h  = (s16)((0x2F000L - (var1C >> 1)) >> 8) - camPx;
                hp = (s16)((0x2F000L + (var1C >> 1) + 0x1000L) >> 8) - camPx;
                v  = (s16)((0x16000L - var1C) >> 8) - camPy;
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), h, v);
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), hp, v);
                v = (s16)((0x16000L + var1C + 0x1000L) >> 8) - camPy;
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), h, v);
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), hp, v);
                h  = (s16)((0x2F000L - var1C) >> 8) - camPx;
                hp = (s16)((0x2F000L + var1C + 0x1000L) >> 8) - camPx;
                v  = (s16)((0x16000L - (var1C >> 1)) >> 8) - camPy;
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), h, v);
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), hp, v);
                v = (s16)((0x16000L + (var1C >> 1) + 0x1000L) >> 8) - camPy;
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), h, v);
                ck_msprite_draw((u16)(SPR2_LILCHUNK1 + chunkFrame), hp, v);
            }
            if (explodeTick >= 20) {
                s16 h, hp, v, vp;
                var1C = ((s32)(explodeTick - 20) << 11);
                h  = (s16)((0x2F000L - var1C) >> 8) - camPx;
                hp = (s16)((0x2F000L + var1C + 0x1000L) >> 8) - camPx;
                v  = (s16)((0x16000L - var1C) >> 8) - camPy;
                vp = (s16)((0x16000L + var1C + 0x1000L) >> 8) - camPy;
                ck_msprite_draw((u16)(SPR2_EARTHCHUNK1 + chunkFrame), h, v);
                ck_msprite_draw((u16)(SPR2_EARTHCHUNK1 + chunkFrame), hp, v);
                ck_msprite_draw((u16)(SPR2_EARTHCHUNK1 + chunkFrame), h, vp);
                ck_msprite_draw((u16)(SPR2_EARTHCHUNK1 + chunkFrame), hp, vp);
            }
            ck_msprite_end();
            if ((explodeTick == 20) && (sub == 0)) {
                u16 xx, yy;
                for (yy = 0x16; yy <= 0x18; yy++)
                    for (xx = 0x2F; xx <= 0x31; xx++)
                        ck_map_set(xx, yy, 0x9B);
            }
            ep2_scene_frame();
        }
    }

    /* linger on the empty sky */
    for (f = 0; f < 90; f++) {
        ck_msprite_begin();
        ck_msprite_end();
        ep2_scene_frame();
    }
}

#endif /* CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2 */
