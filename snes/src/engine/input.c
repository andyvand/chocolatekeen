#include "engine/input.h"

CkInput g_ck_input;

/* 816-tcc/pvsneslib do not zero-init RAM sections and initializers on
 * statics may never be applied (they can land in uncopied RAM banks).
 * ALL runtime state must be set here at runtime. */

static u16 s_prev;

void ck_input_init(void)
{
    u8 *p = (u8 *)&g_ck_input;
    u8 i;
    for (i = 0; i < sizeof(g_ck_input); i++)
        p[i] = 0;
    s_prev = 0;
}

void ck_input_update(void)
{
    u16 pad = padsCurrent(0);
    u16 newly = (u16)(pad & ~s_prev);

    g_ck_input.left  = (pad & KEY_LEFT)  ? 1 : 0;
    g_ck_input.right = (pad & KEY_RIGHT) ? 1 : 0;
    g_ck_input.up    = (pad & KEY_UP)    ? 1 : 0;
    g_ck_input.down  = (pad & KEY_DOWN)  ? 1 : 0;

    g_ck_input.jump = (pad & KEY_B) ? 1 : 0;
    g_ck_input.pogo = (pad & KEY_Y) ? 1 : 0;
    g_ck_input.fire = ((pad & KEY_X) || ((pad & KEY_B) && (pad & KEY_Y)))
                          ? 1 : 0;

    g_ck_input.jump_pressed   = (newly & KEY_B) ? 1 : 0;
    g_ck_input.pogo_pressed   = (newly & KEY_Y) ? 1 : 0;
    g_ck_input.fire_pressed   = (newly & KEY_X) ? 1 : 0;
    g_ck_input.start_pressed  = (newly & KEY_START) ? 1 : 0;
    g_ck_input.select_pressed = (newly & KEY_SELECT) ? 1 : 0;
    g_ck_input.l_pressed      = (newly & KEY_L) ? 1 : 0;
    g_ck_input.r_pressed      = (newly & KEY_R) ? 1 : 0;

    s_prev = pad;
}
