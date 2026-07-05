/* Chocolate Keen SNES port - entry point.
 *
 * M5: boot into the game flow driver (gameflow.c): world map (level
 * 80) <-> level play, lives, ep1 win trigger and game over. Menus,
 * dialogs and the real ending screens are M6.
 */
#include <snes.h>

#include "data_format.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/msprite.h"
#include "engine/input.h"
#include "engine/timer.h"
#include "engine/audio.h"
#include "engine/text.h"
#include "engine/save.h"
#include "game/game_state.h"
#include "game/gameplay.h"
#include "game/gameflow.h"

/* Live-debug mirror of the SPC status port (readable via emulator RAM). */
volatile u8 ck_dbg_audio;

int main(void)
{
    consoleInit();

    /* BG3 priority-high: the text layer draws above BG1 and sprites,
     * matching the DOS overlay UI. */
    setMode(BG_MODE1, BG3_MODE1_PRIORITY_HIGH);
    bgSetDisable(1);
    bgSetDisable(2);
    bgSetGfxPtr(0, CK_BG1_CHR_VRAM);
    bgSetMapPtr(0, CK_BG1_MAP_VRAM, SC_64x32);

    ck_timer_init();
    ck_msprite_init();
    ck_input_init();
    ck_game_state_init();
    ck_render_anim_init(TILEINFO_Anim, CK_TILENUM);
    ck_text_init();              /* forced blank here at boot */
    ck_dbg_audio = (u8)(0x80 | ck_audio_init()); /* bit7: init attempted */
    ck_save_init();

    ck_gameflow_run();           /* never returns */
    return 0;
}
