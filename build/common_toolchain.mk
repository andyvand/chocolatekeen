ifneq ($(filter $(BUILD_PROFILE),emscripten emscripten-legacy),)
	# Emscripten flags are set in common_profiles.mk by BUILD_PROFILE.
	INTCXXFLAGS+= -I$(SRC) $(EMCC_CFLAGS) $(EMCC_WARN_CFLAGS)
	INTLDFLAGS=$(EMCC_LDFLAGS)
else ifeq ($(BUILD_PROFILE),vita)
	INTCXXFLAGS+= -I$(SRC) $(VITA_CFLAGS) $(VITA_SDL_CFLAGS)
	INTLDFLAGS=$(VITA_SDL_LDFLAGS) $(VITA_LDFLAGS)
else ifeq ($(BUILD_PROFILE),sdl3)
	# SDL3 build: pkg-config supplies -I<prefix>/include, but the sources use
	# `#include "SDL.h"`, so also add the SDL3 include subdir so that resolves
	# to SDL3's umbrella header. The compat shim is force-included ahead of
	# every source (see build/sdl3/Makefile for the mechanism).
	INTCXXFLAGS+= -I$(SRC) $(SDL3_CFLAGS) -I$(SDL3_INCDIR) -include $(SRC)/platform/sdl3_compat.h
	INTLDFLAGS=$(SDL3_LDFLAGS)
else
	INTCXXFLAGS+= -I$(SRC) `$(SDLCONFIG) --cflags`
	INTLDFLAGS=`$(SDLCONFIG) --libs`
endif
