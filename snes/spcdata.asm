.include "hdr.asm"

; SPC700 beep driver blob (flat binary at SPC RAM $0200, entry $0300),
; built from spc/driver.asm by wla-spc700 + wlalink -b. Uploaded into SPC
; RAM at boot by ck_audio_init() (snes/src/engine/audio.c).
.SECTION "ck_spc_driver" SUPERFREE

ck_spc_driver_bin:
.incbin "spc/driver.bin" FSIZE ck_spc_driver_size

ck_spc_driver_bin_len:
.dw ck_spc_driver_size

.ENDS
