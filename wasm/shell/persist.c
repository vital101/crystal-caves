/* Crystal Caves shell - save/restore request handshake.
 *
 * cc_shell_key() arms a pending save/restore request; the JS localStorage
 * layer performs it and answers with cc_shell_request_done(ok).  The
 * original could only save in the hub (level arg 0 / loader level 2); the
 * matching DOS error windows are reproduced here.
 */
#include "shell_internal.h"

int g_request_pending;
int g_request_kind;     /* 1 save, 2 restore */
int g_request_slot;

int cc_shell_request_pending(void) { return g_request_pending; }
int cc_shell_request_kind(void) { return g_request_kind; }
int cc_shell_request_slot(void) { return g_request_slot; }

void cc_shell_request_done(int ok) {
    g_request_pending = 0;
    if (g_request_kind == 1) {
        if (ok) {
            show_block(SHELL_TEXT_MESSAGE_SAVED, CC_SHELL_GAME);
            g_shell_state = CC_SHELL_MESSAGE;
            clear_pages();
            add_block_pages(SHELL_TEXT_MESSAGE_SAVED);
            g_return_state = CC_SHELL_GAME;
        } else {
            int block = (cc_cur_level() == 2)
                ? SHELL_TEXT_MESSAGE_FILE_ERROR
                : SHELL_TEXT_MESSAGE_SAVE_HUB_ONLY;
            show_block(block, CC_SHELL_GAME);
            g_shell_state = CC_SHELL_MESSAGE;
            clear_pages();
            add_block_pages(block);
            g_return_state = CC_SHELL_GAME;
        }
    } else if (g_request_kind == 2) {
        if (ok) {
            g_shell_state = CC_SHELL_GAME;
        } else {
            show_block(SHELL_TEXT_MESSAGE_FILE_ERROR, CC_SHELL_GAME);
            g_shell_state = CC_SHELL_MESSAGE;
            clear_pages();
            add_block_pages(SHELL_TEXT_MESSAGE_FILE_ERROR);
            g_return_state = CC_SHELL_GAME;
        }
    }
}
