#include "engine/save.h"

#define CK_SAVE_HDR_OFS  0x0000
#define CK_SAVE_SLOT_OFS 0x0010
#define CK_SAVE_SLOT_SZ  128

static const u8 s_magic[6] = { 'C', 'K', 'S', '1',
                               2 /* format version (2: +targetsMask) */,
                               CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE };

static u16 slot_offset(u8 slot)
{
    return (u16)(CK_SAVE_SLOT_OFS + (u16)slot * CK_SAVE_SLOT_SZ);
}

static u16 slot_checksum(const CkSaveSlot *s)
{
    const u8 *p = (const u8 *)s;
    u16 sum = 0x1234;
    u16 i;
    /* everything up to (not including) the checksum field */
    for (i = 0; i + 2 < sizeof(CkSaveSlot); i++)
        sum += p[i];
    return sum;
}

void ck_save_init(void)
{
    u8 hdr[6];
    u8 i, ok;

    consoleLoadSramWithOffset(hdr, 6, CK_SAVE_HDR_OFS);
    ok = 1;
    for (i = 0; i < 6; i++) {
        if (hdr[i] != s_magic[i])
            ok = 0;
    }
    if (ok)
        return;

    /* Fresh/foreign SRAM: write header and erase all slots. */
    consoleCopySramWithOffset((u8 *)s_magic, 6, CK_SAVE_HDR_OFS);
    for (i = 0; i < CK_SAVE_SLOTS; i++)
        ck_save_erase(i);
}

u8 ck_save_slot_used(u8 slot)
{
    CkSaveSlot tmp;
    return ck_save_read(slot, &tmp);
}

u8 ck_save_read(u8 slot, CkSaveSlot *out)
{
    if (slot >= CK_SAVE_SLOTS)
        return 0;
    consoleLoadSramWithOffset((u8 *)out, sizeof(CkSaveSlot),
                              slot_offset(slot));
    if (out->checksum != slot_checksum(out))
        return 0;
    return 1;
}

void ck_save_write(u8 slot, const CkSaveSlot *in)
{
    CkSaveSlot tmp;
    const u8 *src = (const u8 *)in;
    u8 *dst = (u8 *)&tmp;
    u16 i;

    if (slot >= CK_SAVE_SLOTS)
        return;
    for (i = 0; i < sizeof(CkSaveSlot); i++)
        dst[i] = src[i];
    tmp.checksum = slot_checksum(&tmp);
    consoleCopySramWithOffset((u8 *)&tmp, sizeof(CkSaveSlot),
                              slot_offset(slot));
}

void ck_save_erase(u8 slot)
{
    CkSaveSlot z;
    u8 *p = (u8 *)&z;
    u16 i;
    for (i = 0; i < sizeof(CkSaveSlot); i++)
        p[i] = 0xFF;             /* invalid checksum by construction */
    if (slot >= CK_SAVE_SLOTS)
        return;
    consoleCopySramWithOffset((u8 *)&z, sizeof(CkSaveSlot),
                              slot_offset(slot));
}
