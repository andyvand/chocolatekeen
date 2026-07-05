# Chocolate Keen SNES port - build one ROM per episode.
#
#   make EPISODE=1        -> chocolate-keen-snes-ep1.sfc
#
# Uses PVSnesLib/devkitsnes directly with explicit source lists instead of
# including snes_rules (whose wildcards and Linux-only sed don't fit here).
# The compile pipeline mirrors snes_rules: 816-tcc -H -F -> 816-opt ->
# constify -> wla-65816 -d -s -x -> wlalink against HiROM_FastROM libs.

PVSNESLIB_HOME ?= /opt
DEVKIT  := $(PVSNESLIB_HOME)/devkitsnes
LIBDIR  := $(PVSNESLIB_HOME)/pvsneslib/lib/HiROM_FastROM

CC      := $(DEVKIT)/bin/816-tcc
AS      := $(DEVKIT)/bin/wla-65816
AS700   := $(DEVKIT)/bin/wla-spc700
LD      := $(DEVKIT)/bin/wlalink
OPT     := $(DEVKIT)/tools/816-opt
CTF     := $(DEVKIT)/tools/constify
GFXCONV := $(DEVKIT)/tools/gfx4snes

EPISODE ?= 1
ifeq ($(filter $(EPISODE),1 2 3),)
$(error EPISODE must be 1, 2 or 3)
endif

ROMNAME := chocolate-keen-snes-ep$(EPISODE)

# Baked asset output of scripts/bake_snes_data.sh
GENDIR  := ../build/snes/generated-ep$(EPISODE)

CFLAGS := -I$(PVSNESLIB_HOME)/pvsneslib/include -I$(DEVKIT)/include -Isrc \
          -I$(GENDIR) \
          -DCHOCOLATE_KEEN_TARGET_SNES=1 \
          -DCHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE=$(EPISODE) \
          -DCHOCOLATE_KEEN_IS_EPISODE$(EPISODE)_ENABLED=1

ASFLAGS := -DCK_EPISODE$(EPISODE) -I$(CURDIR)

# --- Explicit source lists (grows with milestones) ---------------------------
CFILES  := src/main.c \
           src/engine/levelload.c \
           src/engine/render.c \
           src/engine/timer.c \
           src/engine/msprite.c \
           src/engine/input.c \
           src/engine/audio.c \
           src/engine/save.c \
           src/engine/text.c \
           src/game/game_state.c \
           src/game/physics.c \
           src/game/keen.c \
           src/game/episode1.c \
           src/game/episode2.c \
           src/game/episode3.c \
           src/game/gameplay.c \
           src/game/ui.c \
           src/game/worldmap.c \
           src/game/gameflow.c \
           $(GENDIR)/snes_data_gen.c


SFILES  := data.asm \
           ram7f.asm \
           $(GENDIR)/data_ep$(EPISODE).asm

OFILES  := $(CFILES:.c=.obj) $(SFILES:.asm=.obj)

GFXFILES := data/pvsneslibfont.pic

.DEFAULT_GOAL := all

# Object files depend on the episode selection, but there
# is no header/flag dependency tracking: force a clean rebuild whenever the
# variant combination changes (stale mixed-variant objects hang/misbehave).
VARIANT := E$(EPISODE)
ifneq ($(shell cat .variant 2>/dev/null),$(VARIANT))
$(shell rm -f $(OFILES) $(CFILES:.c=.ps) $(CFILES:.c=.asm) 2>/dev/null)
$(shell echo $(VARIANT) > .variant)
endif

bake:
	../scripts/bake_snes_data.sh ../data/GAMEDATA/KEEN$(EPISODE) $(GENDIR) $(EPISODE)

.PHONY: all clean run
all: $(ROMNAME).sfc

# --- C -> pseudo-asm -> optimized asm -> object -------------------------------
%.ps: %.c
	@echo "CC  $<"
	$(CC) $(CFLAGS) -Wall -c $< -H -F -o $@

%.asm: %.ps
	@echo "OPT $<"
	$(OPT) $< > $*.asp
	$(CTF) $*.c $*.asp $@
	@rm -f $*.asp

%.obj: %.asm
	@echo "AS  $<"
	$(AS) $(ASFLAGS) -d -s -x -o $@ $<

# --- Graphics ------------------------------------------------------------------
data/pvsneslibfont.pic: data/pvsneslibfont.png
	@echo "GFX $<"
	$(GFXCONV) -s 8 -o 16 -u 16 -p -e 0 -i $<

# --- SPC700 driver ---------------------------------------------------------------
spc/driver.obj: spc/driver.asm
	@echo "AS700 $<"
	$(AS700) -o $@ $<

spc/driver.bin: spc/driver.obj
	@echo "LDSPC $@"
	printf '[objects]\nspc/driver.obj\n' > spc/driver.link
	$(LD) -b spc/driver.link $@

data.obj: spc/driver.bin

data.obj: data.asm $(GFXFILES) hdr.asm

src/main.obj: src/main.asm

# --- Link ---------------------------------------------------------------------
$(ROMNAME).sfc: $(OFILES)
	@echo "[objects]" > linkfile
	@for i in $(OFILES); do echo $$i >> linkfile; done
	@for i in $$(ls $(LIBDIR)); do echo $(LIBDIR)/$$i >> linkfile; done
	@echo "LD  $@"
	@rm -f $(ROMNAME).sym
	$(LD) -d -s -v -A -c -L $(LIBDIR) linkfile $@
	@sed 's/://' $(ROMNAME).sym | sed '/ SECTIONSTART_/d;/ SECTIONEND_/d;/ RAM_USAGE_SLOT_/d' > $(ROMNAME).sym.tmp && mv $(ROMNAME).sym.tmp $(ROMNAME).sym

run: $(ROMNAME).sfc
	open -a bsnes $(ROMNAME).sfc

clean:
	rm -f $(OFILES) $(CFILES:.c=.ps) $(CFILES:.c=.asm) $(CFILES:.c=.asp) \
	      linkfile $(GFXFILES) data/pvsneslibfont.pal data/pvsneslibfont.map \
	      chocolate-keen-snes-ep*.sfc chocolate-keen-snes-ep*.sym
