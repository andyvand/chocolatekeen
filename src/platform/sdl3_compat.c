// sdl3_compat.c: Implementation of the SDL3 compatibility wrappers declared in
// sdl3_compat.h. Compiled only in the `sdl3` build target.
//
// This file defines CK_SDL3_COMPAT_IMPL so the shim header does NOT install its
// SDL2->wrapper rename macros here: that leaves the real SDL3 entry points
// (SDL_CreateWindow, SDL_RenderTexture, SDL_OpenAudioDeviceStream, ...) callable
// by their real names, which is exactly what the wrappers need.

// The build compiles this file with -DCK_SDL3_COMPAT_IMPL so the shim header's
// rename macros stay inert here (see sdl3_compat.h). The guard below keeps the
// file self-consistent if that flag is ever missing.
#ifndef CK_SDL3_COMPAT_IMPL
#define CK_SDL3_COMPAT_IMPL 1
#endif

#include <string.h>

#include "platform/sdl3_compat.h"

#if SDL_VERSION_ATLEAST(3, 0, 0)

/* ------------------------------------------------------------------ */
/* Shared state                                                        */
/* ------------------------------------------------------------------ */

/* Last window created; used by the relative-mouse-mode wrapper, which in SDL3
 * is window-scoped rather than global. */
static SDL_Window *g_ck_window = NULL;

/* Scale mode requested via SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, ...),
 * applied to textures as they are created. SDL2 defaulted to nearest. */
static SDL_ScaleMode g_ck_scale_mode = SDL_SCALEMODE_NEAREST;

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

int CK_SDL_Init(Uint32 flags) {
    /* SDL3 returns true on success; the game checks the SDL2 "< 0 is error"
     * (and "!= 0 is error") conventions, so normalise to 0 / -1. */
    return SDL_Init((SDL_InitFlags)flags) ? 0 : -1;
}

int CK_SDL_InitSubSystem(Uint32 flags) {
    return SDL_InitSubSystem((SDL_InitFlags)flags) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Window / renderer                                                   */
/* ------------------------------------------------------------------ */

SDL_Window *CK_SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                                Uint64 flags) {
    /* SDL3 dropped the x/y position parameters from SDL_CreateWindow; the
     * window is positioned by the platform (matching SDL2's UNDEFINED case the
     * game passes). Honour an explicit position afterwards if one was given. */
    SDL_Window *window = SDL_CreateWindow(title ? title : "", w, h,
                                          (SDL_WindowFlags)flags);
    if (window) {
        if (x != (int)SDL_WINDOWPOS_UNDEFINED &&
            y != (int)SDL_WINDOWPOS_UNDEFINED &&
            !SDL_WINDOWPOS_ISUNDEFINED((Uint32)x) &&
            !SDL_WINDOWPOS_ISUNDEFINED((Uint32)y) &&
            !SDL_WINDOWPOS_ISCENTERED((Uint32)x) &&
            !SDL_WINDOWPOS_ISCENTERED((Uint32)y)) {
            SDL_SetWindowPosition(window, x, y);
        }
        g_ck_window = window;
    }
    return window;
}

SDL_Renderer *CK_SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags) {
    /* SDL2 selected a renderer by driver index (-1 = "let SDL choose") plus a
     * flags bitmask; SDL3 selects by driver name and configures vsync
     * separately. */
    const char *name = (index >= 0) ? SDL_GetRenderDriver(index) : NULL;
    SDL_Renderer *renderer = SDL_CreateRenderer(window, name);
    if (renderer && (flags & SDL_RENDERER_PRESENTVSYNC)) {
        SDL_SetRenderVSync(renderer, 1);
    }
    return renderer;
}

int CK_SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags) {
    /* SDL2 took a flags word (0 / FULLSCREEN / FULLSCREEN_DESKTOP); SDL3 takes a
     * bool and expresses exclusive vs desktop through the fullscreen mode
     * (left at the desktop default here). */
    return SDL_SetWindowFullscreen(window, flags != 0) ? 0 : -1;
}

int CK_SDL_GetRendererInfo(SDL_Renderer *renderer, CK_SDL_RendererInfo *info) {
    if (!info) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    if (!renderer) {
        return -1;
    }

    info->name = SDL_GetRendererName(renderer);

    const bool isSoftware = info->name && (SDL_strcmp(info->name, "software") == 0);
    info->flags = isSoftware ? SDL_RENDERER_SOFTWARE
                             : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

    SDL_PropertiesID props = SDL_GetRendererProperties(renderer);
    if (props) {
        const SDL_PixelFormat *formats =
            (const SDL_PixelFormat *)SDL_GetPointerProperty(
                props, SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER, NULL);
        Uint32 count = 0;
        if (formats) {
            while (formats[count] != SDL_PIXELFORMAT_UNKNOWN &&
                   count < CK_SDL_MAX_TEXTURE_FORMATS) {
                info->texture_formats[count] = formats[count];
                count++;
            }
        }
        info->num_texture_formats = count;
        info->max_texture_width = (int)SDL_GetNumberProperty(
            props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
        info->max_texture_height = info->max_texture_width;
    }
    return 0;
}

int CK_SDL_GetRenderDriverInfo(int index, CK_SDL_RendererInfo *info) {
    if (!info) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->name = SDL_GetRenderDriver(index);
    return info->name ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Textures / rendering (SDL_Rect -> SDL_FRect)                        */
/* ------------------------------------------------------------------ */

SDL_Texture *CK_SDL_CreateTexture(SDL_Renderer *renderer, SDL_PixelFormat format,
                                  int access, int w, int h) {
    SDL_Texture *texture =
        SDL_CreateTexture(renderer, format, (SDL_TextureAccess)access, w, h);
    if (texture) {
        /* Preserve the SDL2 behaviour where the scale quality hint set just
         * before texture creation governed its filtering. */
        SDL_SetTextureScaleMode(texture, g_ck_scale_mode);
    }
    return texture;
}

static void ck_rect_to_frect(const SDL_Rect *r, SDL_FRect *out) {
    out->x = (float)r->x;
    out->y = (float)r->y;
    out->w = (float)r->w;
    out->h = (float)r->h;
}

int CK_SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                      const SDL_Rect *srcrect, const SDL_Rect *dstrect) {
    SDL_FRect src, dst;
    SDL_FRect *srcp = NULL, *dstp = NULL;
    if (srcrect) {
        ck_rect_to_frect(srcrect, &src);
        srcp = &src;
    }
    if (dstrect) {
        ck_rect_to_frect(dstrect, &dst);
        dstp = &dst;
    }
    return SDL_RenderTexture(renderer, texture, srcp, dstp) ? 0 : -1;
}

int CK_SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect) {
    SDL_FRect r;
    SDL_FRect *rp = NULL;
    if (rect) {
        ck_rect_to_frect(rect, &r);
        rp = &r;
    }
    return SDL_RenderFillRect(renderer, rp) ? 0 : -1;
}

int CK_SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect) {
    SDL_FRect r;
    SDL_FRect *rp = NULL;
    if (rect) {
        ck_rect_to_frect(rect, &r);
        rp = &r;
    }
    return SDL_RenderRect(renderer, rp) ? 0 : -1;
}

int CK_SDL_SetHint(const char *name, const char *value) {
    if (name && SDL_strcmp(name, SDL_HINT_RENDER_SCALE_QUALITY) == 0) {
        /* SDL2 accepted "nearest"/"0", "linear"/"1", "best"/"2". */
        if (value && (value[0] == 'l' || value[0] == 'L' || value[0] == 'b' ||
                      value[0] == 'B' || value[0] == '1' || value[0] == '2')) {
            g_ck_scale_mode = SDL_SCALEMODE_LINEAR;
        } else {
            g_ck_scale_mode = SDL_SCALEMODE_NEAREST;
        }
        return 0;
    }
    return SDL_SetHint(name, value) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Surfaces                                                            */
/* ------------------------------------------------------------------ */

SDL_Surface *CK_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                     int depth, Uint32 Rmask, Uint32 Gmask,
                                     Uint32 Bmask, Uint32 Amask) {
    (void)flags;
    SDL_PixelFormat format =
        SDL_GetPixelFormatForMasks(depth, Rmask, Gmask, Bmask, Amask);
    return SDL_CreateSurface(width, height, format);
}

SDL_Surface *CK_SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                         int depth, int pitch, Uint32 Rmask,
                                         Uint32 Gmask, Uint32 Bmask,
                                         Uint32 Amask) {
    SDL_PixelFormat format =
        SDL_GetPixelFormatForMasks(depth, Rmask, Gmask, Bmask, Amask);
    return SDL_CreateSurfaceFrom(width, height, format, pixels, pitch);
}

int CK_SDL_SoftStretch(SDL_Surface *src, const SDL_Rect *srcrect,
                       SDL_Surface *dst, const SDL_Rect *dstrect) {
    return SDL_StretchSurface(src, srcrect, dst, dstrect, SDL_SCALEMODE_NEAREST)
               ? 0
               : -1;
}

/* ------------------------------------------------------------------ */
/* Mouse / cursor                                                      */
/* ------------------------------------------------------------------ */

int CK_SDL_ShowCursor(int toggle) {
    if (toggle == SDL_QUERY) {
        return SDL_CursorVisible() ? SDL_ENABLE : SDL_DISABLE;
    }
    if (toggle == SDL_DISABLE) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
    return toggle;
}

int CK_SDL_SetRelativeMouseMode(bool enabled) {
    if (!g_ck_window) {
        return -1;
    }
    return SDL_SetWindowRelativeMouseMode(g_ck_window, enabled) ? 0 : -1;
}

Uint32 CK_SDL_GetRelativeMouseState(int *x, int *y) {
    float fx = 0.0f, fy = 0.0f;
    Uint32 buttons = (Uint32)SDL_GetRelativeMouseState(&fx, &fy);
    if (x) {
        *x = (int)fx;
    }
    if (y) {
        *y = (int)fy;
    }
    return buttons;
}

/* ------------------------------------------------------------------ */
/* Joysticks                                                           */
/* ------------------------------------------------------------------ */

int CK_SDL_JoystickEventState(int state) {
    if (state == SDL_QUERY) {
        return SDL_JoystickEventsEnabled() ? SDL_ENABLE : SDL_DISABLE;
    }
    SDL_SetJoystickEventsEnabled(state == SDL_ENABLE);
    return state;
}

int CK_SDL_NumJoysticks(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    if (ids) {
        SDL_free(ids);
    }
    return count;
}

SDL_Joystick *CK_SDL_JoystickOpen(int device_index) {
    /* SDL2 opened by 0-based index; SDL3 opens by stable instance id. */
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    SDL_Joystick *joystick = NULL;
    if (ids) {
        if (device_index >= 0 && device_index < count) {
            joystick = SDL_OpenJoystick(ids[device_index]);
        }
        SDL_free(ids);
    }
    return joystick;
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

Uint32 CK_SDL_GetTicks(void) {
    /* SDL3 returns 64-bit milliseconds; the game does 32-bit wrap-around delta
     * arithmetic, so preserve the SDL2 32-bit truncation semantics. */
    return (Uint32)SDL_GetTicks();
}

/* ------------------------------------------------------------------ */
/* Audio (SDL2 push callback -> SDL3 audio stream)                     */
/* ------------------------------------------------------------------ */

static SDL_AudioStream *g_ck_audio_stream = NULL;
static CK_SDL_AudioCallback g_ck_audio_callback = NULL;
static void *g_ck_audio_userdata = NULL;
static CK_SDL_AudioStatus g_ck_audio_status = CK_SDL_AUDIO_STOPPED;

/* SDL3 asks us for `additional_amount` more bytes; feed the SDL2 callback in
 * chunks and push what it produces into the stream. */
static void SDLCALL ck_audio_stream_callback(void *userdata, SDL_AudioStream *stream,
                                             int additional_amount,
                                             int total_amount) {
    (void)userdata;
    (void)total_amount;
    if (additional_amount <= 0 || !g_ck_audio_callback) {
        return;
    }
    Uint8 buffer[4096];
    while (additional_amount > 0) {
        int chunk = additional_amount < (int)sizeof(buffer)
                        ? additional_amount
                        : (int)sizeof(buffer);
        g_ck_audio_callback(g_ck_audio_userdata, buffer, chunk);
        SDL_PutAudioStreamData(stream, buffer, chunk);
        additional_amount -= chunk;
    }
}

int CK_SDL_OpenAudio(CK_SDL_AudioSpec *desired, CK_SDL_AudioSpec *obtained) {
    if (!desired) {
        return -1;
    }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = desired->freq;
    spec.format = desired->format;
    spec.channels = desired->channels ? desired->channels : 1;

    g_ck_audio_callback = desired->callback;
    g_ck_audio_userdata = desired->userdata;

    g_ck_audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
        desired->callback ? ck_audio_stream_callback : NULL, NULL);
    if (!g_ck_audio_stream) {
        return -1;
    }

    /* Streams start paused; the game issues SDL_PauseAudio(0) to begin. */
    g_ck_audio_status = CK_SDL_AUDIO_PAUSED;

    if (obtained) {
        *obtained = *desired;
        obtained->channels = spec.channels;
        obtained->freq = spec.freq;
        obtained->format = spec.format;
    }
    return 0;
}

void CK_SDL_CloseAudio(void) {
    if (g_ck_audio_stream) {
        SDL_DestroyAudioStream(g_ck_audio_stream);
        g_ck_audio_stream = NULL;
    }
    g_ck_audio_callback = NULL;
    g_ck_audio_userdata = NULL;
    g_ck_audio_status = CK_SDL_AUDIO_STOPPED;
}

void CK_SDL_PauseAudio(int pause_on) {
    if (!g_ck_audio_stream) {
        return;
    }
    if (pause_on) {
        SDL_PauseAudioStreamDevice(g_ck_audio_stream);
        g_ck_audio_status = CK_SDL_AUDIO_PAUSED;
    } else {
        SDL_ResumeAudioStreamDevice(g_ck_audio_stream);
        g_ck_audio_status = CK_SDL_AUDIO_PLAYING;
    }
}

int CK_SDL_GetAudioStatus(void) {
    return (int)g_ck_audio_status;
}

void CK_SDL_LockAudio(void) {
    if (g_ck_audio_stream) {
        SDL_LockAudioStream(g_ck_audio_stream);
    }
}

void CK_SDL_UnlockAudio(void) {
    if (g_ck_audio_stream) {
        SDL_UnlockAudioStream(g_ck_audio_stream);
    }
}

/* ------------------------------------------------------------------ */
/* Display modes (SDL2 0-based display index -> SDL3 SDL_DisplayID)    */
/* ------------------------------------------------------------------ */

static SDL_DisplayID ck_display_id(int displayIndex) {
    int count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    SDL_DisplayID id = 0;
    if (ids) {
        if (displayIndex >= 0 && displayIndex < count) {
            id = ids[displayIndex];
        } else if (count > 0) {
            id = ids[0];
        }
        SDL_free(ids);
    }
    return id;
}

int CK_SDL_GetNumVideoDisplays(void) {
    int count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    if (ids) {
        SDL_free(ids);
    }
    return count;
}

int CK_SDL_GetNumDisplayModes(int displayIndex) {
    SDL_DisplayID id = ck_display_id(displayIndex);
    int count = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(id, &count);
    if (modes) {
        SDL_free(modes);
    }
    return count;
}

int CK_SDL_GetDisplayMode(int displayIndex, int modeIndex, SDL_DisplayMode *mode) {
    if (!mode) {
        return -1;
    }
    SDL_DisplayID id = ck_display_id(displayIndex);
    int count = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(id, &count);
    int result = -1;
    if (modes) {
        if (modeIndex >= 0 && modeIndex < count && modes[modeIndex]) {
            *mode = *modes[modeIndex];
            result = 0;
        }
        SDL_free(modes);
    }
    return result;
}

int CK_SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode) {
    if (!mode) {
        return -1;
    }
    SDL_DisplayID id = ck_display_id(displayIndex);
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(id);
    if (!desktop) {
        return -1;
    }
    *mode = *desktop;
    return 0;
}

int CK_SDL_GetWindowDisplayMode(SDL_Window *window, SDL_DisplayMode *mode) {
    if (!mode) {
        return -1;
    }
    const SDL_DisplayMode *current = SDL_GetWindowFullscreenMode(window);
    if (!current) {
        /* Not in an exclusive fullscreen mode: report the desktop mode of the
         * display the window is on, matching how the game uses this value. */
        SDL_DisplayID id = SDL_GetDisplayForWindow(window);
        current = SDL_GetDesktopDisplayMode(id);
    }
    if (!current) {
        return -1;
    }
    *mode = *current;
    return 0;
}

int CK_SDL_SetWindowDisplayMode(SDL_Window *window, const SDL_DisplayMode *mode) {
    return SDL_SetWindowFullscreenMode(window, mode) ? 0 : -1;
}

#endif /* SDL_VERSION_ATLEAST(3,0,0) */
