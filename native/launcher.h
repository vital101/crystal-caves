/* Native episode launcher (the Episode 1 binary only).
 *
 * `caves` shows a picker before booting, styled like the original text
 * windows (MNI font via the core's text renderer).  Episode 1 continues
 * in-process; selecting Episode 2/3 executes the sibling binary
 * (caves_ep2/caves_ep3) found next to this executable and exits.  A missing
 * sibling is simply not listed, so a lone `caves` still boots Episode 1.
 */
#ifndef LAUNCHER_H
#define LAUNCHER_H

/* Show the picker.  Returns 1 to boot this binary's game, 0 when the user
 * quit.  Episodes 2/3 never return: the sibling binary is executed.  With
 * menu_smoke set, the menu runs headless (dummy drivers), a scripted
 * Down+Enter is fed in, the picked episode is printed as
 * "MENU OK: selected <ep> of <n> entries" and returned without spawning. */
int cc_launcher_pick(int menu_smoke);

/* Execute the episode-N sibling binary (never returns on success). */
void cc_launcher_spawn(int episode);

#endif /* LAUNCHER_H */
