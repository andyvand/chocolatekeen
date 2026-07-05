; Chocolate Keen SNES port - cartridge header (HiROM + FastROM)
; Modeled on /opt/snes-examples/memory_mapping/hdr.asm

.MEMORYMAP                      ; HiROM (Mode 21) memory map
  SLOTSIZE $10000               ; 64 KB banks, linear across $C0-$FF
  DEFAULTSLOT 0
  SLOT 0 $0000
  SLOT 1 $0 $2000
  SLOT 2 $2000 $E000
  SLOT 3 $0 $10000
  SLOT 4 $6000                  ; Direct SRAM access window
.ENDME

.ROMBANKSIZE $10000             ; 64 KB per bank (HiROM)
.ROMBANKS 16                    ; 1 MB: banks 8+ hold ck_exe_image and
                                ; ck_tiles_chr (see generated data_ep<N>.asm);
                                ; keep in sync with ROM_BANKS in
                                ; scripts/snes_emit_data.c and ROMSIZE below

.SNESHEADER
  ID "SNES"

.ifdef CK_EPISODE2
  NAME "CHOCOLATE KEEN EP2   "
.else
.ifdef CK_EPISODE3
  NAME "CHOCOLATE KEEN EP3   "
.else
  NAME "CHOCOLATE KEEN EP1   "
.endif
.endif
  ;    "123456789012345678901"  ; max 21 bytes

  FASTROM
  HIROM

  CARTRIDGETYPE $02             ; ROM + SRAM
  ROMSIZE $0A                   ; 8 Mbits = 1 MB (keep in sync with .ROMBANKS)
  SRAMSIZE $03                  ; 64 kilobits = 8 KB battery SRAM
  COUNTRY $01                   ; U.S. (NTSC)
  LICENSEECODE $00
  VERSION $00
.ENDSNES

.SNESNATIVEVECTOR
  COP EmptyHandler
  BRK EmptyHandler
  ABORT EmptyHandler
  NMI VBlank
  IRQ EmptyHandler
.ENDNATIVEVECTOR

.SNESEMUVECTOR
  COP EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  RESET tcc__start
  IRQBRK EmptyHandler
.ENDEMUVECTOR

; FastROM + HiROM: code/data addressed from bank $C0
.BASE $C0
