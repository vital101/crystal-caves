#ifndef SHELL_H
#define SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

enum cc_shell_state {
    CC_SHELL_LOGO = 0,
    CC_SHELL_TITLE,
    CC_SHELL_MENU,
    CC_SHELL_TEXT,
    CC_SHELL_GAME,
    CC_SHELL_PAUSE,
    CC_SHELL_HELP,
    CC_SHELL_QUIT,
    CC_SHELL_SAVE,
    CC_SHELL_RESTORE,
    CC_SHELL_WARP,
    CC_SHELL_HISCORE,
    CC_SHELL_NAME_ENTRY,
    CC_SHELL_ENDING,
    CC_SHELL_MESSAGE,
    CC_SHELL_CREDITS,
    CC_SHELL_KEY_REDEFINE
};

/* Key values used by the shell (JS maps browser keys to these). */
enum {
    CC_KEY_BACKSPACE = 8,
    CC_KEY_ENTER = 13,
    CC_KEY_ESC = 27,
    CC_KEY_SPACE = 32,
    CC_KEY_F1 = 0x13B,
    CC_KEY_UP = 0x101,
    CC_KEY_DOWN = 0x102,
    CC_KEY_LEFT = 0x103,
    CC_KEY_RIGHT = 0x104
};

void cc_shell_init(void);
void cc_shell_step(void);
int  cc_shell_key(int key);
int  cc_shell_key_for_action(int action);
void cc_shell_set_key(int action, int key);
int  cc_shell_key_redefine_active(void);
int  cc_shell_key_redefine_step(void);
void cc_shell_render(void);
int  cc_shell_state(void);
int  cc_shell_message_block(void);
int  cc_shell_overlay(void);
int  cc_shell_hud_active(void);

/* Save/restore request handshake with the JS localStorage layer. */
int  cc_shell_request_pending(void);
int  cc_shell_request_kind(void);   /* 1 = save, 2 = restore */
int  cc_shell_request_slot(void);
void cc_shell_request_done(int ok);

/* Config (persisted by JS). */
int  cc_shell_sound_on(void);
int  cc_shell_joystick_on(void);
int  cc_shell_video_alt(void);
void cc_shell_set_config(int sound, int joystick, int video);

/* High score table (persisted by JS). */
int  cc_shell_hiscore_count(void);
const char *cc_shell_hiscore_name(int i);
unsigned int cc_shell_hiscore_score(int i);
void cc_shell_hiscore_set(int i, const char *name, unsigned int score);
void cc_shell_hiscore_add(const char *name, unsigned int score);
int  cc_shell_hiscore_qualifies(unsigned int score);
void cc_shell_finale_done(void);
void cc_shell_message(int block_id);
void cc_shell_notify_last_crystal(void);
void cc_shell_notify_gravity(void);
void cc_shell_notify_power_shot(void);
void cc_shell_notify_red_mushroom(void);
void cc_shell_notify_poison(void);
void cc_shell_notify_stunned(void);
void cc_shell_notify_perfect_health(void);
void cc_shell_notify_more_crystals(void);
void cc_shell_notify_lever(void);
void cc_shell_warp(int level_arg);

#ifdef __cplusplus
}
#endif

#endif
