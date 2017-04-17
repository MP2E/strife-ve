//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2014 Night Dive Studios, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM graphics stuff for SDL.
//


#include "SDL.h"
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "icon.c"

#include "config.h"
#include "deh_str.h"
#include "doomtype.h"
#include "doomkeys.h"

// [SVE] svillarreal - from gl scale branch
#include "i_glscale.h"

#include "i_input.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_swap.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

// [SVE] svillarreal
#include "doomstat.h"
#include "rb_main.h"
#include "rb_draw.h"
#include "rb_wipe.h"
#include "fe_frontend.h"
#include "m_help.h"

#include "i_social.h"


// SDL video driver name

char *video_driver = "";

// Window position:

static char *window_position = "center";

// SDL surface for the screen.

static SDL_Surface *screen;

// Window title

static char *window_title = "";

// Intermediate 8-bit buffer that we draw to instead of 'screen'.
// This is used when we are rendering in 32-bit screen mode.
// When in a real 8-bit screen mode, screenbuffer == screen.

static SDL_Surface *screenbuffer = NULL;

// palette

static SDL_Color palette[256];
static boolean palette_to_set;

// display has been set up?

static boolean initialized = false;

// disable mouse?

static boolean nomouse = false;
int usemouse = 1;

// Save screenshots in PNG format.

int png_screenshots = 0;

// if true, I_VideoBuffer is screen->pixels

static boolean native_surface;

// Screen width and height, from configuration file.

int screen_width = SCREENWIDTH;
int screen_height = SCREENHEIGHT;
int default_screen_width = SCREENWIDTH;
int default_screen_height = SCREENHEIGHT;

// [SVE] haleyjd
boolean screen_init;

// [SVE] svillarreal
static boolean show_visual_cursor = false;

// Color depth.

int screen_bpp = 0;

// Automatically adjust video settings if the selected mode is 
// not a valid video mode.

static int autoadjust_video_settings = 1;

// Run in full screen mode?  (int type for config code)

int fullscreen = true;
int default_fullscreen = true; // [SVE]

// Aspect ratio correction mode

int aspect_ratio_correct = true;

// Time to wait for the screen to settle on startup before starting the
// game (ms)

static int startup_delay = 1000;

// Grab the mouse? (int type for config code)

static int grabmouse = true;

// The screen buffer; this is modified to draw things to the screen

byte *I_VideoBuffer = NULL;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// [SVE] svillarreal - from gl scale branch
//
// If true, we are rendering the screen using OpenGL hardware scaling
// rather than software mode.

static boolean using_opengl = true;

// If true, we display dots at the bottom of the screen to 
// indicate FPS.

static boolean display_fps_dots;

// If this is true, the screen is rendered but not blitted to the
// video buffer.

static boolean noblit;

// Callback function to invoke to determine whether to grab the 
// mouse pointer.

static grabmouse_callback_t grabmouse_callback = NULL;
static warpmouse_callback_t warpmouse_callback = NULL; // haleyjd [SVE]

// disk image patch name (either STDISK or STCDROM) and
// background overwritten by the disk to be restored by EndRead

static boolean window_focused = true;

// Empty mouse cursor

static SDL_Cursor *cursors[2];

// Window resize state.

static boolean need_resize = false;
static unsigned int resize_w, resize_h;
static unsigned int last_resize_time;

// Gamma correction level to use

int usegamma = 0;

static boolean MouseShouldBeGrabbed()
{
    // never grab the mouse when in screensaver mode
   
    if (screensaver_mode)
        return false;

    // if the window doesn't have focus, never grab it

    if (!window_focused)
        return false;

    // always grab the mouse when full screen (dont want to 
    // see the mouse pointer)

    if (fullscreen && !show_visual_cursor) // [SVE]: allow visual cursor
        return true;

    // Don't grab the mouse if mouse input is disabled

    if (!usemouse || nomouse)
        return false;

    // if we specify not to grab the mouse, never grab

    if (!grabmouse)
        return false;

    // Invoke the grabmouse callback function to determine whether
    // the mouse should be grabbed

    if (grabmouse_callback != NULL)
    {
        return grabmouse_callback();
    }
    else
    {
        return true;
    }
}

//
// haleyjd 20141007: [SVE]
// Our improved mouse support in the menus for SVE requires not warping the
// mouse based on the menu being active, even though it's been released. This
// callback is used similar to the above to let the game code tell us when 
// we're ok to warp the mouse and when we should leave it alone.
//
static boolean MouseShouldBeWarped(void)
{
    return warpmouse_callback ? warpmouse_callback() : true;
}

void I_SetGrabMouseCallback(grabmouse_callback_t func)
{
    grabmouse_callback = func;
}

void I_SetWarpMouseCallback(warpmouse_callback_t func)
{
    warpmouse_callback = func;
}

// Set the variable controlling FPS dots.

void I_DisplayFPSDots(boolean dots_on)
{
    display_fps_dots = dots_on;
}

// Show or hide the mouse cursor. We have to use different techniques
// depending on the OS.

void I_SetShowCursor(boolean show)
{
    // On Windows, using SDL_ShowCursor() adds lag to the mouse input,
    // so work around this by setting an invisible cursor instead. On
    // other systems, it isn't possible to change the cursor, so this
    // hack has to be Windows-only. (Thanks to entryway for this)

#ifdef _WIN32
    if (show && !show_visual_cursor) // [SVE]
    {
        SDL_SetCursor(cursors[1]);
    }
    else
    {
        SDL_SetCursor(cursors[0]);
    }
#else
    SDL_ShowCursor(show && !show_visual_cursor);
#endif

    // When the cursor is hidden, grab the input.

    if (!screensaver_mode)
    {
        SDL_WM_GrabInput(!show);
    }
}

//
// I_SetShowVisualCursor
//

void I_SetShowVisualCursor(boolean show)
{
    show_visual_cursor = show;
#ifdef _WIN32
    SDL_SetCursor(cursors[0]);
#else
    SDL_ShowCursor(0);
#endif
}

//
// Translates the SDL key
//
// [SVE]: Externalized (needed in frontend)
//
int TranslateKey(SDL_keysym *sym)
{
    switch(sym->sym)
    {
      case SDLK_LEFT:	return KEY_LEFTARROW;
      case SDLK_RIGHT:	return KEY_RIGHTARROW;
      case SDLK_DOWN:	return KEY_DOWNARROW;
      case SDLK_UP:	return KEY_UPARROW;
      case SDLK_ESCAPE:	return KEY_ESCAPE;
      case SDLK_RETURN:	return KEY_ENTER;
      case SDLK_TAB:	return KEY_TAB;
      case SDLK_F1:	return KEY_F1;
      case SDLK_F2:	return KEY_F2;
      case SDLK_F3:	return KEY_F3;
      case SDLK_F4:	return KEY_F4;
      case SDLK_F5:	return KEY_F5;
      case SDLK_F6:	return KEY_F6;
      case SDLK_F7:	return KEY_F7;
      case SDLK_F8:	return KEY_F8;
      case SDLK_F9:	return KEY_F9;
      case SDLK_F10:	return KEY_F10;
      case SDLK_F11:	return KEY_F11;
      case SDLK_F12:	return KEY_F12;
      case SDLK_PRINT:  return KEY_PRTSCR;

      case SDLK_BACKSPACE: return KEY_BACKSPACE;
      case SDLK_DELETE:	return KEY_DEL;

      case SDLK_PAUSE:	return KEY_PAUSE;

#if !SDL_VERSION_ATLEAST(1, 3, 0)
      case SDLK_EQUALS: return KEY_EQUALS;
#endif

      case SDLK_MINUS:          return KEY_MINUS;

      case SDLK_LSHIFT:
      case SDLK_RSHIFT:
	return KEY_RSHIFT;
	
      case SDLK_LCTRL:
      case SDLK_RCTRL:
	return KEY_RCTRL;
	
      case SDLK_LALT:
      case SDLK_RALT:
#if !SDL_VERSION_ATLEAST(1, 3, 0)
      case SDLK_LMETA:
      case SDLK_RMETA:
#endif
        return KEY_RALT;

      case SDLK_CAPSLOCK: return KEY_CAPSLOCK;
      case SDLK_SCROLLOCK: return KEY_SCRLCK;
      case SDLK_NUMLOCK: return KEY_NUMLOCK;

      case SDLK_KP0: return KEYP_0;
      case SDLK_KP1: return KEYP_1;
      case SDLK_KP2: return KEYP_2;
      case SDLK_KP3: return KEYP_3;
      case SDLK_KP4: return KEYP_4;
      case SDLK_KP5: return KEYP_5;
      case SDLK_KP6: return KEYP_6;
      case SDLK_KP7: return KEYP_7;
      case SDLK_KP8: return KEYP_8;
      case SDLK_KP9: return KEYP_9;

      case SDLK_KP_PERIOD:   return KEYP_PERIOD;
      case SDLK_KP_MULTIPLY: return KEYP_MULTIPLY;
      case SDLK_KP_PLUS:     return KEYP_PLUS;
      case SDLK_KP_MINUS:    return KEYP_MINUS;
      case SDLK_KP_DIVIDE:   return KEYP_DIVIDE;
      case SDLK_KP_EQUALS:   return KEYP_EQUALS;
      case SDLK_KP_ENTER:    return KEYP_ENTER;

      case SDLK_HOME: return KEY_HOME;
      case SDLK_INSERT: return KEY_INS;
      case SDLK_END: return KEY_END;
      case SDLK_PAGEUP: return KEY_PGUP;
      case SDLK_PAGEDOWN: return KEY_PGDN;

#ifdef SDL_HAVE_APP_KEYS
        case SDLK_APP1:        return KEY_F1;
        case SDLK_APP2:        return KEY_F2;
        case SDLK_APP3:        return KEY_F3;
        case SDLK_APP4:        return KEY_F4;
        case SDLK_APP5:        return KEY_F5;
        case SDLK_APP6:        return KEY_F6;
#endif

      default:
        return tolower(sym->sym);
    }
}

void I_ShutdownGraphics(void)
{
    if (initialized)
    {
        I_SetShowCursor(true);

        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        initialized = false;
    }
}



//
// I_StartFrame
//
void I_StartFrame (void)
{
    // [SVE] svillarreal
    gAppServices->Update();
}

//
// haleyjd 20141004: [SVE] Get true mouse position
//
void I_GetAbsoluteMousePosition(int *x, int *y)
{
    SDL_Surface *display = SDL_GetVideoSurface();
    int w = display->w;
    int h = display->h;
    fixed_t aspectRatio = w * FRACUNIT / h;

    if(!display)
        return;

    SDL_PumpEvents();
    SDL_GetMouseState(x, y);

    if(aspectRatio == 4 * FRACUNIT / 3) // nominal
    {
        *x = *x * SCREENWIDTH  / w;
        *y = *y * SCREENHEIGHT / h;
    }
    else if(aspectRatio > 4 * FRACUNIT / 3) // widescreen
    {
        // calculate centered 4:3 subrect
        int sw = h * 4 / 3;
        int hw = (w - sw) / 2;

        *x = (*x - hw) * SCREENWIDTH / sw;
        *y = *y * SCREENHEIGHT / h;
    }
    else // narrow
    {
        int sh = w * 3 / 4;
        int hh = (h - sh) / 2;

        *x = *x * SCREENWIDTH / w;
        *y = (*y - hh) * SCREENHEIGHT / sh;
    }
}


void I_GetEvent(void)
{
    extern void I_HandleKeyboardEvent(SDL_Event *sdlevent);
    extern void I_HandleMouseEvent(SDL_Event *sdlevent);
    SDL_Event sdlevent;

    SDL_PumpEvents();

    while (SDL_PollEvent(&sdlevent))
    {
        switch (sdlevent.type)
        {
            case SDL_KEYDOWN:
                if (ToggleFullScreenKeyShortcut(&sdlevent.key.keysym))
                {
                    I_ToggleFullScreen();
                    break;
                }
                // deliberate fall-though

            case SDL_KEYUP:
                I_HandleKeyboardEvent(&sdlevent);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEWHEEL:
                if (usemouse && !nomouse && window_focused)
                {
                    I_HandleMouseEvent(&sdlevent);
                }
                break;

            case SDL_QUIT:
                if (screensaver_mode)
                {
                    I_Quit();
                }
                else
                {
                    event_t event;
                    event.type = ev_quit;
                    D_PostEvent(&event);
                }
                break;

            case SDL_WINDOWEVENT:
                if (sdlevent.window.windowID == SDL_GetWindowID(screen))
                {
                    HandleWindowEvent(&sdlevent.window);
                }
                break;

            default:
                break;
        }
    }
}

//
// I_StartTic
//
void I_StartTic (void)
{
    if (!initialized)
    {
        return;
    }

    // [SVE]:
    // if doing in-game options, call the frontend responder for all input
    if(FE_InOptionsMenu())
    {
        if(FE_InGameOptionsResponder())
            FE_EndInGameOptionsMenu();
    }
    else
    {
        I_GetEvent();

        if (usemouse && !nomouse)
        {
            I_ReadMouse();
        }

        I_UpdateJoystick();
    }
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

static void UpdateGrab(void)
{
    static boolean currently_grabbed = false;
    boolean grab;

    grab = MouseShouldBeGrabbed();

    if (screensaver_mode)
    {
        // Hide the cursor in screensaver mode

        I_SetShowCursor(false);
    }
    else if (grab && !currently_grabbed)
    {
        I_SetShowCursor(false);
    }
    else if (!grab && currently_grabbed)
    {
        I_SetShowCursor(true);

        // When releasing the mouse from grab, warp the mouse cursor to
        // the bottom-right of the screen. This is a minimally distracting
        // place for it to appear - we may only have released the grab
        // because we're at an end of level intermission screen, for
        // example.

        if(MouseShouldBeWarped()) // [SVE]: done conditionally.
            SDL_WarpMouse(screen->w - 16, screen->h - 16);
        SDL_GetRelativeMouseState(NULL, NULL);
    }

    currently_grabbed = grab;

}

// [SVE] svillarreal - from gl scale branch
// Ending of I_FinishUpdate() when in software scaling mode.

static void FinishUpdateSoftware(void)
{
    // draw to screen

    BlitArea(0, 0, SCREENWIDTH, SCREENHEIGHT);

    if (palette_to_set)
    {
        SDL_SetColors(screenbuffer, palette, 0, 256);
        palette_to_set = false;

        // In native 8-bit mode, if we have a palette to set, the act
        // of setting the palette updates the screen

        if (screenbuffer == screen)
        {
            return;
        }
    }

    // In 8in32 mode, we must blit from the fake 8-bit screen buffer
    // to the real screen before doing a screen flip.

    if (screenbuffer != screen)
    {
        SDL_Rect dst_rect;

        // Center the buffer within the full screen space.

        dst_rect.x = (screen->w - screenbuffer->w) / 2;
        dst_rect.y = (screen->h - screenbuffer->h) / 2;

        SDL_BlitSurface(screenbuffer, NULL, screen, &dst_rect);
    }

    SDL_Flip(screen);
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{
    static int	lasttic;
    int		tics;
    int		i;

    if (!initialized)
        return;

    if (noblit)
        return;

    if (need_resize && SDL_GetTicks() > last_resize_time + 500)
    {
        ApplyWindowResize(resize_w, resize_h);
        need_resize = false;
        palette_to_set = true;
    }

    UpdateGrab();

    // Don't update the screen if the window isn't visible.
    // Not doing this breaks under Windows when we alt-tab away 
    // while fullscreen.

    if (!(SDL_GetAppState() & SDL_APPACTIVE))
        return;

    // draws little dots on the bottom of the screen

    if(display_fps_dots)
    {
	    i = I_GetTime();
	    tics = i - lasttic;
	    lasttic = i;
	    if (tics > 20) tics = 20;

	    for (i=0 ; i<tics*4 ; i+=4)
	        I_VideoBuffer[ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
	    for ( ; i<20*4 ; i+=4)
	        I_VideoBuffer[ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;
    }

    // show a disk icon if lumps have been read in the previous tic

    if (disk_indicator == disk_on)
    {
        V_BeginRead();
    }
    else if (disk_indicator == disk_dirty)
    {
        disk_indicator = disk_off;
    }

    // draw to screen
    // [SVE] svillarreal - from gl scale branch
    if (using_opengl)
    {
        int mouse_x, mouse_y;

        if(!use3drenderer)
        {
            I_GL_UpdateScreen(I_VideoBuffer, palette);
        }
        else
        {
            M_HelpDrawerGL();
            RB_DrawPatchBuffer();
        }

        if(show_visual_cursor && !gAppServices->OverlayActive())
        {
            if(i_seemouses || !i_seejoysticks) // haleyjd 20141202: this is overtime work.
            {
                SDL_GetMouseState(&mouse_x, &mouse_y);
                RB_DrawMouseCursor(mouse_x, mouse_y);
            }
        }

        RB_SwapBuffers();
    }
    else
    {
        FinishUpdateSoftware();
    }
}


//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH*SCREENHEIGHT);
}


//
// I_SetPalette
//
void I_SetPalette (byte *doompalette)
{
    int i;

    for (i=0; i<256; ++i)
    {
        // Zero out the bottom two bits of each channel - the PC VGA
        // controller only supports 6 bits of accuracy.

        palette[i].r = gammatable[usegamma][*doompalette++] & ~3;
        palette[i].g = gammatable[usegamma][*doompalette++] & ~3;
        palette[i].b = gammatable[usegamma][*doompalette++] & ~3;
    }

    palette_to_set = true;
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex(int r, int g, int b)
{
    int best, best_diff, diff;
    int i;

    best = 0; best_diff = INT_MAX;

    for (i = 0; i < 256; ++i)
    {
        diff = (r - palette[i].r) * (r - palette[i].r)
             + (g - palette[i].g) * (g - palette[i].g)
             + (b - palette[i].b) * (b - palette[i].b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

//
// I_GetPaletteColor
//

void I_GetPaletteColor(byte *rgb, int index)
{
    rgb[0] = palette[index].r;
    rgb[1] = palette[index].g;
    rgb[2] = palette[index].b;
}

// 
// Set the window title
//

void I_SetWindowTitle(char *title)
{
    window_title = title;
}

//
// Call the SDL function to set the window title, based on 
// the title set with I_SetWindowTitle.
//

void I_InitWindowTitle(void)
{
    // haleyjd 20140827: [SVE] hard coded title
    char *buf = "Strife: Veteran Edition";

    //buf = M_StringJoin(window_title, " - ", PACKAGE_STRING, NULL);
    SDL_WM_SetCaption(buf, NULL);
    //free(buf);
}

// Set the application icon

void I_InitWindowIcon(void)
{
    SDL_Surface *surface;
    Uint8 *mask;
    int i;

    // Generate the mask

    mask = malloc(icon_w * icon_h / 8);
    memset(mask, 0, icon_w * icon_h / 8);

    for (i=0; i<icon_w * icon_h; ++i)
    {
        if (icon_data[i * 3] != 0x00
         || icon_data[i * 3 + 1] != 0x00
         || icon_data[i * 3 + 2] != 0x00)
        {
            mask[i / 8] |= 1 << (7 - i % 8);
        }
    }

    surface = SDL_CreateRGBSurfaceFrom(icon_data,
                                       icon_w,
                                       icon_h,
                                       24,
                                       icon_w * 3,
                                       0xff << 0,
                                       0xff << 8,
                                       0xff << 16,
                                       0);

    SDL_WM_SetIcon(surface, mask);
    SDL_FreeSurface(surface);
    free(mask);
}

// Pick the modes list to use:

static void GetScreenModes(screen_mode_t ***modes_list, int *num_modes)
{
    if (aspect_ratio_correct)
    {
        *modes_list = screen_modes_corrected;
        *num_modes = arrlen(screen_modes_corrected);
    }
    else
    {
        *modes_list = screen_modes;
        *num_modes = arrlen(screen_modes);
    }
}

// Find which screen_mode_t to use for the given width and height.

static screen_mode_t *I_FindScreenMode(int w, int h)
{
    screen_mode_t **modes_list;
    screen_mode_t *best_mode;
    int modes_list_length;
    int num_pixels;
    int best_num_pixels;
    int i;

    // [SVE] svillarreal - from gl scale branch
    // In OpenGL mode the rules are different. We can have any
    // resolution, though it needs to match the aspect ratio we
    // expect.

    if (using_opengl)
    {
        static screen_mode_t gl_mode;
        int screenheight;
        float screenwidth;

        if (aspect_ratio_correct)
        {
            screenheight = SCREENHEIGHT_4_3;
        }
        else
        {
            screenheight = SCREENHEIGHT;
        }
        
        // [SVE] svillarreal - if using 3d renderer then account for widescreen resolutions
        screenwidth = use3drenderer ? (float)screenheight * (float)w / (float)h : SCREENWIDTH;
        screenwidth = ((float)h * screenwidth / (float)screenheight);

        gl_mode.width = (int)screenwidth;
        gl_mode.height = h;
        gl_mode.InitMode = NULL;
        gl_mode.DrawScreen = NULL;
        gl_mode.poor_quality = false;

        return &gl_mode;
    }

    // Special case: 320x200 and 640x400 are available even if aspect 
    // ratio correction is turned on.  These modes have non-square
    // pixels.

    if (fullscreen)
    {
        if (w == SCREENWIDTH && h == SCREENHEIGHT)
        {
            return &mode_scale_1x;
        }
        else if (w == SCREENWIDTH*2 && h == SCREENHEIGHT*2)
        {
            return &mode_scale_2x;
        }
    }

    GetScreenModes(&modes_list, &modes_list_length);

    // Find the biggest screen_mode_t in the list that fits within these 
    // dimensions

    best_mode = NULL;
    best_num_pixels = 0;

    for (i=0; i<modes_list_length; ++i) 
    {
        // Will this fit within the dimensions? If not, ignore.

        if (modes_list[i]->width > w || modes_list[i]->height > h)
        {
            continue;
        }

        num_pixels = modes_list[i]->width * modes_list[i]->height;

        if (num_pixels > best_num_pixels)
        {
            // This is a better mode than the current one

            best_mode = modes_list[i];
            best_num_pixels = num_pixels;
        }
    }

    return best_mode;
}

// Adjust to an appropriate fullscreen mode.
// Returns true if successful.

static boolean AutoAdjustFullscreen(void)
{
    SDL_Rect **modes;
    SDL_Rect *best_mode;
    screen_mode_t *screen_mode;
    int diff, best_diff;
    int i;

    modes = SDL_ListModes(NULL, SDL_FULLSCREEN);

    // No fullscreen modes available at all?

    if (modes == NULL || modes == (SDL_Rect **) -1 || *modes == NULL)
    {
        return false;
    }
    
    // [SVE]: On first time run, set desired res to best available res
    if(!screen_init && use3drenderer)
    {
        screen_width  = default_screen_width  = modes[0]->w;
        screen_height = default_screen_height = modes[0]->h;
        screen_init   = true;        // do not do this again
    }

    // Find the best mode that matches the mode specified in the
    // configuration file

    best_mode = NULL;
    best_diff = INT_MAX;

    for (i=0; modes[i] != NULL; ++i)
    {
        //printf("%ix%i?\n", modes[i]->w, modes[i]->h);

        // What screen_mode_t would be used for this video mode?

        screen_mode = I_FindScreenMode(modes[i]->w, modes[i]->h);

        // Never choose a screen mode that we cannot run in, or
        // is poor quality for fullscreen

        if (screen_mode == NULL || screen_mode->poor_quality)
        {
        //    printf("\tUnsupported / poor quality\n");
            continue;
        }

        // Do we have the exact mode?
        // If so, no autoadjust needed

        if (screen_width == modes[i]->w && screen_height == modes[i]->h)
        {
        //    printf("\tExact mode!\n");
            return true;
        }

        // Is this mode better than the current mode?

        diff = (screen_width - modes[i]->w) * (screen_width - modes[i]->w)
             + (screen_height - modes[i]->h) * (screen_height - modes[i]->h);

        if (diff < best_diff)
        {
        //    printf("\tA valid mode\n");
            best_mode = modes[i];
            best_diff = diff;
        }
    }

    if (best_mode == NULL)
    {
        // Unable to find a valid mode!

        return false;
    }

    printf("I_InitGraphics: %ix%i mode not supported on this machine.\n",
           screen_width, screen_height);

    screen_width  = default_screen_width  = best_mode->w;
    screen_height = default_screen_height = best_mode->h;

    return true;
}

// Auto-adjust to a valid windowed mode.

static void AutoAdjustWindowed(void)
{
    screen_mode_t *best_mode;

    // Find a screen_mode_t to fit within the current settings

    best_mode = I_FindScreenMode(screen_width, screen_height);

    if (best_mode == NULL)
    {
        // Nothing fits within the current settings.
        // Pick the closest to 320x200 possible.

        best_mode = I_FindScreenMode(SCREENWIDTH, SCREENHEIGHT_4_3);
    }

    // Switch to the best mode if necessary.

    if (best_mode->width != screen_width || best_mode->height != screen_height)
    {
        printf("I_InitGraphics: Cannot run at specified mode: %ix%i\n",
               screen_width, screen_height);

        screen_width  = default_screen_width  = best_mode->width;
        screen_height = default_screen_height = best_mode->height;
    }
}

// Auto-adjust to a valid color depth.

static void AutoAdjustColorDepth(void)
{
    SDL_Rect **modes;
    SDL_PixelFormat format;
    const SDL_VideoInfo *info;
    int flags;

    // If screen_bpp=0, we should use the current (default) pixel depth.
    // Fetch it from SDL.

    if (screen_bpp == 0)
    {
        info = SDL_GetVideoInfo();

        if (info != NULL && info->vfmt != NULL)
        {
            screen_bpp = info->vfmt->BitsPerPixel;
        }
    }

    if (fullscreen)
    {
        flags = SDL_FULLSCREEN;
    }
    else
    {
        flags = 0;
    }

    format.BitsPerPixel = screen_bpp;
    format.BytesPerPixel = (screen_bpp + 7) / 8;

    // Are any screen modes supported at the configured color depth?

    modes = SDL_ListModes(&format, flags);

    // If not, we must autoadjust to something sensible.

    if (modes == NULL)
    {
        printf("I_InitGraphics: %ibpp color depth not supported.\n",
               screen_bpp);

        info = SDL_GetVideoInfo();

        if (info != NULL && info->vfmt != NULL)
        {
            screen_bpp = info->vfmt->BitsPerPixel;
        }
    }
}

// If the video mode set in the configuration file is not available,
// try to choose a different mode.

static void I_AutoAdjustSettings(void)
{
    int old_screen_w, old_screen_h, old_screen_bpp;

    old_screen_w = screen_width;
    old_screen_h = screen_height;
    old_screen_bpp = screen_bpp;

    // Possibly adjust color depth.

    AutoAdjustColorDepth();

    // If we are running fullscreen, try to autoadjust to a valid fullscreen
    // mode.  If this is impossible, switch to windowed.

    if (fullscreen && !AutoAdjustFullscreen())
    {
        fullscreen = default_fullscreen = 0;
    }

    // If we are running windowed, pick a valid window size.

    if (!fullscreen)
    {
        AutoAdjustWindowed();
    }

    // Have the settings changed?  Show a message.

    if (screen_width != old_screen_w || screen_height != old_screen_h
     || screen_bpp != old_screen_bpp)
    {
        printf("I_InitGraphics: Auto-adjusted to %ix%ix%ibpp.\n",
               screen_width, screen_height, screen_bpp);

        printf("NOTE: Your video settings have been adjusted.  "
               "To disable this behavior,\n"
               "set autoadjust_video_settings to 0 in your "
               "configuration file.\n");
    }
}

// Set video size to a particular scale factor (1x, 2x, 3x, etc.)

static void SetScaleFactor(int factor)
{
    int w, h;

    // Pick 320x200 or 320x240, depending on aspect ratio correct

    if (aspect_ratio_correct)
    {
        w = SCREENWIDTH;
        h = SCREENHEIGHT_4_3;
    }
    else
    {
        w = SCREENWIDTH;
        h = SCREENHEIGHT;
    }

    screen_width  = default_screen_width  = w * factor;
    screen_height = default_screen_height = h * factor;
}

void I_GraphicsCheckCommandLine(void)
{
    int i;

    //!
    // @vanilla
    //
    // Disable blitting the screen.
    //

    noblit = M_CheckParm ("-noblit"); 

    //!
    // @category video 
    //
    // Grab the mouse when running in windowed mode.
    //

    if (M_CheckParm("-grabmouse"))
    {
        grabmouse = true;
    }

    //!
    // @category video 
    //
    // Don't grab the mouse when running in windowed mode.
    //

    if (M_CheckParm("-nograbmouse"))
    {
        grabmouse = false;
    }

    // default to fullscreen mode, allow override with command line
    // nofullscreen because we love prboom

    //!
    // @category video 
    //
    // Run in a window.
    //

    if (M_CheckParm("-window") || M_CheckParm("-nofullscreen"))
    {
        fullscreen = false;
    }

    //!
    // @category video 
    //
    // Run in fullscreen mode.
    //

    if (M_CheckParm("-fullscreen"))
    {
        fullscreen = true;
    }

    //!
    // @category video 
    //
    // Disable the mouse.
    //

    nomouse = M_CheckParm("-nomouse") > 0;

    //!
    // @category video
    // @arg <x>
    //
    // Specify the screen width, in pixels.
    //

    i = M_CheckParmWithArgs("-width", 1);

    if (i > 0)
    {
        screen_width = atoi(myargv[i + 1]);
    }

    //!
    // @category video
    // @arg <y>
    //
    // Specify the screen height, in pixels.
    //

    i = M_CheckParmWithArgs("-height", 1);

    if (i > 0)
    {
        screen_height = atoi(myargv[i + 1]);
    }

    //!
    // @category video
    // @arg <bpp>
    //
    // Specify the color depth of the screen, in bits per pixel.
    //

    i = M_CheckParmWithArgs("-bpp", 1);

    if (i > 0)
    {
        screen_bpp = atoi(myargv[i + 1]);
    }

    // Because we love Eternity:

    //!
    // @category video
    //
    // Set the color depth of the screen to 32 bits per pixel.
    //

    if (M_CheckParm("-8in32"))
    {
        screen_bpp = 32;
    }

    //!
    // @category video
    // @arg <WxY>
    //
    // Specify the screen mode (when running fullscreen) or the window
    // dimensions (when running in windowed mode).

    i = M_CheckParmWithArgs("-geometry", 1);

    if (i > 0)
    {
        int w, h;

        if (sscanf(myargv[i + 1], "%ix%i", &w, &h) == 2)
        {
            screen_width = w;
            screen_height = h;
        }
    }

    //!
    // @category video
    //
    // Don't scale up the screen.
    //

    if (M_CheckParm("-1")) 
    {
        SetScaleFactor(1);
    }

    //!
    // @category video
    //
    // Double up the screen to 2x its normal size.
    //

    if (M_CheckParm("-2")) 
    {
        SetScaleFactor(2);
    }

    //!
    // @category video
    //
    // Double up the screen to 3x its normal size.
    //

    if (M_CheckParm("-3")) 
    {
        SetScaleFactor(3);
    }
}

// Check if we have been invoked as a screensaver by xscreensaver.

void I_CheckIsScreensaver(void)
{
    char *env;

    env = getenv("XSCREENSAVER_WINDOW");

    if (env != NULL)
    {
        screensaver_mode = true;
    }
}

static void CreateCursors(void)
{
    static Uint8 empty_cursor_data = 0;

    // Save the default cursor so it can be recalled later

    cursors[1] = SDL_GetCursor();

    // Create an empty cursor

    cursors[0] = SDL_CreateCursor(&empty_cursor_data,
                                  &empty_cursor_data,
                                  1, 1, 0, 0);
}

static void SetSDLVideoDriver(void)
{
    // Allow a default value for the SDL video driver to be specified
    // in the configuration file.

    if (strcmp(video_driver, "") != 0)
    {
        char *env_string;

        env_string = M_StringJoin("SDL_VIDEODRIVER=", video_driver, NULL);
        putenv(env_string);
        free(env_string);
    }
}

static void SetWindowPositionVars(void)
{
    char buf[64];
    int x, y;

    if (window_position == NULL || !strcmp(window_position, ""))
    {
        return;
    }

    if (!strcmp(window_position, "center"))
    {
        putenv("SDL_VIDEO_CENTERED=1");
    }
    else if (sscanf(window_position, "%i,%i", &x, &y) == 2)
    {
        M_snprintf(buf, sizeof(buf), "SDL_VIDEO_WINDOW_POS=%i,%i", x, y);
        putenv(buf);
    }
}

static char *WindowBoxType(screen_mode_t *mode, int w, int h)
{
    if (mode->width != w && mode->height != h) 
    {
        return "Windowboxed";
    }
    else if (mode->width == w) 
    {
        return "Letterboxed";
    }
    else if (mode->height == h)
    {
        return "Pillarboxed";
    }
    else
    {
        return "...";
    }
}

static void SetVideoMode(screen_mode_t *mode, int w, int h)
{
    byte *doompal;
    int flags = 0;

    doompal = W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE);

    // If we are already running and in a true color mode, we need
    // to free the screenbuffer surface before setting the new mode.

    // [SVE] svillarreal - from gl scale branch
    if (!using_opengl && screenbuffer != NULL && screen != screenbuffer)
    {
        SDL_FreeSurface(screenbuffer);
    }

    // Generate lookup tables before setting the video mode.

    if (mode != NULL && mode->InitMode != NULL)
    {
        mode->InitMode(doompal);
    }

    if (fullscreen)
    {
        flags |= SDL_FULLSCREEN;
    }
    // [SVE] svillarreal - this is a huge hassle to support with
    // the OpenGL context
#if 0
    else
    {
        // In windowed mode, the window can be resized while the game is
        // running. Mac OS X has a quirk where an ugly resize handle is
        // shown in software mode when resizing is enabled, so avoid that.
#ifdef __MACOSX__
        if (using_opengl)
#endif
        {
            flags |= SDL_RESIZABLE;
        }
    }
#endif

    // [SVE] svillarreal - from gl scale branch
    if (using_opengl)
    {
        flags |= SDL_OPENGL;
        screen_bpp = 32;
    }
    else
    {
        flags |= SDL_SWSURFACE | SDL_DOUBLEBUF;

        if (screen_bpp == 8)
        {
            flags |= SDL_HWPALETTE;
        }
    }

    screen = SDL_SetVideoMode(w, h, screen_bpp, flags);

    if (screen == NULL)
    {
        I_Error("Error setting video mode %ix%ix%ibpp: %s\n",
                w, h, screen_bpp, SDL_GetError());
    }

    // [SVE] svillarreal
    DEH_printf("GL_Init: Initializing OpenGL extensions\n");
    GL_Init();

    DEH_printf("RB_Init: Initializing OpenGL render backend\n");
    RB_Init();

    // [SVE] svillarreal - from gl scale branch
    if (using_opengl)
    {
        // Try to initialize OpenGL scaling backend. This can fail,
        // because we need an OpenGL context before we can find out
        // if we have all the extensions that we need to make it work.
        // If it does, then fall back to software mode instead.

        if (!I_GL_InitScale(screen->w, screen->h))
        {
            fprintf(stderr,
                    "Failed to initialize in OpenGL mode. "
                    "Falling back to software mode instead.\n");
            using_opengl = false;

            // TODO: This leaves us in window with borders around it.
            // We shouldn't call with NULL here; this needs to be refactored
            // so that 'mode' isn't even an argument to this function.
            SetVideoMode(NULL, w, h);
            return;
        }
    }
    else
    {
        // Blank out the full screen area in case there is any junk in
        // the borders that won't otherwise be overwritten.

        SDL_FillRect(screen, NULL, 0);

        // If mode was not set, it must be set now that we know the
        // screen size.

        if (mode == NULL)
        {
            mode = I_FindScreenMode(screen->w, screen->h);

            if (mode == NULL)
            {
                I_Error("I_InitGraphics: Unable to find a screen mode small "
                        "enough for %ix%i", screen->w, screen->h);
            }

            // Generate lookup tables before setting the video mode.

            if (mode->InitMode != NULL)
            {
                mode->InitMode(doompal);
            }
        }

        // Save screen mode.

        screen_mode = mode;

        // Create the screenbuffer surface; if we have a real 8-bit palettized
        // screen, then we can use the screen as the screenbuffer.

        if (screen->format->BitsPerPixel == 8)
        {
            screenbuffer = screen;
        }
        else
        {
            screenbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,
                                                mode->width, mode->height, 8,
                                                0, 0, 0, 0);

            SDL_FillRect(screenbuffer, NULL, 0);
        }
    }
}

static void ApplyWindowResize(unsigned int w, unsigned int h)
{
    screen_mode_t *mode;

    // Find the biggest screen mode that will fall within these
    // dimensions, falling back to the smallest mode possible if
    // none is found.

    mode = I_FindScreenMode(w, h);

    if (mode == NULL)
    {
        mode = I_FindScreenMode(SCREENWIDTH, SCREENHEIGHT);
    }

    // Reset mode to resize window.

    printf("Resize to %ix%i\n", mode->width, mode->height);
    SetVideoMode(mode, mode->width, mode->height);

    // Save settings.

    screen_width  = default_screen_width  = mode->width;
    screen_height = default_screen_height = mode->height;
}

void I_InitGraphics(void)
{
    SDL_Event dummy;
    byte *doompal;
    char *env;

    // Pass through the XSCREENSAVER_WINDOW environment variable to 
    // SDL_WINDOWID, to embed the SDL window into the Xscreensaver
    // window.

    env = getenv("XSCREENSAVER_WINDOW");

    if (env != NULL)
    {
        char winenv[30];
        int winid;

        sscanf(env, "0x%x", &winid);
        M_snprintf(winenv, sizeof(winenv), "SDL_WINDOWID=%i", winid);

        putenv(winenv);
    }

    SetSDLVideoDriver();
    SetWindowPositionVars();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) 
    {
        I_Error("Failed to initialize video: %s", SDL_GetError());
    }

    // Set up title and icon.  Windows cares about the ordering; this
    // has to be done before the call to SDL_SetVideoMode.

    I_InitWindowTitle();
    
#ifndef __MACOSX__
#if !SDL_VERSION_ATLEAST(1, 3, 0)
    I_InitWindowIcon();
#endif
#endif

    // Warning to OS X users... though they might never see it :(
#ifdef __MACOSX__
    if (fullscreen)
    {
        printf("Some old versions of OS X might crash in fullscreen mode.\n"
               "If this happens to you, switch back to windowed mode.\n");
    }
#endif

    // [SVE] svillarreal - from gl scale branch
    // If we're using OpenGL, call the preinit function now; if it fails
    // then we have to fall back to software mode.

    if (using_opengl && !GL_PreInit())
    {
        using_opengl = false;
        // [SVE] svillarreal - OpenGL must be supported
        I_Error("Failed to initialize OpenGL");
        return;
    }

    //
    // Enter into graphics mode.
    //
    // When in screensaver mode, run full screen and auto detect
    // screen dimensions (don't change video mode)
    //

    if (screensaver_mode)
    {
        SetVideoMode(NULL, 0, 0);
    }
    else
    {
        int w, h;

        if (autoadjust_video_settings)
        {
            I_AutoAdjustSettings();
        }

        w = screen_width;
        h = screen_height;

        screen_mode = I_FindScreenMode(w, h);

        if (screen_mode == NULL)
        {
            I_Error("I_InitGraphics: Unable to find a screen mode small "
                    "enough for %ix%i", w, h);
        }

        if (w != screen_mode->width || h != screen_mode->height)
        {
            printf("I_InitGraphics: %s (%ix%i within %ix%i)\n",
                   WindowBoxType(screen_mode, w, h),
                   screen_mode->width, screen_mode->height, w, h);
        }

        SetVideoMode(screen_mode, w, h);
    }

    // Set the palette

    doompal = W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE);
    I_SetPalette(doompal);

    if (!using_opengl)
    {
        SDL_SetColors(screenbuffer, palette, 0, 256);

        // Start with a clear black screen
        // (screen will be flipped after we set the palette)

        SDL_FillRect(screenbuffer, NULL, 0);
    }

    CreateCursors();

    UpdateGrab();

    // On some systems, it takes a second or so for the screen to settle
    // after changing modes.  We include the option to add a delay when
    // setting the screen mode, so that the game doesn't start immediately
    // with the player unable to see anything.

    if (fullscreen && !screensaver_mode)
    {
        SDL_Delay(startup_delay);
    }

    // Check if we have a native surface we can use
    // If we have to lock the screen, draw to a buffer and copy
    // Likewise if the screen pitch is not the same as the width
    // If we have to multiply, drawing is done to a separate 320x200 buf

    native_surface = !using_opengl
                  && screen == screenbuffer
                  && !SDL_MUSTLOCK(screen)
                  && screen_mode == &mode_scale_1x
                  && screen->pitch == SCREENWIDTH
                  && aspect_ratio_correct;

    // If not, allocate a buffer and copy from that buffer to the
    // screen when we do an update

    if (native_surface)
    {
	I_VideoBuffer = (unsigned char *) screen->pixels;

        I_VideoBuffer += (screen->h - SCREENHEIGHT) / 2;
    }
    else
    {
	I_VideoBuffer = (unsigned char *) Z_Malloc(SCREENWIDTH * SCREENHEIGHT,
                                                   PU_STATIC, NULL);
    }

    V_RestoreBuffer();

    // Clear the screen to black.

    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);

    // We need SDL to give us translated versions of keys as well

    SDL_EnableUNICODE(1);

    // Repeat key presses - this is what Vanilla Doom does
    // Not sure about repeat rate - probably dependent on which DOS
    // driver is used.  This is good enough though.

    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

    // Clear out any events waiting at the start:

    while (SDL_PollEvent(&dummy));

    initialized = true;

    // Call I_ShutdownGraphics on quit

    I_AtExit(I_ShutdownGraphics, true);
}

// Bind all variables controlling video options into the configuration
// file system.

void I_BindVideoVariables(void)
{
    M_BindVariable("use_mouse",                 &usemouse);
    M_BindVariable("autoadjust_video_settings", &autoadjust_video_settings);
    M_BindVariable("aspect_ratio_correct",      &aspect_ratio_correct);
    M_BindVariable("startup_delay",             &startup_delay);
    M_BindVariable("screen_init",               &screen_init);
    M_BindVariable("screen_bpp",                &screen_bpp);
    M_BindVariable("grabmouse",                 &grabmouse);
    M_BindVariable("video_driver",              &video_driver);
    M_BindVariable("window_position",           &window_position);
    M_BindVariable("usegamma",                  &usegamma);
    M_BindVariable("gl_max_scale",              &gl_max_scale);
    M_BindVariable("png_screenshots",           &png_screenshots);

    // [SVE]
    M_BindVariableWithDefault("fullscreen",    &fullscreen,    &default_fullscreen);
    M_BindVariableWithDefault("screen_width",  &screen_width,  &default_screen_width);
    M_BindVariableWithDefault("screen_height", &screen_height, &default_screen_height);

    // Windows Vista or later?  Set screen color depth to
    // 32 bits per pixel, as 8-bit palettized screen modes
    // don't work properly in recent versions.

#if defined(_WIN32) && !defined(_WIN32_WCE)
    {
        OSVERSIONINFOEX version_info;

        ZeroMemory(&version_info, sizeof(OSVERSIONINFOEX));
        version_info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

        GetVersionEx((OSVERSIONINFO *) &version_info);

        if (version_info.dwPlatformId == VER_PLATFORM_WIN32_NT
         && version_info.dwMajorVersion >= 6)
        {
            screen_bpp = 32;
        }
    }
#endif

    // Disable fullscreen by default on OS X, as there is an SDL bug
    // where some old versions of OS X (<= Snow Leopard) crash.

#ifdef __MACOSX__
    fullscreen    = default_fullscreen    = 0;
    screen_width  = default_screen_width  = 800;
    screen_height = default_screen_height = 600;
    screen_init = true;
#endif
}
