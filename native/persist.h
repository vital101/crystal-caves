#ifndef CC_PERSIST_H
#define CC_PERSIST_H

/* Native port of wasm/persist.js.  Same observable rules and the same file
 * formats the browser stores in localStorage, but as plain files in the
 * per-user pref directory (SDL_GetPrefPath):
 *
 *   cc1.config.v2.json    JSON config (sound/joystick/video/keys)
 *   cc1.config.v1.json    legacy config (migrated on load)
 *   cc1.hiscores.v1.json  JSON array of ten { name, score } rows
 *   cc1.save.<slot>.b64   base64 of the versioned cc_state_save() bytes
 */

typedef struct {
    int version;
    int sound, joystick, video;
    int keys[4];
} cc_config;

/* Call once at startup (uses SDL_GetPrefPath). */
void cc_persist_init(void);

/* Test hook: pin the directory (persist_test.c — save-file isolation). */
void cc_persist_set_dir_for_test(const char *dir);

cc_config cc_load_config(void);          /* validated, defaults on any error */
int       cc_save_config(const cc_config *cfg);

/* Load the ten-row table into the core; on missing/corrupt data writes the
 * current (default) table back out, like CC1.HS all-or-nothing. */
int  cc_load_hiscores(void);
int  cc_save_hiscores(void);

/* Save slots: cc_state_save() bytes base64'd into cc1.save.<slot>.b64.
 * cc_load_slot() copies into stateP and runs cc_state_load_ex(hub_only). */
int  cc_save_slot(int slot, unsigned char *stateP, int stateSize);
int  cc_load_slot(int slot, unsigned char *stateP, int stateSize, int hubOnly);

#endif
