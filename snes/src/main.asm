.include "hdr.asm"
.accu 16
.index 16
.16bit
.BASE $C0
.define __main_locals 0
.SECTION ".maintext_0x0" SUPERFREE
main:
.ifgr __main_locals 0
tsa
sec
sbc #__main_locals
tas
.endif
jsr.l consoleInit
pea.w (8 * 256 + 1)
sep #$20
rep #$20
jsr.l setMode
pla
sep #$20
lda #1
pha
rep #$20
jsr.l bgSetDisable
tsa
clc
adc #1
tas
sep #$20
lda #2
pha
rep #$20
jsr.l bgSetDisable
tsa
clc
adc #1
tas
pea.w 0
sep #$20
lda #0
pha
rep #$20
jsr.l bgSetGfxPtr
tsa
clc
adc #3
tas
sep #$20
lda #1
pha
rep #$20
pea.w 16384
sep #$20
lda #0
pha
rep #$20
jsr.l bgSetMapPtr
tsa
clc
adc #4
tas
jsr.l ck_timer_init
jsr.l ck_msprite_init
jsr.l ck_input_init
jsr.l ck_game_state_init
pea.w 715
lda.l TILEINFO_Anim + 0
sta.b tcc__r0
lda.l TILEINFO_Anim + 0 + 2
pha
pei (tcc__r0)
jsr.l ck_render_anim_init
tsa
clc
adc #6
tas
jsr.l ck_text_init
jsr.l ck_audio_init
lda.b tcc__r0
ora.w #128
and.w #255
sep #$20
sta.w ck_dbg_audio + 0
rep #$20
jsr.l ck_save_init
jsr.l ck_gameflow_run
lda.w #0
sta.b tcc__r0
__local_0:
.ifgr __main_locals 0
tsa
clc
adc #__main_locals
tas
.endif
rtl
.ENDS
.BASE $00
.RAMSECTION "ram{WLA_FILENAME}.data" APPENDTO "globram.data"
.ENDS
.SECTION "{WLA_FILENAME}.data" APPENDTO "glob.data"
.ENDS
.SECTION ".rodata" SEMIFREE ORG $8000
.ENDS


.BASE $00
.RAMSECTION ".bss" BANK $7e SLOT 2
ck_dbg_audio dsb 1
.ENDS
