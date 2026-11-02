/* Crystal Caves shell - high-score table.
 *
 * CC1.HS defaults/sort/tie semantics: names are displayed and entered as
 * 20 characters, insertion finds the first strictly-smaller entry, and
 * entries below it shift down one slot (the original table is 10 rows).
 */
#include "shell_internal.h"

HiScore g_hiscores[HISCORE_COUNT];
char g_name[24];
int g_name_len;

int cc_shell_hiscore_count(void) { return HISCORE_COUNT; }
const char *cc_shell_hiscore_name(int i) {
    return (i >= 0 && i < HISCORE_COUNT) ? g_hiscores[i].name : "";
}
unsigned int cc_shell_hiscore_score(int i) {
    return (i >= 0 && i < HISCORE_COUNT) ? g_hiscores[i].score : 0;
}
void cc_shell_hiscore_set(int i, const char *name, unsigned int score) {
    if (i < 0 || i >= HISCORE_COUNT) return;
    /* Names are displayed/entered as 20 characters in the original. */
    scopy(g_hiscores[i].name, name ? name : "", HISCORE_NAME_MAX + 1);
    g_hiscores[i].score = score;
}
int cc_shell_hiscore_qualifies(unsigned int score) {
    return score > g_hiscores[HISCORE_COUNT - 1].score;
}
void cc_shell_hiscore_add(const char *name, unsigned int score) {
    if (!cc_shell_hiscore_qualifies(score)) return;
    int pos = HISCORE_COUNT;
    for (int i = 0; i < HISCORE_COUNT; i++) {
        if (score > g_hiscores[i].score) { pos = i; break; }
    }
    if (pos >= HISCORE_COUNT) return;
    for (int i = HISCORE_COUNT - 1; i > pos; i--)
        g_hiscores[i] = g_hiscores[i - 1];
    scopy(g_hiscores[pos].name, name ? name : "", HISCORE_NAME_MAX + 1);
    g_hiscores[pos].score = score;
}
