/* Crystal Caves shell - configuration and key tables.
 *
 * Sound/joystick/video config with the persisted-config dirty flag, plus
 * the committed action-key bindings and the DOS scan-code helpers.
 * cc_shell_set_key() mirrors each binding into the original gameplay
 * globals (0x2B6C/0x2B6E/0x2B70/0x2B7A) so the physics input path sees it.
 */
#include "shell_internal.h"

/* Selected keys before confirmation, and the committed action keys. */
int g_key_tmp[4];
int g_keys[4] = { 0, 0, 0, 0 };
int g_key_action;

int g_sound_on = 1;
int g_joystick_on = 0;
int g_video_alt = 0;
int g_config_dirty = 0;

int cc_shell_sound_on(void) { return g_sound_on; }
int cc_shell_joystick_on(void) { return g_joystick_on; }
int cc_shell_video_alt(void) { return g_video_alt; }
int cc_shell_config_dirty(void) { return g_config_dirty; }
void cc_shell_config_clear_dirty(void) { g_config_dirty = 0; }
int cc_shell_key_redefine_active(void) {
    return g_shell_state == CC_SHELL_KEY_REDEFINE;
}
int cc_shell_key_redefine_step(void) { return g_key_action; }
int cc_shell_key_for_action(int action) {
    if (action < 0 || action > 3) return 0;
    return g_keys[action];
}
int cc_shell_key_name(int scan, const char **name) {
    if (scan < 0 || scan > 0x7F) {
        if (name) *name = "NULL";
        return 0;
    }
    if (name) *name = g_key_names[scan];
    return g_key_names[scan][0] != 'N' || g_key_names[scan][1] != 'U';
}
int cc_shell_action_for_scan(int scan) {
    for (int i = 0; i < 4; i++)
        if (g_keys[i] == scan) return i;
    return -1;
}
void cc_shell_set_key(int action, int key) {
    static const int globals[4] = { 0x2B6C, 0x2B6E, 0x2B70, 0x2B7A };
    if (action < 0 || action > 3) return;
    if (key < 0 || key > 0x7F) return;
    g_keys[action] = key;
    cc_global_set(globals[action], key & 0xFFFF);
}
void cc_input_scan(int scan, int down) {
    scan &= 0x7F;
    if (scan == g_keys[0]) cc_input_set(0x338E, down ? 1 : 0);
    if (scan == g_keys[1]) cc_input_set(0x338F, down ? 1 : 0);
    if (scan == g_keys[2]) cc_input_set(0x338B, down ? 1 : 0);
    if (scan == g_keys[3]) cc_input_set(0x3390, down ? 1 : 0);
}

void cc_shell_set_config(int sound, int joystick, int video) {
    g_sound_on = sound ? 1 : 0;
    g_joystick_on = joystick ? 1 : 0;
    g_video_alt = video ? 1 : 0;
    cc_sound_set_enabled(g_sound_on);
}
