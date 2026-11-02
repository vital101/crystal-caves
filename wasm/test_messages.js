// Phase 5 in-game message trigger regression (P5.R2).
// Exercises the cell-event branches that display the remaining messages and
// their guard conditions, using the real shell state machine.
const { loadGame, readJson } = require('./test_helpers.js');

const GAME = 4;
const MESSAGE = 14;
// Derive the enum indices from the exported block table so adding blocks
// does not silently shift the expected message ids.
const blockTable = readJson('../out/portable/shell_text.json');
const IDX = {};
blockTable.blocks.forEach((b, i) => { IDX[b.id] = i; });
const BLOCK = {
  last_crystal: IDX.message_last_crystal,
  stunned: IDX.message_stunned,
  more_crystals: IDX.message_more_crystals,
  red_mushroom: IDX.message_red_mushroom,
  lever: IDX.message_lever,
  power_shot: IDX.message_power_shot,
  poison: IDX.message_poison,
  gravity: IDX.message_gravity,
  perfect_health: IDX.message_perfect_health
};

function assert(cond, msg) {
  if (!cond) { console.error('FAIL:', msg); process.exitCode = 1; }
  else console.log('PASS', msg);
}

(async () => {
  const { e } = await loadGame();

  // Enter the game through the real title/menu/intro flow.
  e.cc_shell_init();
  e.cc_shell_key(13);            // logo -> credits
  e.cc_shell_key(13);            // credits -> title
  e.cc_shell_key(13);            // title -> menu
  e.cc_shell_key('n'.charCodeAt(0));
  for (let i = 0; i < 8 && e.cc_shell_state() === 3; i++) e.cc_shell_key(13);
  assert(e.cc_shell_state() === GAME && e.cc_cur_level() === 2,
         'message tests start in the hub');

  const col = 30, row = 20;
  const setWord = (off, val) => {
    e.cc_cell_set(col, row, off, val & 0xFF);
    e.cc_cell_set(col, row, off + 1, (val >> 8) & 0xFF);
  };
  const clearCell = () => {
    for (let off = 0; off < 20; off++) e.cc_cell_set(col, row, off, 0);
  };
  const resetFlags = () => {
    const flags = [0xA0F5, 0xA0F6, 0xA101, 0xA102, 0xA104, 0xA106,
                   0xA109, 0xA10D, 0x513C, 0x5140];
    for (const off of flags) e.cc_global_set(off, 0);
    e.cc_global_set(0x509E, 3);
  };
  const fire = () => e.cc_test_cell_event(col * 16, row * 16);
  const dismiss = () => e.cc_shell_key(27);

  function expectMessage(label, setup, block, mutate) {
    resetFlags();
    clearCell();
    if (setup) setup();
    fire();
    const gotState = e.cc_shell_state();
    const gotBlock = e.cc_shell_message_block();
    assert(gotState === MESSAGE && gotBlock === block,
           `${label} message (state ${gotState}, block ${gotBlock}, want ${block})`);
    if (mutate) mutate();
    if (gotState === MESSAGE) {
      dismiss();
      assert(e.cc_shell_state() === GAME, `${label} dismiss returns to game`);
    }
  }

  function expectNoMessage(label, setup) {
    resetFlags();
    clearCell();
    if (setup) setup();
    fire();
    const gotState = e.cc_shell_state();
    const gotBlock = e.cc_shell_message_block();
    assert(gotState === GAME && gotBlock === -1,
           `${label} stays quiet (state ${gotState}, block ${gotBlock})`);
  }

  // Power shot: only when the power-shot latch is set.
  expectMessage('power shot', () => {
    setWord(0x0C, 6);
    setWord(0x0E, 0x32);
    e.cc_global_set(0xA104, 1);
  }, BLOCK.power_shot);
  expectNoMessage('power shot without latch', () => {
    setWord(0x0C, 6);
    setWord(0x0E, 0x32);
  });

  // Gravity switch: only when its latch is set.
  expectMessage('gravity', () => {
    setWord(0x0C, 0x0C);
    setWord(0x0E, 0x26);
    e.cc_global_set(0xA102, 1);
  }, BLOCK.gravity);
  expectNoMessage('gravity without latch', () => {
    setWord(0x0C, 0x0C);
    setWord(0x0E, 0x26);
  });

  // Red mushroom: unconditional, and must not show the gravity message.
  expectMessage('red mushroom', () => {
    setWord(0x0C, 0x0D);
    setWord(0x0E, 0x2D);
  }, BLOCK.red_mushroom);

  // Poison: only with the gravity/immune gate.
  expectMessage('poison', () => {
    setWord(0x0C, 0x0D);
    setWord(0x0E, 0x2E);
    e.cc_global_set(0x513C, 1);
    e.cc_global_set(0x5140, 0);
  }, BLOCK.poison);
  expectNoMessage('poison with immunity', () => {
    setWord(0x0C, 0x0D);
    setWord(0x0E, 0x2E);
    e.cc_global_set(0x513C, 1);
    e.cc_global_set(0x5140, 1);
  });

  // Stunned creatures: only when the stun latch is set.
  expectMessage('stunned', () => {
    setWord(0x0C, 0x0E);
    setWord(0x0E, 0x19);
    e.cc_global_set(0xA101, 1);
  }, BLOCK.stunned);
  expectNoMessage('stunned without latch', () => {
    setWord(0x0C, 0x0E);
    setWord(0x0E, 0x19);
  });

  // Perfect health: exit tiles, unlocked exit, full health.
  expectMessage('perfect health', () => {
    setWord(0x06, 0x11);
    setWord(0x0A, 0x0C);
    e.cc_global_set(0xA0F5, 1);
    e.cc_global_set(0xA0F6, 0);
    e.cc_global_set(0x509E, 3);
  }, BLOCK.perfect_health);
  expectNoMessage('perfect health at two hearts', () => {
    setWord(0x06, 0x11);
    setWord(0x0A, 0x0C);
    e.cc_global_set(0xA0F5, 1);
    e.cc_global_set(0xA0F6, 0);
    e.cc_global_set(0x509E, 2);
  });

  // More crystals: exit latch without the unlocked-exit flag.
  expectMessage('more crystals', () => {
    setWord(0x06, 0x0D);
    setWord(0x0A, 0x0C);
    e.cc_global_set(0xA0F5, 0);
    e.cc_global_set(0xA106, 1);
  }, BLOCK.more_crystals);
  expectNoMessage('more crystals without latch', () => {
    setWord(0x06, 0x0D);
    setWord(0x0A, 0x0C);
    e.cc_global_set(0xA0F5, 0);
    e.cc_global_set(0xA106, 0);
  });

  // Levers: all three switch pairs share the same message trigger.
  for (const w3 of [0x11, 0x15, 0x13]) {
    expectMessage(`lever ${w3.toString(16)}`, () => {
      setWord(0x06, w3);
      setWord(0x0A, 9);
      e.cc_global_set(0xA109, 1);
    }, BLOCK.lever);
  }
  expectNoMessage('lever without latch', () => {
    setWord(0x06, 0x11);
    setWord(0x0A, 9);
  });

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASS message trigger regression');
})();
