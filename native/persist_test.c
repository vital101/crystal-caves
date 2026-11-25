/* Per-episode save-file isolation test (native).
 *
 * Each of the three variants (persist_test_ep1/2/3, -DCC_EPISODE=N) runs
 * against the SAME directory and must only ever touch its own cc<N>.* files:
 *
 *   1. its files do not exist yet in a fresh dir (distinct namespaces),
 *   2. it writes config + hiscores + save slot 0 and loads them back,
 *   3. every file present at start (another episode's) is byte-identical at
 *      end — a clobbering write in any layer fails the run.
 *
 * The Makefile target test-persist-isolation chains the three variants in
 * one shared temp directory.  Uses persist.c's cc_persist_set_dir_for_test,
 * so no SDL pref-path redirection is needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "persist.h"
#include "objects.h"
#include "shell.h"
#include "cc_host.h"

#ifndef CC_EPISODE
#define CC_EPISODE 1
#endif

#define FAMILIES 3   /* config, hiscores, save slot 0 */

static const char *TMPL[FAMILIES] = {
    "cc%d.config.v2.json", "cc%d.hiscores.v1.json", "cc%d.save.0.b64" };

static char *read_file_bytes(const char *dir, const char *name, long *out_len) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf || (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n)) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = 0;
    *out_len = n;
    return buf;
}

static void expect(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "persist_test_ep%d: FAIL %s\n", CC_EPISODE, what);
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: persist_test_ep%d <dir>\n", CC_EPISODE);
        return 2;
    }
    const char *dir = argv[1];

    /* Snapshot every episode's file family before touching anything. */
    char names[3][FAMILIES][64];
    char *snap[3][FAMILIES] = { 0 };
    long snap_len[3][FAMILIES] = { 0 };
    for (int ep = 1; ep <= 3; ep++) {
        for (int i = 0; i < FAMILIES; i++) {
            snprintf(names[ep - 1][i], sizeof(names[ep - 1][i]),
                     TMPL[i], ep);
            snap[ep - 1][i] = read_file_bytes(dir, names[ep - 1][i],
                                              &snap_len[ep - 1][i]);
        }
    }

    /* Boot a deterministic hub state for the slot bytes. */
    cc_persist_set_dir_for_test(dir);
    int stateSize = cc_state_size();
    unsigned char *stateP = cc_alloc((unsigned)stateSize);
    expect(stateP != NULL, "state allocation");
    cc_shell_init();
    cc_objects_init(2);

    /* 1: none of our files exist yet (fresh dir, distinct namespaces). */
    for (int i = 0; i < FAMILIES; i++)
        expect(snap[CC_EPISODE - 1][i] == NULL,
               "fresh dir: our file already exists");

    /* 2: write all three families and load them back. */
    cc_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version = 2;
    cfg.sound = 0; cfg.joystick = 1; cfg.video = 1;
    cfg.keys[0] = 0x1E; cfg.keys[1] = 0x20;
    cfg.keys[2] = 0x39; cfg.keys[3] = 0x1C;
    expect(cc_save_config(&cfg) == 1, "config write");
    expect(cc_save_hiscores() == 1, "hiscores write");
    expect(cc_save_slot(0, stateP, stateSize) == 1, "slot 0 write");

    cc_config loaded = cc_load_config();
    expect(loaded.sound == 0 && loaded.joystick == 1 && loaded.video == 1 &&
           loaded.keys[0] == 0x1E && loaded.keys[3] == 0x1C,
           "config round-trips");
    expect(cc_load_hiscores() == 1, "hiscores reload");
    expect(cc_load_slot(0, stateP, stateSize, 0) == 1, "slot 0 reload");

    /* 3: everything present before is byte-identical now; ours appeared. */
    for (int ep = 1; ep <= 3; ep++) {
        for (int i = 0; i < FAMILIES; i++) {
            long len = 0;
            char *after = read_file_bytes(dir, names[ep - 1][i], &len);
            if (snap[ep - 1][i]) {
                expect(after != NULL && len == snap_len[ep - 1][i] &&
                       !memcmp(after, snap[ep - 1][i], (size_t)len),
                       "another episode's file was clobbered");
            } else if (ep == CC_EPISODE) {
                expect(after != NULL, "our file is missing after the write");
            }
            free(after);
        }
    }
    printf("PERSIST ISOLATION OK ep=%d\n", CC_EPISODE);
    return 0;
}
