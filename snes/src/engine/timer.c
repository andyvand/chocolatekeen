#include "engine/timer.h"

u16 ck_sprite_sync;
u32 ck_ticks;

/* 8.8 fixed-point game ticks per video frame. */
#define CK_TICKS_PER_FRAME_NTSC 620u
#define CK_TICKS_PER_FRAME_PAL  746u

static u16 s_acc;            /* 8.8 accumulator, fractional carry */
static u16 s_perFrame;

void ck_timer_init(void)
{
    /* snes_50hz is set by the pvsneslib crt0 from the console region. */
    s_perFrame = snes_50hz ? CK_TICKS_PER_FRAME_PAL
                           : CK_TICKS_PER_FRAME_NTSC;
    s_acc = 0;
    ck_ticks = 0;
    ck_sprite_sync = 0;
}

void ck_timer_frame(u8 framesElapsed)
{
    u16 whole;

    while (framesElapsed--)
        s_acc += s_perFrame;

    whole = s_acc >> 8;
    s_acc &= 0xFF;

    ck_ticks += whole;
    ck_sprite_sync = whole;
    if (ck_sprite_sync > 15)
        ck_sprite_sync = 15;
}
