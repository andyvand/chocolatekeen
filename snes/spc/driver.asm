; Chocolate Keen SNES port - SPC700 PC-speaker beep driver (M7).
;
; Assembled with wla-spc700 + wlalink -b into a flat binary (spc/driver.bin)
; that the 65816 uploads into SPC RAM via the IPL-ROM handshake protocol
; (see snes/src/engine/audio.c). No music, no snesmod: the whole APU plays
; the original PC-speaker beep effects on DSP voice 0.
;
; ---- SPC RAM memory map (keep in sync with snes/src/engine/audio.c) ----
;   $0000-$000F  driver zero-page variables
;   $0100-$01EF  stack
;   $0200        DSP sample directory (page aligned; DIR register = $02)
;   $0300        driver code (entry point = $0300)
;   $0600        ck_brr_square32 (18 bytes, uploaded by the CPU)
;   $0620        ck_brr_square8   (9 bytes, uploaded by the CPU)
;   $0700        ck_sounds_bin    (uploaded by the CPU, ~8-11 KiB)
;
; driver.bin spans $0200..end-of-code; the BRR samples and sounds.bin are
; uploaded as separate IPL blocks so they must lie above the code.
;
; ---- Port protocol (CPU <-> SPC via $2140-$2143 / $F4-$F7) -------------
;   Port 0 (CPU->SPC): command byte. Bits 6..0 = Keen soundIndex+1
;       (0 = stop). Bit 7 = sequence flag toggled by the CPU on every
;       command so back-to-back retriggers of the same sound are seen as
;       a change. Priority gating is done CPU-side; the SPC just obeys.
;   Port 0 (SPC->CPU): ack: the driver echoes the full command byte once
;       the command has been processed. At boot the driver writes $7F
;       ("ready") - never a valid command echo (soundIndex+1 <= 126).
;   Port 1 (SPC->CPU): status: soundIndex+1 while a sound plays, 0 idle.
;   Port 2 (SPC->CPU): heartbeat: 6.875 ms tick counter low byte.
;
; ---- Tick stream format (ck_sounds_bin, see snes/src/data_format.h) ----
;   header: u16 count; count * { u16 tickStreamOfs, u8 priority, u8 pad }.
;   Streams: one u16 per 6.87 ms tick: 0x0000 = rest (key off), 0xFFFF =
;   end of sound, else bits13..0 = DSP V0PITCH, bit14 = use the fine
;   (8-samples-per-cycle) square sample instead of the coarse 32-sample one.
;   Matches engine_audio_gba.c semantics: each tick sets the frequency,
;   a rest silences; the tone is NOT retriggered tick-to-tick (the square
;   keeps looping, only the pitch register changes).

.MEMORYMAP
  DEFAULTSLOT 0
  SLOTSIZE $10000
  SLOT 0 $0000
.ENDME

.ROMBANKMAP
  BANKSTOTAL 1
  BANKSIZE $10000
  BANKS 1
.ENDRO

.EMPTYFILL $00

; Fixed upload addresses (keep in sync with audio.c and the comment above).
.DEFINE BRR32_ADDR  $0600
.DEFINE BRR8_ADDR   $0620
.DEFINE SND_BASE    $0700

; Zero-page driver variables.
.ENUM $0000
lastCmd  db   ; last command byte seen on port 0
active   db   ; 0 = idle, else current soundIndex+1
keyed    db   ; 0 = voice 0 keyed off, 1 = keyed on
cursrc   db   ; SRCN currently keyed ($FF = force rewrite)
hbeat    db   ; tick heartbeat mirrored to port 2
tickn    db   ; timer ticks left to process this loop pass
wL       db   ; current tick word, low byte
wH       db   ; current tick word, high byte
tmpL     db
tmpH     db
streamL  db   ; tick stream read pointer
streamH  db
.ENDE

.BANK 0 SLOT 0

; ---------------------------------------------------------------------------
; DSP sample directory at $0200 (DIR register = $02). Two entries of
; {u16 start, u16 loop}; both squares loop from their first block.
; ---------------------------------------------------------------------------
.ORGA $0200
.SECTION "SpcDir" FORCE
spc_dir:
    .dw BRR32_ADDR, BRR32_ADDR     ; sample 0: coarse square (32-sample loop)
    .dw BRR8_ADDR,  BRR8_ADDR      ; sample 1: fine square (2 cycles / 16)
.ENDS

; ---------------------------------------------------------------------------
; Driver code, entry at $0300.
; ---------------------------------------------------------------------------
.ORGA $0300
.SECTION "SpcDriver" FORCE
start:
    mov x, #$EF
    mov sp, x               ; stack at $01EF

    ; ---- DSP setup (write register number to $F2, value to $F3) ----
    mov $F2, #$6C           ; FLG: soft reset off, mute off, echo
    mov $F3, #$20           ;      writes disabled (ECEN=1), noise off
    mov $F2, #$5C           ; KOF: key off all voices while configuring
    mov $F3, #$FF
    mov $F2, #$0C           ; MVOL(L) = $7F
    mov $F3, #$7F
    mov $F2, #$1C           ; MVOL(R) = $7F
    mov $F3, #$7F
    mov $F2, #$2C           ; EVOL(L) = 0 (echo silent)
    mov $F3, #$00
    mov $F2, #$3C           ; EVOL(R) = 0
    mov $F3, #$00
    mov $F2, #$0D           ; EFB (echo feedback) = 0
    mov $F3, #$00
    mov $F2, #$2D           ; PMON: no pitch modulation
    mov $F3, #$00
    mov $F2, #$3D           ; NON: no noise
    mov $F3, #$00
    mov $F2, #$4D           ; EON: no echo on any voice
    mov $F3, #$00
    mov $F2, #$5D           ; DIR: sample directory at $0200
    mov $F3, #$02
    mov $F2, #$6D           ; ESA: park echo buffer high (writes disabled)
    mov $F3, #$FE
    mov $F2, #$7D           ; EDL: minimum echo delay
    mov $F3, #$00
    mov $F2, #$00           ; V0 VOL(L) = $60 (square peaks at ~44% already)
    mov $F3, #$60
    mov $F2, #$01           ; V0 VOL(R) = $60
    mov $F3, #$60
    mov $F2, #$02           ; V0 PITCH = 0 for now
    mov $F3, #$00
    mov $F2, #$03
    mov $F3, #$00
    mov $F2, #$04           ; V0 SRCN = 0 (coarse square)
    mov $F3, #$00
    mov $F2, #$05           ; V0 ADSR1 = 0: ADSR disabled -> GAIN mode
    mov $F3, #$00
    mov $F2, #$06           ; V0 ADSR2: don't care (ADSR off)
    mov $F3, #$00
    mov $F2, #$07           ; V0 GAIN: direct mode, full level
    mov $F3, #$7F
    mov $F2, #$4C           ; KON: none
    mov $F3, #$00
    mov $F2, #$5C           ; KOF: clear again (DSP has long since seen $FF)
    mov $F3, #$00

    ; ---- timer 0: 8 kHz base clock / 55 = 6.875 ms per tick ----
    mov $FA, #55            ; T0DIV
    mov $F1, #$31           ; CONTROL: start timer 0, clear input ports
                            ; 0-3 (also unmaps the IPL ROM; never needed again)

    ; ---- driver state + CPU-visible boot marker ----
    mov lastCmd, #0
    mov active, #0
    mov keyed, #0
    mov cursrc, #$FF
    mov hbeat, #0
    mov $F5, #$00           ; port 1: status = idle
    mov $F6, #$00           ; port 2: heartbeat = 0
    mov $F4, #$7F           ; port 0: "driver ready" marker for the CPU

; ---------------------------------------------------------------------------
; Main loop: poll port 0 for commands, process one stream word per
; elapsed timer-0 tick.
; ---------------------------------------------------------------------------
loop:
    mov a, $F4              ; command byte from the CPU
    cmp a, lastCmd
    beq chk_tick            ; unchanged -> no new command
    mov lastCmd, a
    and a, #$7F             ; strip the sequence-toggle bit
    beq cmd_stop

    ; ---- start sound: a = soundIndex+1 ----
    mov $F5, a              ; status: playing (set BEFORE the ack so the
    mov active, a           ; CPU sees valid status once acked)
    dec a                   ; a = soundIndex
    mov tmpH, #0
    asl a
    rol tmpH
    asl a
    rol tmpH                ; tmpH:a = index*4
    clrc
    adc a, #<(SND_BASE+2)   ; + header base (u16 count comes first)
    mov streamL, a
    mov a, tmpH
    adc a, #>(SND_BASE+2)
    mov streamH, a          ; stream -> header entry {u16 ofs, prio, pad}
    mov y, #0
    mov a, [streamL]+y      ; ofs low
    push a
    inc y
    mov a, [streamL]+y      ; ofs high
    mov tmpH, a
    pop a
    clrc
    adc a, #<SND_BASE       ; stream = SND_BASE + ofs
    mov streamL, a
    mov a, tmpH
    adc a, #>SND_BASE
    mov streamH, a
    call !keyoff             ; silence any running tone; first tick of the
    mov cursrc, #$FF        ; new stream keys back on (force SRCN rewrite)
    bra cmd_done

cmd_stop:
    call !keyoff
    mov active, #0
    mov $F5, #$00           ; status: idle

cmd_done:
    mov a, lastCmd
    mov $F4, a              ; ack: echo the full command byte

chk_tick:
    mov a, $FD              ; T0OUT: 4-bit tick count, clears on read
    beq loop
    mov tickn, a

tickloop:
    inc hbeat               ; heartbeat on port 2 (CPU-visible liveness)
    mov a, hbeat
    mov $F6, a
    mov a, active
    beq tick_next           ; idle: nothing to play

    ; ---- fetch the next tick word ----
    mov y, #0
    mov a, [streamL]+y
    mov wL, a
    inc y
    mov a, [streamL]+y
    mov wH, a
    clrc
    adc streamL, #2         ; stream += 2
    bcc no_carry
    inc streamH
no_carry:

    ; classify: $FFFF = end (pitch high byte is <= $7F, so wH=$FF is unique)
    mov a, wH
    cmp a, #$FF
    bne not_end
    call !keyoff             ; end of sound
    mov active, #0
    mov $F5, #$00           ; status: idle (CPU polls this for is_done)
    bra tick_next

not_end:
    or a, wL                ; wH|wL == 0 -> rest tick
    bne tone
    call !keyoff
    bra tick_next

tone:
    ; desired sample: bit14 of the word = bit6 of wH (0 coarse, 1 fine)
    mov a, wH
    and a, #$40
    beq src_sel
    mov a, #$01
src_sel:
    mov tmpL, a
    mov $F2, #$02           ; V0 PITCH(L) = wL
    mov a, wL
    mov $F3, a
    mov $F2, #$03           ; V0 PITCH(H) = wH & $3F
    mov a, wH
    and a, #$3F
    mov $F3, a
    mov a, keyed            ; (re)key only when off or the sample changed;
    beq do_keyon            ; otherwise the loop keeps running and only
    mov a, cursrc           ; the pitch register moved (GBA semantics)
    cmp a, tmpL
    beq tick_next
do_keyon:
    mov a, tmpL
    mov cursrc, a
    mov $F2, #$04           ; V0 SRCN = selected square sample
    mov $F3, a
    mov $F2, #$4C           ; KON voice 0 (retriggers if already playing,
    mov $F3, #$01           ; which only happens on a sample switch)
    mov keyed, #1

tick_next:
    dbnz tickn, tickloop
    jmp !loop

; ---------------------------------------------------------------------------
; keyoff: release voice 0 if keyed. KOF must stay set long enough for the
; DSP to latch it (it samples KON/KOF every 2 output samples = 64 us),
; then be cleared so a later KON is not masked.
; ---------------------------------------------------------------------------
keyoff:
    mov a, keyed
    beq keyoff_ret
    mov keyed, #0
    mov $F2, #$5C           ; KOF: key off voice 0 (release ramp)
    mov $F3, #$01
    call !dly
    mov $F2, #$5C           ; clear KOF
    mov $F3, #$00
keyoff_ret:
    ret

; ~250 us busy wait (SPC700 runs at 1.024 MHz). Clobbers Y.
dly:
    mov y, #64
dly_l:
    dbnz y, dly_l
    ret
.ENDS
