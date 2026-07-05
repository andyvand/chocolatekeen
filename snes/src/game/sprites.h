/* SNES port of the Vorticons entity model (see src/game/sprites.h in the
 * desktop tree). Function pointers become u8 IDs dispatched through ROM
 * tables (816-tcc friendly; the original code compares think pointers,
 * which maps 1:1 to comparing IDs).
 */
#ifndef CK_SNES_GAME_SPRITES_H
#define CK_SNES_GAME_SPRITES_H

#include <snes.h>

/* Think/contact IDs. THINK_NONE slots are inactive/no-op. */
enum {
    CK_THINK_NOP = 0,
    CK_THINK_KEEN_GROUND,
    CK_THINK_KEEN_JUMP,        /* CVort_think_keen_jump_ground */
    CK_THINK_KEEN_POGO,        /* CVort_think_keen_pogo_ground */
    CK_THINK_KEEN_DEATH,
    CK_THINK_DEAD_SPRITE,
    CK_THINK_KILL_SPRITE,
    CK_THINK_REMOVE_SPRITE,
    CK_THINK_SHOT,             /* CVort_think_keengun */
    CK_THINK_KEEN_JUMP_AIR,    /* CVort_think_keen_jump_air */
    CK_THINK_KEEN_POGO_AIR,    /* CVort_think_keen_pogo_air */
    CK_THINK_KEEN_SHOOT,       /* CVort_think_keen_shoot */
    CK_THINK_KEEN_EXIT,        /* CVort_think_keen_exit */
    CK_THINK_ZAPZOT,           /* CVort_think_zapzot (shot splash) */
    /* shared enemy thinks (src/game/enemies.c) */
    CK_THINK_VORT_WALK,        /* CVort_think_vorticon_walk */
    CK_THINK_VORT_JUMP,        /* CVort_think_vorticon_jump */
    CK_THINK_VORT_SEARCH,      /* CVort_think_vorticon_search */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    /* episode 1 thinks (src/episodes/episode1.c) */
    CK_THINK_KEEN_FROZEN,      /* CVort1_think_keen_frozen */
    CK_THINK_YORP_WALK,
    CK_THINK_YORP_LOOK,
    CK_THINK_YORP_STUNNED,
    CK_THINK_GARG_MOVE,
    CK_THINK_GARG_LOOK,
    CK_THINK_BUTLER_WALK,
    CK_THINK_BUTLER_TURN,
    CK_THINK_TANK_MOVE,
    CK_THINK_TANK_SPAWN,
    CK_THINK_TANK_TURN,
    CK_THINK_TANK_SHOOT,
    CK_THINK_CUBE,             /* CVort1_think_cube */
    CK_THINK_CUBETTE,          /* CVort1_think_cubette_flight */
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    /* episode 2 thinks (src/episodes/episode2.c + enemies.c) */
    CK_THINK_KEEN_STUNNED,     /* CVort_think_keen_stunned */
    CK_THINK_YOUTH_WALK,       /* CVort_think_youth_walk */
    CK_THINK_YOUTH_JUMP,       /* CVort_think_youth_jump */
    CK_THINK_ELITE_WALK,       /* CVort2_think_elite_walk */
    CK_THINK_ELITE_SHOOT,      /* CVort2_think_elite_shoot */
    CK_THINK_ELITE_JUMP,       /* CVort2_think_elite_jump */
    CK_THINK_GUARD_MOVE,       /* CVort2_think_guardbot_move */
    CK_THINK_GUARD_SHOOT,      /* CVort2_think_guardbot_shoot */
    CK_THINK_GUARD_TURN,       /* CVort2_think_guardbot_turn */
    CK_THINK_SCRUB_LEFT,       /* CVort2_think_scrub_walk_left */
    CK_THINK_SCRUB_DOWN,       /* CVort2_think_scrub_walk_down */
    CK_THINK_SCRUB_RIGHT,      /* CVort2_think_scrub_walk_right */
    CK_THINK_SCRUB_UP,         /* CVort2_think_scrub_walk_up */
    CK_THINK_SCRUB_FALL,       /* CVort2_think_scrub_fall */
    CK_THINK_PLATFORM_MOVE,    /* CVort2_think_platform_move */
    CK_THINK_PLATFORM_TURN,    /* CVort2_think_platform_turn */
    CK_THINK_TANTALUS,         /* CVort2_think_tantalus */
#else
    /* episode 3 thinks (src/episodes/episode3.c + enemies.c) */
    CK_THINK_KEEN_STUNNED,     /* CVort_think_keen_stunned */
    CK_THINK_YOUTH_WALK,       /* CVort_think_youth_walk */
    CK_THINK_YOUTH_JUMP,       /* CVort_think_youth_jump */
    CK_THINK_MOM_WALK,         /* CVort3_think_vortimom_walk */
    CK_THINK_MOM_SHOOT,        /* CVort3_think_vortimom_shoot */
    CK_THINK_MOMSHOT,          /* CVort3_think_vortimomshot */
    CK_THINK_MEEP_WALK,        /* CVort3_think_meep_walk */
    CK_THINK_MEEP_SHOOT,       /* CVort3_think_meep_shoot */
    CK_THINK_MEEPSHOT,         /* CVort3_think_meepshot */
    CK_THINK_NINJA_STAND,      /* CVort3_think_vortininja_stand */
    CK_THINK_NINJA_JUMP,       /* CVort3_think_vortininja_jump */
    CK_THINK_FOOB_WALK,        /* CVort3_think_foob_walk */
    CK_THINK_FOOB_RUN,         /* CVort3_think_foob_run */
    CK_THINK_FOOB_SCARED,      /* CVort3_think_foob_scared */
    CK_THINK_JACK,             /* CVort3_think_jack */
    CK_THINK_BALL,             /* CVort3_think_ball */
    CK_THINK_PLATFORM_MOVE,    /* CVort3_think_platform_move */
    CK_THINK_PLATFORM_TURN,    /* CVort3_think_platform_turn */
    CK_THINK_ENEMYSHOT,        /* CVort3_think_enemyshot */
    CK_THINK_SPARK,            /* CVort3_think_spark */
    CK_THINK_HEART,            /* CVort3_think_heart */
#endif
    CK_THINK_MAX
};

enum {
    CK_CONTACT_NOP = 0,
    CK_CONTACT_KEEN,           /* CVort<n>_contact_keen */
    CK_CONTACT_KEENGUN,        /* CVort_contact_keengun */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    CK_CONTACT_VORTICON,       /* CVort_contact_vorticon */
    CK_CONTACT_TANKSHOT,       /* CVort_contact_tankshot */
    CK_CONTACT_YORP,
    CK_CONTACT_GARG,
    CK_CONTACT_BUTLER,
    CK_CONTACT_CUBE,
    CK_CONTACT_CHAIN,
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    CK_CONTACT_VORTICON,       /* CVort_contact_vorticon */
    CK_CONTACT_TANKSHOT,       /* CVort_contact_tankshot */
    CK_CONTACT_YOUTH,          /* CVort_contact_youth */
    CK_CONTACT_ELITE,          /* CVort2_contact_elite */
    CK_CONTACT_GUARD,          /* CVort2_contact_guardbot (no-op) */
    CK_CONTACT_SCRUB,          /* CVort2_contact_scrub */
    CK_CONTACT_TANTALUS,       /* CVort2_contact_tantalus */
#else
    CK_CONTACT_VORTICON,       /* CVort_contact_vorticon */
    CK_CONTACT_YOUTH,          /* CVort_contact_youth */
    CK_CONTACT_MOM,            /* CVort3_contact_vortimom */
    CK_CONTACT_MOMSHOT,        /* CVort3_contact_vortimomshot */
    CK_CONTACT_MEEP,           /* CVort3_contact_meep */
    CK_CONTACT_NINJA,          /* CVort3_contact_vortininja */
    CK_CONTACT_FOOB,           /* CVort3_contact_foob */
    CK_CONTACT_SPARK,          /* CVort3_contact_spark */
    CK_CONTACT_HEART,          /* CVort3_contact_heart */
#endif
    CK_CONTACT_MAX
};

/* Body think IDs (Body_T::think_ptr on desktop). */
enum {
    CK_BODY_NOP = 0,
    CK_BODY_SLIDE_DOOR,        /* CVort_body_slide_door */
    CK_BODY_BRIDGE_EXTEND,     /* CVort_body_bridge_extend */
    CK_BODY_BRIDGE_RETRACT,    /* CVort_body_bridge_retract */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    CK_BODY_ICE_CANNON,        /* CVort1_body_ice_cannon */
    CK_BODY_SHOT_CHAIN,        /* CVort1_body_shot_chain */
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    CK_BODY_DESTROY_TANTALUS,  /* CVort2_body_destroy_tantalus */
#else
    CK_BODY_ENEMYGUN_R,        /* CVort3_think_enemygun_right */
    CK_BODY_ENEMYGUN_D,        /* CVort3_think_enemygun_down */
    CK_BODY_MANGLING_ARM,      /* CVort3_think_mangling_arm */
    CK_BODY_MANGLING_LEG_MOVE, /* CVort3_think_mangling_leg_moving */
    CK_BODY_MANGLING_LEG_WAIT, /* CVort3_think_mangling_leg_awaiting */
    CK_BODY_MANGLING_ARM_DESTRUCT, /* CVort3_think_mangling_arm_destruct */
    CK_BODY_MANGLING_DESTRUCT, /* CVort3_think_mangling_destruct */
#endif
    CK_BODY_MAX
};

typedef struct CkSprite_T {
    u16 type_;
    u16 active;
    s32 pos_x, pos_y;              /* world units: 4096 per 16px tile */
    s32 box_x1, box_y1, box_x2, box_y2;
    s16 del_x, del_y, vel_x, vel_y;
    s16 health;
    u16 varA;
    u16 frame;
    s16 time;
    s16 varB;
    s16 varC;
    s16 varD;
    u8 think;                      /* CK_THINK_* */
    u8 contact;                    /* CK_CONTACT_* */
} CkSprite;

#define CK_MAX_SPRITES 0x50
#define CK_MAX_BODIES  0x10

typedef struct CkBody_T {
    s32 tile_x, tile_y;
    u16 type_;
    s16 variant;
    s16 field_C, field_E, field_10, field_12, field_14, field_16,
        field_18, field_1A, field_1C, field_1E, field_20;
    u8 think;                      /* body think id */
} CkBody;

typedef struct CkEntities_T {
    CkSprite sprites[CK_MAX_SPRITES];
    CkSprite temp_sprite;
    CkBody bodies[CK_MAX_BODIES];
    s16 num_sprites, num_bodies;
} CkEntities;

extern CkEntities g_entities;

#endif
