/* Crystal Caves shell - internal umbrella header (Phase 2 split).
 *
 * Declares the types, constants, generated-data tables, file-scope globals
 * and cross-module helpers shared by the shell/ modules.  Definitions live
 * in the owning module.  This keeps the Phase 2 split mechanical; per-module
 * headers and a real state-machine design belong to Phase 3/4.
 *
 * The public API is shell.h and must not grow through this header.
 */
#ifndef SHELL_INTERNAL_H
#define SHELL_INTERNAL_H

#include "shell.h"
#include "text.h"
#include "render.h"
#include "objects.h"
#include "sound.h"
#include "shell_text_data.h"
#include "shell_exact_data.h"

#define MAX_PAGES 96
#define PAGE_LINES 18
#define HISCORE_NAME_MAX 20
#define HISCORE_COUNT 10

typedef struct { int block; int start; } TextPage;
typedef struct { char name[24]; unsigned int score; } HiScore;

/* ---- state.c: shell state machine -------------------------------------- */
extern int g_shell_state;
extern int g_timer;
extern int g_return_state;
extern int g_wait_sound;       /* finale ID 26: hold until [0x513F] clears */
extern int g_pending_ending;   /* enter CC1.END after the ID 26 fanfare */
extern int g_story_kind;       /* 0 none, 1 intro, 2 finale */
extern int g_story_level;      /* -1 = no level background */
extern int g_finale_pending;
extern unsigned int g_pending_score;

/* Input cursor animation (0x0FC5:0x0A18): F2 glyphs 45..49 at the '^'
 * marker cell, one frame every 21 PIT ticks (the original compares
 * [0x2B60] > 0x14). */
extern int g_cursor_ticks;
extern int g_cursor_frame;

/* Text-window page state. */
extern TextPage g_pages[MAX_PAGES];
extern int g_page_count;
extern int g_page_index;
extern const ShellPageDef *g_story_pages;
extern int g_story_page_count;

/* ---- scroll.c: title/credits attract loop (0x0FC5:0x8265) --------------- */
extern int g_scroll_phase;
extern int g_scroll_frame;
extern int g_scroll_row;
extern int g_scroll_dir;
extern int g_scroll_ticks;

/* ---- config.c: persisted config + committed key bindings ---------------- */
/* Selected keys before confirmation, and the committed action keys. */
extern int g_key_tmp[4];
extern int g_keys[4];
extern int g_key_action;
extern int g_sound_on;
extern int g_joystick_on;
extern int g_video_alt;
extern int g_config_dirty;

/* ---- hiscore.c: high-score table + name entry --------------------------- */
extern HiScore g_hiscores[HISCORE_COUNT];
extern char g_name[24];
extern int g_name_len;

/* ---- persist.c: save/restore request handshake -------------------------- */
extern int g_request_pending;
extern int g_request_kind;     /* 1 save, 2 restore */
extern int g_request_slot;

/* ---- input.c ------------------------------------------------------------ */
extern int g_warp_value;

/* ---- cross-module helpers ---------------------------------------------- */
/* state.c */
int slen(const char *s);
void scopy(char *d, const char *s, int cap);
void cursor_reset(void);
void cursor_step(int ticks);
void clear_pages(void);
void add_block_pages(int block);
void set_pages(const int *blocks, int n, int return_state,
               int story_kind, int story_level);
void set_story_pages(const ShellPageDef *pages, int n,
                     int return_state, int story_kind,
                     int story_level);
void show_block(int block, int return_state);
void enter_high_score(void);
void enter_ending(void);
void begin_ending_fanfare(void);

/* scroll.c */
void scroll_reset(void);
void scroll_enter_next(void);
void scroll_advance(int ticks);

#endif /* SHELL_INTERNAL_H */
