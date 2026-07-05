/* episode3.c - SNES transcription of src/episodes/episode3.c (sprite
 * spawns, Vortimom / Meep / Vortininja / Foob / Ball & Jack / Sparks /
 * Heart-of-the-Mangling-Machine thinks and contacts, contact_keen,
 * enemy gun and Mangling Machine bodies) plus the ep3-relevant parts of
 * src/game/enemies.c (Vorticon, Vorticon youth - GAMEVER_KEEN3 paths)
 * and a text-box rendition of the Grand Intellect reveal.
 *
 * 816-tcc rules observed: no negated short-circuit ORs, no zero-init
 * assumptions, positions s32, everything else s16/u16, map writes via
 * ck_map_set.
 */
#include "game/game_state.h"
#include "game/sprites.h"
#include "game/physics.h"
#include "game/keen.h"
#include "game/gameplay.h"
#include "game/episode3.h"
#include "game/ui.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/timer.h"
#include "data_format.h"
#include "snes_data_gen.h"

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3

#define TS g_entities.temp_sprite

/* object types (CVort3_objtype, src/episodes/episode3.h) */
#define CK3_OBJ_VORTICON   2
#define CK3_OBJ_YOUTH      3
#define CK3_OBJ_VORTIMOM   4
#define CK3_OBJ_MEEP       5
#define CK3_OBJ_VORTININJA 6
#define CK3_OBJ_FOOB       7
#define CK3_OBJ_BALL       8
#define CK3_OBJ_JACK       9
#define CK3_OBJ_PLATFORM   10
#define CK3_OBJ_HEART      12
#define CK3_OBJ_SPARK      13
#define CK3_OBJ_ENEMYSHOT  16
#define CK3_OBJ_MEEPSHOT   17

/* body types (CVort3_bodytype) */
#define CK3_BOD_ENEMYSHOT             5
#define CK3_BOD_MANGLING_ARM          6
#define CK3_BOD_MANGLING_LEG          7
#define CK3_BOD_MANGLING_ARM_DESTRUCT 8
#define CK3_BOD_MANGLING_DESTRUCT     9

/* sprite frames (CVort3_spr_*, src/episodes/episode3.h) */
#define SPR3_YOUTHLEFT1   47
#define SPR3_YOUTHRIGHT1  51
#define SPR3_YOUTHLEFT4   50
#define SPR3_YOUTHRIGHT4  54
#define SPR3_YOUTHDIE1    55
#define SPR3_MOMFIREL1    57
#define SPR3_MOMFIRER1    59
#define SPR3_VORTLEFT1    63
#define SPR3_VORTRIGHT1   67
#define SPR3_VORTSTAND1   71
#define SPR3_VORTJUMPL    73
#define SPR3_VORTJUMPR    74
#define SPR3_VORTDIE1     75
#define SPR3_NINJAL1      77
#define SPR3_NINJAR1      79
#define SPR3_NINJAJUMPL   81
#define SPR3_NINJAJUMPR   82
#define SPR3_NINJADIE1    83
#define SPR3_MOMLEFT1     85
#define SPR3_MOMRIGHT1    87
#define SPR3_MOMATTACKL   89
#define SPR3_MOMATTACKR   90
#define SPR3_MOMDIE1      91
#define SPR3_FOOBL1       93
#define SPR3_FOOBR1       95
#define SPR3_FOOBYELL1    97
#define SPR3_FOOBDIE1     99
#define SPR3_TANKSHOT     103
#define SPR3_TANKSHOTV    104
#define SPR3_PLATFORM1    107
#define SPR3_BALL         109
#define SPR3_JACK1        110
#define SPR3_SPARK1       114
#define SPR3_MEEPR1       118
#define SPR3_MEEPL1       120
#define SPR3_MEEPSINGR    122
#define SPR3_MEEPSINGL    123
#define SPR3_MEEPDIE1     124
#define SPR3_MEEPWAVER1   126
#define SPR3_MEEPWAVEL1   128
#define SPR3_HEART1       146

/* ---- shared spawns (enemies.c, GAMEVER_KEEN3 paths) --------------------- */

static void CVort_add_sprite_vorticon(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_VORTICON;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_VORT_WALK;
    s->contact = CK_CONTACT_VORTICON;
    s->health = 1;               /* GAMEVER != KEEN1 */
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -90;
    else
        s->vel_x = 90;
    s->frame = SPR3_VORTSTAND1;
}

static void CVort_add_sprite_youth(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_YOUTH;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_YOUTH_WALK;
    s->contact = CK_CONTACT_YOUTH;
    s->health = 1;
    if (g_entities.sprites[0].pos_x > s->pos_x)
        s->vel_x = 250;
    else
        s->vel_x = -250;
    s->frame = SPR3_YOUTHLEFT1;
}

/* ---- episode spawns ------------------------------------------------------ */

static void CVort3_add_sprite_vortimom(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_VORTIMOM;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_MOM_WALK;
    s->contact = CK_CONTACT_MOM;
    s->health = 5;
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -50;
    else
        s->vel_x = 50;
    s->frame = SPR3_MOMLEFT1;
}

static void CVort3_add_sprite_meep(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_MEEP;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY) + 0x800;
    s->think = CK_THINK_MEEP_WALK;
    s->contact = CK_CONTACT_MEEP;
    s->health = 1;
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -65;
    else
        s->vel_x = 65;
    s->frame = SPR3_MEEPL1;
}

static void CVort3_add_sprite_vortininja(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_VORTININJA;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_NINJA_STAND;
    s->contact = CK_CONTACT_NINJA;
    s->health = 3;               /* in practice 4 shots (post-dec) */
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -1;
    else
        s->vel_x = 1;
    s->frame = SPR3_NINJAL1;
}

static void CVort3_add_sprite_foob(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_FOOB;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_FOOB_WALK;
    s->contact = CK_CONTACT_FOOB;
    s->health = 1;
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -50;
    else
        s->vel_x = 50;
    s->frame = SPR3_FOOBL1;
}

static void CVort3_add_sprite_ball(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_BALL;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_BALL;
    s->contact = CK_CONTACT_NOP;
    s->health = 1;
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -400;
    else
        s->vel_x = 400;
    if (s->pos_y > g_entities.sprites[0].pos_y)
        s->vel_y = -400;
    else
        s->vel_y = 400;
    s->frame = SPR3_BALL;
}

static void CVort3_add_sprite_jack(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_JACK;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_JACK;
    s->contact = CK_CONTACT_NOP;
    s->health = 1;
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -400;
    else
        s->vel_x = 400;
    if (s->pos_y > g_entities.sprites[0].pos_y)
        s->vel_y = -400;
    else
        s->vel_y = 400;
    s->frame = SPR3_JACK1;
}

static void CVort3_add_sprite_platform_h(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_PLATFORM;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY) - 0x400;
    s->think = CK_THINK_PLATFORM_MOVE;
    s->contact = CK_CONTACT_NOP;
    s->frame = SPR3_PLATFORM1;
    s->vel_x = 75;
}

static void CVort3_add_sprite_platform_v(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_PLATFORM;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_PLATFORM_MOVE;
    s->contact = CK_CONTACT_NOP;
    s->frame = SPR3_PLATFORM1;
    s->vel_y = 75;
}

static void CVort3_add_sprite_spark(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_SPARK;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_SPARK;
    s->contact = CK_CONTACT_SPARK;
    s->frame = SPR3_SPARK1;
}

static void CVort3_add_sprite_heart(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK3_OBJ_HEART;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_HEART;
    s->contact = CK_CONTACT_HEART;
    /* yes, that is an unnoticeable vanilla bug (spark frame) */
    s->frame = SPR3_SPARK1;
}

static void CVort3_add_body_enemygun(u16 tileX, u16 tileY, u8 down)
{
    s16 i = CVort_add_body();
    CkBody *b = &g_entities.bodies[i];
    b->type_ = CK3_BOD_ENEMYSHOT;
    /* yes, the scaling IS done (used for the shots later) */
    b->tile_x = CK_T2W(tileX);
    b->tile_y = CK_T2W(tileY);
    if (down)
        b->think = CK_BODY_ENEMYGUN_D;
    else
        b->think = CK_BODY_ENEMYGUN_R;
}

static void CVort3_add_body_mangling_arm(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_body();
    CkBody *b = &g_entities.bodies[i];
    b->type_ = CK3_BOD_MANGLING_ARM;
    b->tile_x = tileX;
    b->tile_y = tileY;
    b->think = CK_BODY_MANGLING_ARM;
    b->field_C = 0;
    b->variant = 1;
    b->field_E = 15;
}

static void CVort3_add_body_mangling_leg(u16 tileX, u16 tileY, s16 left_right)
{
    s16 i = CVort_add_body();
    CkBody *b = &g_entities.bodies[i];
    b->type_ = CK3_BOD_MANGLING_LEG;
    b->tile_x = tileX;
    b->tile_y = tileY;
    b->field_E = left_right;
    b->variant = left_right;
    b->think = CK_BODY_MANGLING_LEG_MOVE;
}

/* CVort3_init_level's per-cell spawn dispatch. */
void CVort3_spawn_plane_value(u16 currSprite, u16 tileX, u16 tileY)
{
    switch (currSprite) {
        case 1:
            CVort_add_sprite_vorticon(tileX, tileY);
            break;
        case 2:
            CVort_add_sprite_youth(tileX, tileY);
            break;
        case 3:
            CVort3_add_sprite_vortimom(tileX, tileY);
            break;
        case 4:
            CVort3_add_sprite_meep(tileX, tileY);
            break;
        case 5:
            CVort3_add_sprite_vortininja(tileX, tileY);
            break;
        case 6:
            CVort3_add_sprite_foob(tileX, tileY);
            break;
        case 7:
            CVort3_add_sprite_ball(tileX, tileY);
            break;
        case 8:
            CVort3_add_sprite_jack(tileX, tileY);
            break;
        case 9:
            CVort3_add_sprite_platform_h(tileX, tileY);
            break;
        case 10:
            CVort3_add_sprite_platform_v(tileX, tileY);
            break;
        case 11:
            CVort_add_sprite_vorticon(tileX, tileY);
            break;
        case 12:
            CVort3_add_sprite_spark(tileX, tileY);
            break;
        case 13:
            CVort3_add_sprite_heart(tileX, tileY);
            break;
        case 14:
            CVort3_add_body_enemygun(tileX, tileY, 0);
            break;
        case 15:
            CVort3_add_body_enemygun(tileX, tileY, 1);
            break;
        case 16:
            CVort3_add_body_mangling_arm(tileX, tileY);
            break;
        case 17:
            CVort3_add_body_mangling_leg(tileX, tileY, -1);
            break;
        case 18:
            CVort3_add_body_mangling_leg(tileX, tileY, 1);
            break;
        case 0xFF: /* Keen */
            g_entities.sprites[0].pos_x = CK_T2W(tileX);
            g_entities.sprites[0].pos_y = CK_T2W(tileY) + 0x800;
            break;
        default:
            break;
    }
}

/* ---- contact_keen -------------------------------------------------------- */

void CVort3_contact_keen(CkSprite *keen, CkSprite *contacted)
{
    switch (contacted->type_) {
        case CK3_OBJ_VORTICON:
        case CK3_OBJ_VORTININJA:
        case CK3_OBJ_JACK:
        case CK3_OBJ_ENEMYSHOT:
        case CK3_OBJ_MEEPSHOT: /* kill Keen */
            CVort_kill_keen();
            break;
        case CK3_OBJ_YOUTH: /* knock Keen flat */
            if (keen->think == CK_THINK_KEEN_STUNNED)
                return;
            if (g_game.god_mode)
                return;
            if (g_game.keen_invincible)
                return;
            keen->think = CK_THINK_KEEN_STUNNED;
            keen->vel_x = contacted->vel_x;
            keen->vel_y = contacted->vel_y;
            keen->time = 400;
            break;
        case CK3_OBJ_VORTIMOM:
        case CK3_OBJ_MEEP:
            CVort_push_keen(keen, contacted);
            break;
        case CK3_OBJ_BALL:
        case CK3_OBJ_PLATFORM:
            CVort_carry_keen(keen, contacted);
            break;
        default:
            break;
    }
}

/* ---- Vorticon (enemies.c, GAMEVER_KEEN3 paths) --------------------------- */

void CVort_think_vorticon_walk(void)
{
    s16 currDelta;
    u8 jumped;
    if (TS.vel_x > 0)
        TS.frame = SPR3_VORTRIGHT1;
    else
        TS.frame = SPR3_VORTLEFT1;
    TS.frame += (ck_ticks_lo >> 4) & 3;
    jumped = 0;
    if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
        if (g_game.lights) { /* always 1 in ep3 */
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
        TS.frame = SPR3_VORTJUMPL; /* sic - vanilla swaps them */
    else
        TS.frame = SPR3_VORTJUMPR;
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
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 3) + SPR3_VORTSTAND1);
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
    /* KEEN3: keenshot (15) or enemyshot (16) */
    if (contacted->type_ != 15) {
        if (contacted->type_ != 16)
            return;
    }
    vorticon->health--;
    if (vorticon->health)
        return;
    CVort_engine_setCurSound(0x27);
    vorticon->time = 0;
    vorticon->varB = 2;
    vorticon->frame = SPR3_VORTDIE1;
    vorticon->contact = CK_CONTACT_NOP;
    vorticon->think = CK_THINK_KILL_SPRITE;
}

/* ---- Vorticon youth ------------------------------------------------------ */

void CVort_think_youth_walk(void)
{
    u16 blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR3_YOUTHRIGHT1;
    else
        TS.frame = SPR3_YOUTHLEFT1;
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
        TS.frame = SPR3_YOUTHRIGHT4;
    else
        TS.frame = SPR3_YOUTHLEFT4;
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
    if (contacted->type_ != 15) /* KEEN3: keenshot only */
        return;
    CVort_engine_setCurSound(0x27); /* snd_vortscream */
    youth->time = 0;
    youth->varB = 2;
    youth->frame = SPR3_YOUTHDIE1;
    youth->contact = CK_CONTACT_NOP;
    youth->think = CK_THINK_KILL_SPRITE;
}

/* ---- Vortimom ------------------------------------------------------------ */

void CVort3_think_vortimom_walk(void)
{
    s32 currPosY;
    u16 blocking;

    if (TS.vel_x > 0)
        TS.frame = SPR3_MOMRIGHT1;
    else
        TS.frame = SPR3_MOMLEFT1;
    TS.frame += (ck_ticks_lo >> 4) & 1;
    currPosY = TS.pos_y;
    if (CVort_get_random() < (s16)ck_sprite_sync) {
        TS.varC = -TS.vel_x;
        TS.vel_x = 0;
        TS.varB = 0;
        TS.time = 0;
        TS.think = CK_THINK_MOM_SHOOT;
    }
    CVort_do_fall();
    blocking = (u16)CVort_compute_sprite_delta();

    if (!(blocking & 2)) {
        TS.pos_y = currPosY;
        TS.vel_x = -TS.vel_x;
        TS.pos_x += ((s32)TS.vel_x << 1);
    }
    if (blocking & 4)
        TS.vel_x = -50;
    if (blocking & 1)
        TS.vel_x = 50;
}

static void CVort3_add_vortimomshot(s32 pos_x, s32 pos_y, s16 velocity)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    u16 tx, ty;
    s->type_ = CK3_OBJ_ENEMYSHOT;
    s->pos_x = pos_x;
    s->pos_y = pos_y + 0xD00;
    s->think = CK_THINK_MOMSHOT;
    s->vel_x = velocity;
    s->contact = CK_CONTACT_MOMSHOT;

    tx = (u16)CK_W2T(pos_x);
    ty = (u16)CK_W2T(pos_y);
    if (velocity >= 0) {
        /* yes, initially time == frame! */
        s->time = SPR3_MOMFIRER1;
        s->frame = SPR3_MOMFIRER1;
        if (!TILEINFO_REdge[map_data_tiles[ck_rowofs[ty + 1] + tx + 1]])
            return;
    } else {
        s->time = SPR3_MOMFIREL1;
        s->frame = SPR3_MOMFIREL1;
        if (!TILEINFO_LEdge[map_data_tiles[ck_rowofs[ty + 1] + tx]])
            return;
    }
    CVort_engine_setCurSound(0x25);
    s->type_ = CK_OBJ_ZAPZOT;
    s->think = CK_THINK_ZAPZOT;
    s->time = 0;
    if (CVort_get_random() > 0x80)
        s->frame = CK_SPR_SHOTSPLASHR;
    else
        s->frame = CK_SPR_SHOTSPLASHL;
}

void CVort3_think_vortimom_shoot(void)
{
    if (TS.varC < 0)
        TS.frame = SPR3_MOMATTACKL;
    else
        TS.frame = SPR3_MOMATTACKR;
    TS.time += (s16)ck_sprite_sync;
    if (TS.time < 30)
        return;
    if (!TS.varB) {
        TS.varB = 1;
        CVort_engine_setCurSound(0x26);
        if (TS.varC < 0)
            CVort3_add_vortimomshot(TS.pos_x, TS.pos_y - 0x100, -150);
        else
            CVort3_add_vortimomshot(TS.pos_x, TS.pos_y - 0x100, 150);
    }
    if (TS.time <= 50)
        return;
    TS.vel_x = TS.varC;
    TS.think = CK_THINK_MOM_WALK;
}

void CVort3_think_vortimomshot(void)
{
    TS.frame = (u16)(TS.time + ((ck_ticks_lo >> 3) & 1));
    if (!CVort_compute_sprite_delta())
        return;
    CVort_engine_setCurSound(0x25);
    TS.type_ = CK_OBJ_ZAPZOT;
    TS.think = CK_THINK_ZAPZOT;
    TS.time = 0;
    if (CVort_get_random() > 0x80)
        TS.frame = CK_SPR_SHOTSPLASHR;
    else
        TS.frame = CK_SPR_SHOTSPLASHL;
}

void CVort3_contact_vortimomshot(CkSprite *shot, CkSprite *contacted)
{
    if (contacted->type_ == CK3_OBJ_VORTIMOM)
        return;
    if (contacted->type_ == CK_OBJ_DEAD)
        return;
    CVort_engine_setCurSound(0x25);
    shot->think = CK_THINK_ZAPZOT;
    shot->contact = CK_CONTACT_NOP;
    shot->time = 0;
    if (CVort_get_random() > 0x80)
        shot->frame = CK_SPR_SHOTSPLASHR;
    else
        shot->frame = CK_SPR_SHOTSPLASHL;
}

void CVort3_contact_vortimom(CkSprite *vortimom, CkSprite *contacted)
{
    if (contacted->type_ != 15) /* keenshot */
        return;
    vortimom->health--;
    if (vortimom->health)
        return;
    CVort_engine_setCurSound(0x27);
    vortimom->time = 0;
    vortimom->varB = 2;
    vortimom->frame = SPR3_MOMDIE1;
    vortimom->contact = CK_CONTACT_NOP;
    vortimom->think = CK_THINK_KILL_SPRITE;
}

/* ---- Meep ----------------------------------------------------------------- */

void CVort3_think_meep_walk(void)
{
    u16 blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR3_MEEPR1;
    else
        TS.frame = SPR3_MEEPL1;
    TS.frame += (ck_ticks_lo >> 4) & 1;
    if (CVort_get_random() < (s16)ck_sprite_sync) {
        TS.varC = TS.vel_x;
        TS.vel_x = 0;
        TS.varB = 0;
        TS.time = 0;
        TS.think = CK_THINK_MEEP_SHOOT;
    }
    CVort_do_fall();
    blocking = (u16)CVort_compute_sprite_delta();
    /* do NOT fix! (an apparent vanilla bug: sings instead of turning) */
    if (!(blocking & 2)) {
        TS.varC = TS.vel_x;
        TS.vel_x = 0;
        TS.varB = 0;
        TS.time = 0;
        TS.think = CK_THINK_MEEP_SHOOT;
    }
    if (blocking & 4)
        TS.vel_x = -65;
    if (blocking & 1)
        TS.vel_x = 65;
}

void CVort3_think_meep_shoot(void)
{
    if (TS.varC < 0)
        TS.frame = SPR3_MEEPSINGL;
    else
        TS.frame = SPR3_MEEPSINGR;
    TS.time += (s16)ck_sprite_sync;
    if (TS.time < 60)
        return;
    if (!TS.varB) {
        s16 shotIndex;
        CkSprite *s;
        TS.varB = 1;
        CVort_engine_setCurSound(0x2B); /* snd_meep */
        shotIndex = CVort_add_sprite();
        s = &g_entities.sprites[shotIndex];
        if (TS.varC < 0) {
            s->pos_x = TS.pos_x;
            /* yes, again time == frame here! */
            s->time = SPR3_MEEPWAVEL1;
            s->frame = SPR3_MEEPWAVEL1;
            s->vel_x = -200;
        } else {
            s->pos_x = TS.pos_x + 0x1000;
            s->vel_x = 200;
            s->time = SPR3_MEEPWAVER1;
            s->frame = SPR3_MEEPWAVER1;
        }
        s->pos_y = TS.pos_y + 0x400;
        s->think = CK_THINK_MEEPSHOT;
        s->contact = CK_CONTACT_NOP;
        s->type_ = CK3_OBJ_MEEPSHOT;
    }
    if (TS.time <= 80)
        return;
    TS.vel_x = TS.varC;
    TS.think = CK_THINK_MEEP_WALK;
}

void CVort3_contact_meep(CkSprite *meep, CkSprite *contacted)
{
    if (contacted->type_ != 15)
        return;
    meep->health--;
    if (meep->health)
        return;
    CVort_engine_setCurSound(0x27);
    meep->time = 0;
    meep->varB = 2;
    meep->frame = SPR3_MEEPDIE1;
    meep->contact = CK_CONTACT_NOP;
    meep->think = CK_THINK_KILL_SPRITE;
}

void CVort3_think_meepshot(void)
{
    TS.frame = (u16)(TS.time + ((ck_ticks_lo >> 3) & 1));
    TS.del_x = TS.vel_x * (s16)ck_sprite_sync;
}

/* ---- Vortininja ------------------------------------------------------------ */

void CVort3_think_vortininja_stand(void)
{
    s32 yPosDiff;
    if (TS.pos_x < g_entities.sprites[0].pos_x) {
        TS.vel_x = 250;
        TS.frame = SPR3_NINJAR1;
    } else {
        TS.vel_x = -250;
        TS.frame = SPR3_NINJAL1;
    }
    TS.frame += (ck_ticks_lo >> 5) & 1;
    yPosDiff = g_entities.sprites[0].pos_y - TS.pos_y;
    if (yPosDiff > 20480L)
        return;
    if (yPosDiff < -20480L)
        return;
    if (CVort_get_random() >= (s16)(ck_sprite_sync * 3))
        return;
    TS.vel_y = -CVort_calc_jump_height(350);
    TS.think = CK_THINK_NINJA_JUMP;
}

void CVort3_think_vortininja_jump(void)
{
    if (TS.vel_x > 0)
        TS.frame = SPR3_NINJAJUMPR;
    else
        TS.frame = SPR3_NINJAJUMPL;
    CVort_do_fall();
    if (CVort_compute_sprite_delta() & 2)
        TS.think = CK_THINK_NINJA_STAND;
}

void CVort3_contact_vortininja(CkSprite *vortininja, CkSprite *contacted)
{
    s16 origHealth;
    if (contacted->type_ != 15)
        return;
    origHealth = vortininja->health;
    vortininja->health--;
    if (origHealth)
        return;
    CVort_engine_setCurSound(0x27);
    vortininja->time = 0;
    vortininja->varB = 2;
    vortininja->frame = SPR3_NINJADIE1;
    vortininja->contact = CK_CONTACT_NOP;
    vortininja->think = CK_THINK_KILL_SPRITE;
}

/* ---- Foob ------------------------------------------------------------------ */

void CVort3_think_foob_walk(void)
{
    s16 curr_tile_x, curr_tile_y, blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR3_FOOBR1;
    else
        TS.frame = SPR3_FOOBL1;
    TS.frame += (ck_ticks_lo >> 5) & 1;
    curr_tile_x = CK_W2T(TS.pos_x);
    curr_tile_y = CK_W2T(TS.pos_y);
    if ((curr_tile_x > scroll_x_tile) && (curr_tile_y > scroll_y_tile) &&
        (scroll_x_tile + 19 > curr_tile_x)) { /* ENGINE_VIEWPORT_MAX_Y_TILE */
        s16 currRand = 0, secondQuery = 0;
        if (CVort_get_random() < (s16)(ck_sprite_sync << 1))
            currRand = 1;
        if (scroll_y_tile + 12 > curr_tile_y) /* VIEWPORT_HEIGHT_TILES */
            secondQuery = 1;
        if (currRand & secondQuery) {
            TS.time = 0;
            TS.think = CK_THINK_FOOB_SCARED;
            CVort_engine_setCurSound(0x22); /* snd_yorpscream */
        }
    }
    CVort_do_fall();
    blocking = CVort_compute_sprite_delta();
    /* do NOT fix! (an apparent vanilla bug: near-zero turn speeds) */
    if (blocking & 4)
        TS.vel_x = -50;
    if (blocking & 1)
        TS.vel_x = 50;
}

void CVort3_think_foob_run(void)
{
    s16 blocking;
    if (TS.vel_x > 0)
        TS.frame = SPR3_FOOBR1;
    else
        TS.frame = SPR3_FOOBL1;
    TS.frame += (ck_ticks_lo >> 3) & 1;
    CVort_do_fall();
    blocking = CVort_compute_sprite_delta();
    /* the foob is now "chasing" into a wall */
    if (blocking & 1)
        TS.vel_x = -1;
    if (blocking & 4)
        TS.vel_x = 1;
}

void CVort3_think_foob_scared(void)
{
    TS.frame = (u16)(((ck_ticks_lo / 0xA) & 1) + SPR3_FOOBYELL1);
    TS.time += (s16)ck_sprite_sync;
    if (TS.time <= 24)
        return;
    if (TS.pos_x > g_entities.sprites[0].pos_x)
        TS.vel_x = 250;
    else
        TS.vel_x = -250;
    TS.think = CK_THINK_FOOB_RUN;
}

void CVort3_contact_foob(CkSprite *foob, CkSprite *contacted)
{
    /* keen, keenshot or enemyshot pops the foob */
    if (contacted->type_ != CK_OBJ_KEEN) {
        if (contacted->type_ != 15) {
            if (contacted->type_ != 16)
                return;
        }
    }
    CVort_engine_setCurSound(0x22);
    foob->time = 0;
    foob->varB = 3;
    foob->frame = SPR3_FOOBDIE1;
    foob->contact = CK_CONTACT_NOP;
    foob->think = CK_THINK_KILL_SPRITE;
}

/* ---- Jack & Ball ------------------------------------------------------------ */

void CVort3_think_jack(void)
{
    s16 blocking;
    TS.frame = (u16)(((ck_ticks_lo >> 3) & 3) + SPR3_JACK1);
    blocking = CVort_compute_sprite_delta();
    if (blocking & 4)
        TS.vel_x = -400;
    if (blocking & 1)
        TS.vel_x = 400;
    if (blocking & 2)
        TS.vel_y = -400;
    if (blocking & 8)
        TS.vel_y = 400;
}

void CVort3_think_ball(void)
{
    s16 blocking = CVort_compute_sprite_delta();
    if (blocking & 4)
        TS.vel_x = -400;
    if (blocking & 1)
        TS.vel_x = 400;
    if (blocking & 2)
        TS.vel_y = -400;
    if (blocking & 8)
        TS.vel_y = 400;
}

/* ---- Platform ----------------------------------------------------------------- */

void CVort3_think_platform_move(void)
{
    u16 blocking;
    TS.frame = (u16)(SPR3_PLATFORM1 + ((ck_ticks_lo >> 5) & 1));
    blocking = (u16)CVort_compute_sprite_delta();

    if (blocking & 5) {
        if (blocking & 1)
            TS.varB = 75;
        else
            TS.varB = -75;
        TS.vel_x = 0;
        TS.varC = 0;
        TS.time = 0;
        TS.think = CK_THINK_PLATFORM_TURN;
    }
    if (blocking & 8) {
        TS.varC = 75;
        TS.vel_y = 0;
        TS.varB = 0;
        TS.time = 0;
        TS.think = CK_THINK_PLATFORM_TURN;
    } else if (blocking & 2) {
        TS.vel_y = -75; /* a quick bump */
    }
}

void CVort3_think_platform_turn(void)
{
    TS.time += (s16)ck_sprite_sync;
    if (TS.time > 75) {
        TS.vel_x = TS.varB;
        TS.vel_y = TS.varC;
        TS.think = CK_THINK_PLATFORM_MOVE;
    }
}

/* ---- fixed enemy guns ------------------------------------------------------------ */

void CVort3_think_enemyshot(void)
{
    if (!CVort_compute_sprite_delta())
        return;
    CVort_engine_setCurSound(0x25);
    TS.type_ = CK_OBJ_ZAPZOT;
    TS.think = CK_THINK_ZAPZOT;
    TS.time = 0;
    if (CVort_get_random() > 0x80)
        TS.frame = CK_SPR_SHOTSPLASHR;
    else
        TS.frame = CK_SPR_SHOTSPLASHL;
}

void CVort3_think_enemygun_right(CkBody *enemygun)
{
    s16 i;
    CkSprite *s;
    enemygun->variant += (s16)ck_sprite_sync;
    if (enemygun->variant < 400)
        return;
    enemygun->variant -= 400;
    CVort_engine_setCurSound(0x26);

    i = CVort_add_sprite();
    s = &g_entities.sprites[i];
    s->frame = SPR3_TANKSHOT;
    s->vel_x = 400;
    s->pos_x = enemygun->tile_x;
    s->pos_y = enemygun->tile_y + 0x300;
    s->think = CK_THINK_ENEMYSHOT;
    s->contact = CK_CONTACT_NOP;
    s->type_ = CK3_OBJ_ENEMYSHOT;
}

void CVort3_think_enemygun_down(CkBody *enemygun)
{
    s16 i;
    CkSprite *s;
    enemygun->variant += (s16)ck_sprite_sync;
    if (enemygun->variant < 400)
        return;
    enemygun->variant -= 400;
    CVort_engine_setCurSound(0x26);

    i = CVort_add_sprite();
    s = &g_entities.sprites[i];
    s->frame = SPR3_TANKSHOTV;
    s->vel_y = 400;
    s->pos_x = enemygun->tile_x + 0x400;
    s->pos_y = enemygun->tile_y;
    s->think = CK_THINK_ENEMYSHOT;
    s->contact = CK_CONTACT_NOP;
    s->type_ = CK3_OBJ_ENEMYSHOT;
}

/* ---- sparks + heart (the Mangling Machine's vitals) ------------------------------ */

void CVort3_think_spark(void)
{
    TS.frame = (u16)(((ck_ticks_lo >> 3) & 3) + SPR3_SPARK1);
}

void CVort3_contact_spark(CkSprite *spark, CkSprite *contacted)
{
    s16 bodyIndex;
    if (contacted->type_ != 15)
        return;
    spark->type_ = 0;
    /* desktop adds a border-flash body here; no EGA border on SNES */
    CVort_add_score(1000);
    g_game.spark_counter++;
    if (g_game.spark_counter != 6)
        return;

    for (bodyIndex = 0; bodyIndex < g_entities.num_bodies; bodyIndex++) {
        if (g_entities.bodies[bodyIndex].type_ == CK3_BOD_MANGLING_ARM)
            g_entities.bodies[bodyIndex].type_ = 0;
    }
    bodyIndex = CVort_add_body();
    g_entities.bodies[bodyIndex].type_ = CK3_BOD_MANGLING_ARM_DESTRUCT;
    g_entities.bodies[bodyIndex].think = CK_BODY_MANGLING_ARM_DESTRUCT;
    g_entities.bodies[bodyIndex].tile_y = 6;
    g_entities.bodies[bodyIndex].variant = 0;
    g_entities.bodies[bodyIndex].field_C = 0;
}

void CVort3_think_heart(void)
{
    TS.frame = (u16)((((ck_ticks_lo & 0xFF) / 24) & 1) + SPR3_HEART1);
}

void CVort3_contact_heart(CkSprite *heart, CkSprite *contacted)
{
    s16 bodyIndex;
    if (contacted->type_ != 15)
        return;
    heart->type_ = 0;
    /* border-flash body skipped (no EGA border) */
    CVort_add_score(1000);

    for (bodyIndex = 0; bodyIndex < g_entities.num_bodies; bodyIndex++) {
        if (g_entities.bodies[bodyIndex].type_ == CK3_BOD_MANGLING_LEG)
            g_entities.bodies[bodyIndex].type_ = 0;
    }
    bodyIndex = CVort_add_body();
    g_entities.bodies[bodyIndex].type_ = CK3_BOD_MANGLING_DESTRUCT;
    g_entities.bodies[bodyIndex].think = CK_BODY_MANGLING_DESTRUCT;
    g_entities.bodies[bodyIndex].tile_y = 2;
    g_entities.bodies[bodyIndex].variant = 0;
    g_entities.bodies[bodyIndex].field_C = 0;
    g_entities.num_sprites = 0; /* vanilla: everything vanishes */
}

/* ---- Mangling Machine destruction -------------------------------------------------- */

static void CVort3_destory_mangling_tile(u16 tileX, u16 tileY, u16 tileId)
{
    s16 i;
    CkSprite *s;
    CVort_engine_setCurSound(0x25);
    i = CVort_add_sprite();
    s = &g_entities.sprites[i];
    s->think = CK_THINK_ZAPZOT;
    s->type_ = CK_OBJ_DEAD; /* yeah, NOT zapzot */
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->time = 0;
    s->contact = CK_CONTACT_NOP;
    if (CVort_get_random() > 0x80)
        s->frame = CK_SPR_SHOTSPLASHR;
    else
        s->frame = CK_SPR_SHOTSPLASHL;
    ck_map_set(tileX, tileY, tileId);
}

void CVort3_think_mangling_arm_destruct(CkBody *body)
{
    s16 curr_tile_y, curr_tile_x;
    body->variant += (s16)ck_sprite_sync;
    if (body->variant < 30)
        return;
    body->variant -= 30;
    body->tile_y++;
    if (body->tile_y == 19) {
        body->type_ = 0;
        return;
    }
    curr_tile_y = (s16)(body->tile_y & 0xFF);
    for (curr_tile_x = 5; curr_tile_x <= 7; curr_tile_x++) {
        if (map_data_tiles[ck_rowofs[(u16)curr_tile_y] + (u16)curr_tile_x]
            != 0xA9)
            CVort3_destory_mangling_tile((u16)curr_tile_x, (u16)curr_tile_y,
                                         0xA9);
    }
    for (curr_tile_x = 17; curr_tile_x <= 19; curr_tile_x++) {
        if (map_data_tiles[ck_rowofs[(u16)curr_tile_y] + (u16)curr_tile_x]
            != 0xA9)
            CVort3_destory_mangling_tile((u16)curr_tile_x, (u16)curr_tile_y,
                                         0xA9);
    }
}

void CVort3_think_mangling_destruct(CkBody *body)
{
    s16 curr_tile_y, curr_tile_x;
    body->variant += (s16)ck_sprite_sync;
    if (body->variant < 20)
        return;
    body->variant -= 20;
    body->tile_y++;
    if (body->tile_y == 24) {
        g_game.level_finished = CK_LEVEL_END_EXIT;
        body->type_ = 0;
        return;
    }
    if (body->tile_y >= 19)
        return;

    curr_tile_y = (s16)(body->tile_y & 0xFF);
    for (curr_tile_x = 8; curr_tile_x <= 16; curr_tile_x++) {
        if (map_data_tiles[ck_rowofs[(u16)curr_tile_y] + (u16)curr_tile_x]
            != 0xA9)
            CVort3_destory_mangling_tile((u16)curr_tile_x, (u16)curr_tile_y,
                                         0xA9);
    }
}

void CVort3_think_mangling_arm(CkBody *arm)
{
    /* the desktop stores truncated 16-bit tile coords; ours are s32 */
    u16 curr_tile_x = (u16)arm->tile_x, curr_tile_y = (u16)arm->tile_y;
    arm->field_C += (s16)ck_sprite_sync;
    if (arm->field_C < arm->field_E)
        return;
    arm->field_C -= arm->field_E;
    ck_map_set(curr_tile_x, curr_tile_y, 0xA9);
    ck_map_set((u16)(curr_tile_x - 1), curr_tile_y, 0xA9);
    ck_map_set((u16)(curr_tile_x - 1), (u16)(curr_tile_y + 1), 0xA9);
    ck_map_set((u16)(curr_tile_x - 1), (u16)(curr_tile_y + 2), 0xA9);
    ck_map_set((u16)(curr_tile_x + 1), curr_tile_y, 0xA9);
    ck_map_set((u16)(curr_tile_x + 1), (u16)(curr_tile_y + 1), 0xA9);
    ck_map_set((u16)(curr_tile_x + 1), (u16)(curr_tile_y + 2), 0xA9);

    if (arm->variant == -1) {
        if (map_data_tiles[ck_rowofs[(u16)(curr_tile_y - 1)] + curr_tile_x]
            != 0x255) {
            arm->variant = 1;
            arm->field_E = 15;
        } else {
            curr_tile_y--;
            arm->tile_y--;
        }
    } else {
        if (map_data_tiles[ck_rowofs[(u16)(curr_tile_y + 3)] + curr_tile_x]
            == 0x1D7) {
            arm->variant = -1;
            arm->field_E = 40;
        } else {
            ck_map_set(curr_tile_x, curr_tile_y, 0x255);
            curr_tile_y++;
            arm->tile_y++;
        }
    }
    ck_map_set(curr_tile_x, curr_tile_y, 0x255);
    ck_map_set((u16)(curr_tile_x - 1), curr_tile_y, 0x26A);
    ck_map_set((u16)(curr_tile_x - 1), (u16)(curr_tile_y + 1), 0x26C);
    ck_map_set((u16)(curr_tile_x - 1), (u16)(curr_tile_y + 2), 0x26B);
    ck_map_set((u16)(curr_tile_x + 1), curr_tile_y, 0x26A);
    ck_map_set((u16)(curr_tile_x + 1), (u16)(curr_tile_y + 1), 0x26C);
    ck_map_set((u16)(curr_tile_x + 1), (u16)(curr_tile_y + 2), 0x26B);
}

void CVort3_think_mangling_leg_moving(CkBody *leg)
{
    u16 curr_tile_x = (u16)leg->tile_x, curr_tile_y = (u16)leg->tile_y;
    leg->field_C += (s16)ck_sprite_sync;
    if (leg->field_C < 35)
        return;
    leg->field_C -= 35;
    ck_map_set(curr_tile_x, curr_tile_y, 0xA9);

    if (leg->field_E == -1) {
        ck_map_set((u16)(curr_tile_x - 1), curr_tile_y, 0xA9);
        ck_map_set((u16)(curr_tile_x - 2), curr_tile_y, 0xA9);
        ck_map_set((u16)(curr_tile_x - 3), curr_tile_y, 0xA9);
    } else {
        ck_map_set((u16)(curr_tile_x + 1), curr_tile_y, 0xA9);
        ck_map_set((u16)(curr_tile_x + 2), curr_tile_y, 0xA9);
        ck_map_set((u16)(curr_tile_x + 3), curr_tile_y, 0xA9);
    }
    if (leg->variant == -1) {
        if (map_data_tiles[ck_rowofs[(u16)(curr_tile_y - 1)] + curr_tile_x]
            != 0x255) {
            leg->variant = 1;
        } else {
            curr_tile_y--;
            leg->tile_y--;
        }
    } else {
        if (map_data_tiles[ck_rowofs[(u16)(curr_tile_y + 1)] + curr_tile_x]
            == 0x1AE) {
            leg->variant = -1;
            CVort_engine_setCurSound(0x2D); /* snd_footslam */
            leg->think = CK_BODY_MANGLING_LEG_WAIT;
            leg->field_C = 0;
        } else {
            ck_map_set(curr_tile_x, curr_tile_y, 0x255);
            curr_tile_y++;
            leg->tile_y++;
        }
    }
    if (leg->field_E == -1) {
        ck_map_set(curr_tile_x, curr_tile_y, 0x26E);
        ck_map_set((u16)(curr_tile_x - 1), curr_tile_y, 0x26F);
        ck_map_set((u16)(curr_tile_x - 2), curr_tile_y, 0x26F);
        ck_map_set((u16)(curr_tile_x - 3), curr_tile_y, 0x26D);
    } else {
        ck_map_set(curr_tile_x, curr_tile_y, 0x26D);
        ck_map_set((u16)(curr_tile_x + 1), curr_tile_y, 0x26F);
        ck_map_set((u16)(curr_tile_x + 2), curr_tile_y, 0x26F);
        ck_map_set((u16)(curr_tile_x + 3), curr_tile_y, 0x26E);
    }
}

void CVort3_think_mangling_leg_awaiting(CkBody *leg)
{
    leg->field_C += (s16)ck_sprite_sync;
    if (leg->field_C > 200) {
        leg->field_C = 0;
        leg->think = CK_BODY_MANGLING_LEG_MOVE;
    }
}

/* ---- the Grand Intellect reveal (CVort3_handle_grand_intellect) ---------
 * Text-box rendition of the level-16 cutscene; the level stays visible
 * behind the BG3 boxes. */
void ck_ep3_grand_intellect(void)
{
    g_game.spark_counter = 0;
    CVort_engine_setCurSound(0x2C); /* snd_mortimer */
    ck_ui_message("NO... IT CAN'T BE!", "MORTIMER MCMIRE!!!");
    ck_ui_message("MORTIMER HAS BEEN A THORN IN",
                  "YOUR SIDE ALL YOUR LIFE.");
    ck_ui_message("YOUR IQ SCORE WAS 314 --",
                  "MORTIMER'S WAS 315.");
    ck_ui_message("\"ALL RIGHT MORTIMER, WHY", "DESTROY EARTH?\"");
    ck_ui_message("\"YOU MENTAL WIMPS DESERVE",
                  "TO DIE, MISTER THREE FOURTEEN!\"");
    ck_ui_message("\"YOU'LL NEVER GET PAST MY",
                  "HIDEOUS MANGLING MACHINE!\"");
}

#endif /* CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3 */
