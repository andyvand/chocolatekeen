# SDL3 build target for Chocolate Keen.
#
# The engine is written against the SDL2 API. This target builds it against
# SDL3 by force-including src/platform/sdl3_compat.h ahead of every source
# (wired up in ../common_toolchain.mk for BUILD_PROFILE=sdl3) and linking the
# compat wrappers in src/platform/sdl3_compat.c. See that header for details.
#
# Requires SDL3 discoverable via pkg-config (the `sdl3` package). Override the
# pkg-config binary with SDL3_PKG_CONFIG=... for cross builds.

BUILD_PROFILE := sdl3

include ../common_base.mk
include ../common_profiles.mk

# SDL3 discovery. pkg-config --cflags gives -I<prefix>/include; the sources
# include "SDL.h", so ../common_toolchain.mk also adds SDL3_INCDIR on top.
SDL3_PKG_CONFIG ?= pkg-config
SDL3_CFLAGS := $(shell $(SDL3_PKG_CONFIG) --cflags sdl3)
SDL3_LDFLAGS := $(shell $(SDL3_PKG_CONFIG) --libs sdl3)
SDL3_INCDIR := $(shell $(SDL3_PKG_CONFIG) --variable=includedir sdl3)/SDL3

PLATFORM_OBJECTS=$(OBJ)/platform_sleep_native.o \
	$(OBJ)/platform_input_stub.o \
	$(OBJ)/platform_sdl3_compat.o
PLATFORM_SLEEP_OBJECT=$(OBJ)/platform_sleep_native.o
PLATFORM_SLEEP_SOURCE=$(SRC)/platform/sleep_native.c
PLATFORM_INPUT_OBJECT=$(OBJ)/platform_input_stub.o
PLATFORM_INPUT_SOURCE=$(SRC)/platform/input_stub.c

# On macOS the engine resolves bundled game data through getBundlePath()
# (src/platform/macbundle.m), so link that object and Foundation there.
PLATFORM_LDFLAGS=
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	PLATFORM_OBJECTS += $(OBJ)/macbundle.o
	PLATFORM_LDFLAGS += -framework Foundation
endif

include ../common_objects.mk

OBJECTS=$(COMMON_OBJECTS) $(EPISODE_OBJECTS) $(PLATFORM_OBJECTS)
include ../common_flags.mk

include ../common_toolchain.mk

.PHONY: all game clean veryclean test

all: game

game: $(EXE_PATH)

PRELOAD_DATA_DIR=data
PRELOAD_REQUIRE_EXISTING_DIR=0
include ../common_preload.mk

$(EXE_PATH): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) $(INTLDFLAGS) $(PLATFORM_LDFLAGS) -o $@ $(PRELOAD_FILES)

include ../common_rules.mk

# The compat implementation is compiled with the rename macros disabled so it
# can call the real SDL3 entry points (see the header/impl comments).
$(OBJ)/platform_sdl3_compat.o: $(SRC)/platform/sdl3_compat.c $(SRC)/platform/sdl3_compat.h
	$(CXX) -c $(INTCXXFLAGS) $(CXXFLAGS) -DCK_SDL3_COMPAT_IMPL $< -o $@

CLEAN_DIRS=$(OBJ) GAMEDATA
CLEAN_FILES=chocolate-keen
include ../common_clean.mk

# Run tests
test:
	$(MAKE) -C ../../tests run
