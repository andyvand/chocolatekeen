#include "engine/audio.h"
#include "data_format.h"
#include "snes_data_gen.h"

/* APU I/O ports (CPU side). */
#define APU_P0 (*(vuint8 *)0x2140)
#define APU_P1 (*(vuint8 *)0x2141)
#define APU_P2 (*(vuint8 *)0x2142)
#define APU_P3 (*(vuint8 *)0x2143)
#define APU_P23 (*(vuint16 *)0x2142)

/* SPC RAM layout - keep in sync with spc/driver.asm. */
#define SPC_DRIVER_ADDR 0x0200
#define SPC_ENTRY_ADDR  0x0300
#define SPC_BRR32_ADDR  0x0600
#define SPC_BRR8_ADDR   0x0620
#define SPC_SOUNDS_ADDR 0x0700

extern char spcdriver, spcdriver_end; /* .incbin in data.asm */

static u8 s_ready;
static u8 s_seq;      /* bit 7 sequence flag, toggled per command */
volatile u16 ck_dbg_sndcalls;  /* sticky: commands actually sent */
volatile u16 ck_dbg_sndreq;    /* sticky: ck_audio_play entries  */

/* Bounded wait for a port-0 echo. Returns 1 on match. */
static u8 wait_p0(u8 value)
{
    u16 spin = 0;
    while (APU_P0 != value) {
        spin++;
        if (spin == 0)
            return 0; /* 65536 spins - timeout */
    }
    return 1;
}

/* One IPL-ROM block transfer. First block starts with $CC after the
 * AA/BB boot banner; follow-up blocks reuse the last index + 2. */
static u8 ipl_send_block(const u8 *src, u16 len, u16 dest, u8 first)
{
    u16 i;
    u8 idx;

    APU_P23 = dest;
    APU_P1 = 1;                 /* non-zero: more data follows */
    if (first) {
        APU_P0 = 0xCC;
        if (!wait_p0(0xCC))
            return 0;
    } else {
        u8 kick = (u8)(APU_P0 + 2);
        if (kick == 0)
            kick = 2;           /* must be non-zero to start a block */
        APU_P0 = kick;
        if (!wait_p0(kick))
            return 0;
    }

    idx = 0;
    for (i = 0; i < len; i++) {
        APU_P1 = src[i];
        APU_P0 = idx;
        if (!wait_p0(idx))
            return 0;
        idx++;
    }
    return 1;
}

/* Finish the IPL upload: jump to entry. */
static void ipl_start(u16 entry)
{
    u8 kick = (u8)(APU_P0 + 2);
    if (kick == 0)
        kick = 2;
    APU_P23 = entry;
    APU_P1 = 0;                 /* zero: no more data, jump */
    APU_P0 = kick;
}

u8 ck_audio_init(void)
{
    u16 drvLen = (u16)(&spcdriver_end - &spcdriver);

    s_ready = 0;
    s_seq = 0;
    ck_dbg_sndcalls = 0;
    ck_dbg_sndreq = 0;

    /* IPL boot banner: SPC writes $AA/$BB after reset. */
    if (!wait_p0(0xAA))
        return 0;

    if (!ipl_send_block((const u8 *)&spcdriver, drvLen, SPC_DRIVER_ADDR, 1))
        return 0;
    if (!ipl_send_block(ck_brr_square32, 18, SPC_BRR32_ADDR, 0))
        return 0;
    if (!ipl_send_block(ck_brr_square8, 9, SPC_BRR8_ADDR, 0))
        return 0;
    if (!ipl_send_block(ck_sounds_bin, ck_sounds_bin_len, SPC_SOUNDS_ADDR, 0))
        return 0;

    ipl_start(SPC_ENTRY_ADDR);

    /* Driver writes $7F to port 0 when its main loop is up. */
    if (!wait_p0(0x7F))
        return 0;

    s_ready = 1;
    return 1;
}

/* Priority of a 0-based sound index from the baked header:
 * u16 count; count * { u16 ofs, u8 priority, u8 pad }. */
static u8 sound_priority(u8 idx0)
{
    return ck_sounds_bin[2 + ((u16)idx0 << 2) + 2];
}

void ck_audio_play(u16 sound)
{
    u16 count;
    u8 cur;

    ck_dbg_sndreq++;
    if (!s_ready)
        return;
    if (sound == 0)
        return;
    sound--;                    /* DOS indexes from 1 */
    count = *(const u16 *)ck_sounds_bin;
    if (sound >= count)
        return;

    /* Original gate: while something plays, only sounds with priority
     * >= the playing sound's priority may preempt (engine_audio_gba.c). */
    cur = APU_P1;               /* driver status: soundIndex+1 or 0 */
    if (cur != 0) {
        if (sound_priority((u8)(cur - 1)) > sound_priority((u8)sound))
            return;
    }

    s_seq ^= 0x80;
    APU_P0 = (u8)(((u8)(sound + 1) & 0x7F) | s_seq);
    ck_dbg_sndcalls++;
}

void ck_audio_stop(void)
{
    if (!s_ready)
        return;
    s_seq ^= 0x80;
    APU_P0 = (u8)(0x00 | s_seq);
}

u8 ck_audio_status(void)
{
    if (!s_ready)
        return 0;
    return APU_P1;
}
