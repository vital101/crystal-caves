/* Native episode launcher: the picker menu shown by the Episode 1 binary.
 *
 * The menu is drawn with the core's original text renderer (MNI font, the
 * same window frames the in-game text windows use) into the 320x200 view
 * framebuffer, blitted integer-scaled like the game.  Selecting Episode 1
 * returns to the caller (which boots the game); Episode 2/3 replace this
 * process with the matching sibling executable, so save/config files stay
 * with the episode that wrote them (persist.c namespacing).
 *
 * `caves --menu-smoke` runs the menu headless with a scripted Down+Enter and
 * prints "MENU OK: selected <ep> of <n> entries" instead of spawning.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "render.h"
#include "text.h"
#include "cc_host.h"
#include "assets_embed.h"
#include "launcher.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define W 320
#define H 200

/* One frame of the original 5-frame cursor animation is 21 PIT ticks
 * (one tick = divisor 0x2147 at 1193182 Hz, ~7.14 ms -> ~150 ms a frame). */
static const int CURSOR_FRAME_MS = 21 * 1000 * 0x2147 / 1193182.0;

static const char *EP_TITLES[3] = {
    "1  TROUBLE IN TWIN PLANETOIDS",
    "2  SLUGGING IT OUT",
    "3  MYLO VS. THE SUPERNOVA",
};
static const char *EP_BINS[3] = { NULL, "caves_ep2", "caves_ep3" };

/* ------------------------------------------------------------ siblings */

/* Absolute path of the episode-N sibling executable, or NULL. */
static char *sibling_path(int episode) {
    if (episode < 2 || episode > 3) return NULL;
    const char *base = SDL_GetBasePath();   /* SDL-owned, do not free */
    if (!base) return NULL;
    size_t len = strlen(base) + strlen(EP_BINS[episode - 1]) + 8;
    char *path = malloc(len);
    if (!path) return NULL;
#ifdef _WIN32
    snprintf(path, len, "%s%s.exe", base, EP_BINS[episode - 1]);
#else
    snprintf(path, len, "%s%s", base, EP_BINS[episode - 1]);
#endif
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        free(path);
        return NULL;
    }
    return path;
}

void cc_launcher_spawn(int episode) {
    char *path = sibling_path(episode);
    if (!path) {
        fprintf(stderr, "caves_ep%d: sibling executable not found\n", episode);
        exit(1);
    }
#ifdef _WIN32
    wchar_t wprog[MAX_PATH], wcmd[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wprog, MAX_PATH) ||
        !MultiByteToWideChar(CP_UTF8, 0, path, -1, wcmd, MAX_PATH)) {
        fprintf(stderr, "cannot convert %s to a Windows path\n", path);
        exit(1);
    }
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(wprog, wcmd, NULL, NULL, FALSE, 0, NULL, NULL,
                        &si, &pi)) {
        fprintf(stderr, "CreateProcess %s failed\n", path);
        exit(1);
    }
    free(path);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    ExitProcess(code);
#else
    char *argv0 = path;
    execv(path, (char *[]){ argv0, NULL });
    fprintf(stderr, "exec %s failed\n", path);
    exit(1);
#endif
}

/* ---------------------------------------------------------------- menu */

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;

static void present(void) {
    SDL_UpdateTexture(g_texture, NULL, cc_view_framebuffer(), W * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

static void draw_menu(int sel, const int *eps, int n, Uint64 now) {
    int cursor = (int)(now / (Uint64)CURSOR_FRAME_MS % 5) + 1;
    cc_view_clear(0);
    /* Window frame: interior cells wx+1..rx-1 / wy+1..by, i.e. x 32..295,
     * y 48..151 for the geometry below. */
    cc_text_draw_frame(24, 40, 288, 120);
    cc_text_draw_string(108, 56, "CRYSTAL CAVES");
    cc_text_draw_string(92, 72, "SELECT AN EPISODE");
    for (int i = 0; i < n; i++) {
        int y = 104 + 16 * i;
        cc_text_draw_string(40, y, EP_TITLES[eps[i] - 1]);
        if (i == sel) cc_text_cursor_draw(4, y / 8, cursor);
    }
    cc_text_draw_string(116, 144, "ESC TO QUIT");
    present();
}

int cc_launcher_pick(int menu_smoke) {
    /* Only the fonts and palette are needed; the game boot re-does the full
     * asset setup. */
    cc_text_set_fonts(cc_asset_mni_f1, cc_asset_mni_f2, cc_asset_mni_spl);
    cc_set_palette(cc_palette32);

    /* Listed entries: Episode 1 always; 2/3 only when their sibling binary
     * sits next to this executable. */
    int eps[3], n = 0;
    for (int e = 1; e <= 3; e++) {
        char *path = sibling_path(e);
        int avail = (e == 1 || path != NULL);
        free(path);
        if (avail) eps[n++] = e;
    }

    if (menu_smoke) {
        SDL_Event down, up, ret;
        memset(&down, 0, sizeof(down));
        memset(&up, 0, sizeof(up));
        memset(&ret, 0, sizeof(ret));
        down.type = SDL_EVENT_KEY_DOWN;
        down.key.key = SDLK_DOWN;
        down.key.scancode = SDL_SCANCODE_DOWN;
        up = down;
        up.type = SDL_EVENT_KEY_UP;
        ret.type = SDL_EVENT_KEY_DOWN;
        ret.key.key = SDLK_RETURN;
        ret.key.scancode = SDL_SCANCODE_RETURN;
        SDL_PushEvent(&down);
        SDL_PushEvent(&up);
        SDL_PushEvent(&ret);
    }

    g_window = SDL_CreateWindow("Crystal Caves", W * 3, H * 3,
                                SDL_WINDOW_RESIZABLE);
    g_renderer = g_window ? SDL_CreateRenderer(g_window, NULL) : NULL;
    g_texture = g_renderer
        ? SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STREAMING, W, H)
        : NULL;
    if (!g_texture) {
        /* No usable video (or no display): skip the picker and boot. */
        fprintf(stderr, "launcher: no window (%s), booting episode 1\n",
                SDL_GetError());
        if (g_renderer) SDL_DestroyRenderer(g_renderer);
        if (g_window) SDL_DestroyWindow(g_window);
        g_renderer = NULL;
        g_window = NULL;
        return 1;
    }
    SDL_SetRenderLogicalPresentation(g_renderer, W, H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    int sel = 0, picked = 0, quit = 0, done = 0;
    while (!done) {
        SDL_Event ev;
        int pending;
        while ((pending = SDL_PollEvent(&ev)) && !done) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                quit = 1;
                done = 1;
                break;
            case SDL_EVENT_KEY_DOWN:
                switch (ev.key.key) {
                case SDLK_UP: case SDLK_W:
                    sel = (sel + n - 1) % n;
                    break;
                case SDLK_DOWN: case SDLK_S:
                    sel = (sel + 1) % n;
                    break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                    picked = eps[sel];
                    done = 1;
                    break;
                case SDLK_ESCAPE:
                    quit = 1;
                    done = 1;
                    break;
                default:
                    if (ev.key.key >= '1' && ev.key.key <= '3') {
                        int e = ev.key.key - '0';
                        for (int i = 0; i < n; i++) {
                            if (eps[i] == e) { picked = e; sel = i; done = 1; }
                        }
                    }
                    break;
                }
                break;
            default:
                break;
            }
        }
        if (done) break;
        if (menu_smoke) {
            /* The scripted events are consumed; report where they landed. */
            picked = eps[sel];
            done = 1;
            break;
        }
        draw_menu(sel, eps, n, SDL_GetTicks());
        SDL_Delay(16);
    }

    SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    g_texture = NULL;
    g_renderer = NULL;
    g_window = NULL;
    if (quit) return 0;
    if (menu_smoke) {
        printf("MENU OK: selected %d of %d entries\n", picked, n);
        return picked;
    }
    if (picked == 1) return 1;
    cc_launcher_spawn(picked);   /* episodes 2/3: never returns */
    return 0;
}
