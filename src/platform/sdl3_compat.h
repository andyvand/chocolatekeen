// sdl3_compat.h: SDL3 compatibility shim for Chocolate Keen.
//
// Chocolate Keen targets the SDL2 API (with a legacy SDL 1.2 path still guarded
// by SDL_VERSION_ATLEAST). This header lets the same sources build and run
// against SDL3 without disturbing the SDL2/1.2 code paths. It is force-included
// (via -include) into every translation unit of the `sdl3` build target, ahead
// of each source's own `#include "SDL.h"`.
//
// SDL3 ships an official SDL2-name bridge, SDL_oldnames.h, gated by
// SDL_ENABLE_OLD_NAMES. We turn it on so every straightforward rename (event
// enums, init flags, byte-swap helpers, SDL_bool, most surface/joystick/GL
// function renames, ...) is handled by SDL itself. This shim then only has to
// cover what SDL_oldnames.h cannot:
//
//   * APIs SDL3 removed outright (the SDL2 push-callback audio model,
//     SDL_RendererInfo, SDL_GetRendererInfo, SDL_CreateRGBSurface, the
//     display-mode-by-index enumeration, SDL_JoystickEventState, ...).
//   * APIs that kept their name but changed signature/semantics
//     (SDL_CreateWindow dropped x/y, SDL_CreateRenderer takes a driver name,
//     the render calls take float rects, SDL_GetTicks returns 64-bit,
//     SDL_ShowCursor lost its toggle argument, ...).
//   * A few SDL2 names SDL_oldnames.h maps to deliberate "deprecated" compile
//     errors because the migration is not a clean 1:1 rename (SDL_NumJoysticks,
//     SDL_JoystickOpen by index, SDL_RenderCopy's rect types, the renderer
//     flag bits, SDL_HINT_RENDER_SCALE_QUALITY, ...). We #undef those and
//     supply a working definition.
//
// Wrapper renames must be inert while compiling sdl3_compat.c itself (defining
// CK_SDL3_COMPAT_IMPL) so the wrappers can call the real SDL3 entry points
// without recursing.
//
// Three constructs cannot be handled here and are guarded with
// `#if SDL_VERSION_ATLEAST(3,0,0)` directly in the sources instead:
// SDL_Surface::format struct-field access + SDL_MapRGB in render/gfx.c, the
// SDL_WINDOWEVENT switch dispatch (split into SDL_EVENT_WINDOW_* types), and
// the flattened SDL_KeyboardEvent (keysym) field access.

#ifndef CHOCOLATE_KEEN_SDL3_COMPAT_H
#define CHOCOLATE_KEEN_SDL3_COMPAT_H

// Opt into SDL3's SDL2-compatibility name aliases before pulling in the header.
#ifndef SDL_ENABLE_OLD_NAMES
#define SDL_ENABLE_OLD_NAMES 1
#endif

#include <SDL3/SDL.h>

/* SDL2's SDL.h transitively pulled in <stdlib.h>/<string.h> (via SDL_stdinc.h);
 * SDL3's headers are leaner, so restore those so the sources that relied on the
 * leak (exit(), malloc(), memcpy(), ...) still see their declarations. */
#include <stdlib.h>
#include <string.h>

#if SDL_VERSION_ATLEAST(3, 0, 0)

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants SDL_oldnames.h does not (usefully) provide                */
/* ------------------------------------------------------------------ */

/* The audio/timer subsystems no longer need explicit timer init. */
#ifndef SDL_INIT_TIMER
#define SDL_INIT_TIMER 0u
#endif

/* Cursor/joystick tri-state args used by our SDL_ShowCursor /
 * SDL_JoystickEventState wrappers (removed from SDL3). */
#ifndef SDL_ENABLE
#define SDL_ENABLE 1
#endif
#ifndef SDL_DISABLE
#define SDL_DISABLE 0
#endif
#ifndef SDL_QUERY
#define SDL_QUERY (-1)
#endif

/* Renderer creation flags: removed from SDL3. SOFTWARE/TARGETTEXTURE simply no
 * longer exist; ACCELERATED/PRESENTVSYNC are aliased by SDL_oldnames.h to
 * deliberate compile errors. Give all four working bit values so the game can
 * pass them to (and read them back from) our CreateRenderer / RendererInfo
 * compat layer. */
#undef SDL_RENDERER_SOFTWARE
#undef SDL_RENDERER_ACCELERATED
#undef SDL_RENDERER_PRESENTVSYNC
#undef SDL_RENDERER_TARGETTEXTURE
#define SDL_RENDERER_SOFTWARE 0x00000001u
#define SDL_RENDERER_ACCELERATED 0x00000002u
#define SDL_RENDERER_PRESENTVSYNC 0x00000004u
#define SDL_RENDERER_TARGETTEXTURE 0x00000008u

/* Fullscreen-desktop flag folded into plain fullscreen (+ a fullscreen mode).
 * SDL_oldnames.h maps it to a compile error, so redefine it. */
#undef SDL_WINDOW_FULLSCREEN_DESKTOP
#define SDL_WINDOW_FULLSCREEN_DESKTOP SDL_WINDOW_FULLSCREEN

/* SDL_HINT_RENDER_SCALE_QUALITY was removed (scale mode is now per-texture).
 * SDL_oldnames.h maps it to a compile error. Redefine it to a private sentinel
 * that the compat SDL_SetHint wrapper recognises: it records the requested
 * quality, which the compat SDL_CreateTexture wrapper applies to the new
 * texture via SDL_SetTextureScaleMode. */
#undef SDL_HINT_RENDER_SCALE_QUALITY
#define SDL_HINT_RENDER_SCALE_QUALITY "CHOCOLATE_KEEN_SCALE_QUALITY"

/* SDL3 renamed the "controller" events to "gamepad", including the SDL_Event
 * union members: event.cbutton -> event.gbutton, event.caxis -> event.gaxis.
 * The union member fields the game reads (which/button/axis/value) are
 * identical, and these tokens appear nowhere else in the tree, so aliasing the
 * member names is safe. (SDL3 headers are already parsed above.) */
#define cbutton gbutton
#define caxis gaxis

/* ------------------------------------------------------------------ */
/* Reshaped types                                                      */
/* ------------------------------------------------------------------ */

/* SDL3's SDL_AudioSpec is only {format, channels, freq} and the callback-driven
 * device model is gone. The game uses the SDL2 push-callback model, so expose
 * an SDL2-shaped spec that the compat SDL_OpenAudio wrapper translates into an
 * SDL_AudioStream. */
typedef void (*CK_SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct CK_SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    CK_SDL_AudioCallback callback;
    void *userdata;
} CK_SDL_AudioSpec;

/* SDL_GetAudioStatus and its enum were removed; the compat layer tracks it. */
typedef enum CK_SDL_AudioStatus {
    CK_SDL_AUDIO_STOPPED = 0,
    CK_SDL_AUDIO_PLAYING,
    CK_SDL_AUDIO_PAUSED
} CK_SDL_AudioStatus;
#undef SDL_AUDIO_STOPPED
#undef SDL_AUDIO_PLAYING
#undef SDL_AUDIO_PAUSED
#define SDL_AUDIO_STOPPED CK_SDL_AUDIO_STOPPED
#define SDL_AUDIO_PLAYING CK_SDL_AUDIO_PLAYING
#define SDL_AUDIO_PAUSED CK_SDL_AUDIO_PAUSED

/* SDL_RendererInfo was removed. The game reads name/flags/texture formats. */
#ifndef CK_SDL_MAX_TEXTURE_FORMATS
#define CK_SDL_MAX_TEXTURE_FORMATS 16
#endif

typedef struct CK_SDL_RendererInfo {
    const char *name;
    Uint32 flags;
    Uint32 num_texture_formats;
    SDL_PixelFormat texture_formats[CK_SDL_MAX_TEXTURE_FORMATS];
    int max_texture_width;
    int max_texture_height;
} CK_SDL_RendererInfo;

/* ------------------------------------------------------------------ */
/* Wrapper prototypes (implemented in sdl3_compat.c)                   */
/* ------------------------------------------------------------------ */

int CK_SDL_Init(Uint32 flags);
int CK_SDL_InitSubSystem(Uint32 flags);

SDL_Window *CK_SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                                Uint64 flags);
SDL_Renderer *CK_SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
int CK_SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);

int CK_SDL_GetRendererInfo(SDL_Renderer *renderer, CK_SDL_RendererInfo *info);
int CK_SDL_GetRenderDriverInfo(int index, CK_SDL_RendererInfo *info);

SDL_Texture *CK_SDL_CreateTexture(SDL_Renderer *renderer, SDL_PixelFormat format,
                                  int access, int w, int h);
int CK_SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                      const SDL_Rect *srcrect, const SDL_Rect *dstrect);
int CK_SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int CK_SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int CK_SDL_SetHint(const char *name, const char *value);

SDL_Surface *CK_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                     int depth, Uint32 Rmask, Uint32 Gmask,
                                     Uint32 Bmask, Uint32 Amask);
SDL_Surface *CK_SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                         int depth, int pitch, Uint32 Rmask,
                                         Uint32 Gmask, Uint32 Bmask,
                                         Uint32 Amask);
int CK_SDL_SoftStretch(SDL_Surface *src, const SDL_Rect *srcrect,
                       SDL_Surface *dst, const SDL_Rect *dstrect);

int CK_SDL_ShowCursor(int toggle);
int CK_SDL_SetRelativeMouseMode(bool enabled);
Uint32 CK_SDL_GetRelativeMouseState(int *x, int *y);

int CK_SDL_JoystickEventState(int state);
int CK_SDL_NumJoysticks(void);
SDL_Joystick *CK_SDL_JoystickOpen(int device_index);

Uint32 CK_SDL_GetTicks(void);

int CK_SDL_OpenAudio(CK_SDL_AudioSpec *desired, CK_SDL_AudioSpec *obtained);
void CK_SDL_CloseAudio(void);
void CK_SDL_PauseAudio(int pause_on);
int CK_SDL_GetAudioStatus(void);
void CK_SDL_LockAudio(void);
void CK_SDL_UnlockAudio(void);

int CK_SDL_GetNumVideoDisplays(void);
int CK_SDL_GetNumDisplayModes(int displayIndex);
int CK_SDL_GetDisplayMode(int displayIndex, int modeIndex, SDL_DisplayMode *mode);
int CK_SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode);
int CK_SDL_GetWindowDisplayMode(SDL_Window *window, SDL_DisplayMode *mode);
int CK_SDL_SetWindowDisplayMode(SDL_Window *window, const SDL_DisplayMode *mode);

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------ */
/* Rename macros: active in game sources, inert while building the shim */
/* ------------------------------------------------------------------ */
#ifndef CK_SDL3_COMPAT_IMPL

/* Reshaped types. */
#define SDL_AudioSpec CK_SDL_AudioSpec
#define SDL_AudioStatus CK_SDL_AudioStatus
#define SDL_RendererInfo CK_SDL_RendererInfo

/* Functions SDL3 kept by name but changed, or removed entirely. These are real
 * SDL3 functions (or gone), never macros, so a plain #define suffices. */
#define SDL_Init CK_SDL_Init
#define SDL_InitSubSystem CK_SDL_InitSubSystem
#define SDL_CreateWindow CK_SDL_CreateWindow
#define SDL_CreateRenderer CK_SDL_CreateRenderer
#define SDL_SetWindowFullscreen CK_SDL_SetWindowFullscreen
#define SDL_GetRendererInfo CK_SDL_GetRendererInfo
#define SDL_GetRenderDriverInfo CK_SDL_GetRenderDriverInfo
#define SDL_CreateTexture CK_SDL_CreateTexture
#define SDL_RenderFillRect CK_SDL_RenderFillRect
#define SDL_SetHint CK_SDL_SetHint
#define SDL_CreateRGBSurface CK_SDL_CreateRGBSurface
#define SDL_CreateRGBSurfaceFrom CK_SDL_CreateRGBSurfaceFrom
#define SDL_SoftStretch CK_SDL_SoftStretch
#define SDL_ShowCursor CK_SDL_ShowCursor
#define SDL_SetRelativeMouseMode CK_SDL_SetRelativeMouseMode
#define SDL_GetRelativeMouseState CK_SDL_GetRelativeMouseState
#define SDL_JoystickEventState CK_SDL_JoystickEventState
#define SDL_GetTicks CK_SDL_GetTicks
#define SDL_OpenAudio CK_SDL_OpenAudio
#define SDL_CloseAudio CK_SDL_CloseAudio
#define SDL_PauseAudio CK_SDL_PauseAudio
#define SDL_GetAudioStatus CK_SDL_GetAudioStatus
#define SDL_LockAudio CK_SDL_LockAudio
#define SDL_UnlockAudio CK_SDL_UnlockAudio
#define SDL_GetNumVideoDisplays CK_SDL_GetNumVideoDisplays
#define SDL_GetNumDisplayModes CK_SDL_GetNumDisplayModes
#define SDL_GetDisplayMode CK_SDL_GetDisplayMode
#define SDL_GetDesktopDisplayMode CK_SDL_GetDesktopDisplayMode

/* Names SDL_oldnames.h already aliased (to the wrong signature or to a
 * deliberate compile error): drop its alias, then install ours. */
#undef SDL_RenderCopy
#undef SDL_RenderDrawRect
#undef SDL_NumJoysticks
#undef SDL_JoystickOpen
#undef SDL_GetWindowDisplayMode
#undef SDL_SetWindowDisplayMode
#define SDL_RenderCopy CK_SDL_RenderCopy
#define SDL_RenderDrawRect CK_SDL_RenderDrawRect
#define SDL_NumJoysticks CK_SDL_NumJoysticks
#define SDL_JoystickOpen CK_SDL_JoystickOpen
#define SDL_GetWindowDisplayMode CK_SDL_GetWindowDisplayMode
#define SDL_SetWindowDisplayMode CK_SDL_SetWindowDisplayMode

#endif /* !CK_SDL3_COMPAT_IMPL */

#endif /* SDL_VERSION_ATLEAST(3,0,0) */

#endif /* CHOCOLATE_KEEN_SDL3_COMPAT_H */
