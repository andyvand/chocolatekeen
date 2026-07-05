.include "hdr.asm"

; Expanded level data lives in WRAM bank $7F, declared as a linker RAM
; section so C code addresses it through a proper 24-bit symbol (the
; pvsneslib-sanctioned pattern, see /opt/snes-examples/maps/DynamicMap).
; Slot 3 spans the full 64 KB bank; largest expanded Keen level is
; 61 472 bytes (ep3 LEVEL15) plus the leading size word.

.base 0
.ramsection "ck_map_ram" bank $7f slot 3
    ck_map_data: dsb 61504
.ends
