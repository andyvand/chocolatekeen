.include "hdr.asm"


; Debug console font (PVSnesLib's BSD-licensed font, converted by gfx4snes)
.section ".rodata_font" superfree

snesfont:
.incbin "data/pvsneslibfont.pic"

snespal:
.incbin "data/pvsneslibfont.pal"

.ends

; SPC700 beep driver (assembled from spc/driver.asm; uploaded to SPC RAM
; at boot by src/engine/audio.c)
.section ".rodata_spcdrv" superfree

spcdriver:
.incbin "spc/driver.bin"
spcdriver_end:

.ends
