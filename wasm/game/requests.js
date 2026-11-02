/* Crystal Caves - save/restore request handshake.
 *
 * The shell asks the host to save or restore a slot and then waits for
 * cc_shell_request_done().  This module performs the localStorage side through
 * CCPersist and reports success back into the wasm state machine.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CCGameRequests = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  /**
   * Bind the request handler to one running game instance.
   * @param {{
   *   e: object,
   *   persist: object,
   *   storage: Storage,
   *   stateP: number,
   *   stateSize: number,
   *   mem: function(): Uint8Array,
   *   render: function(): void,
   *   renderLegend: function(): void
   * }} opts
   * @returns {{handleRequest: function(): void}}
   */
  function create(opts) {
    const e = opts.e;
    const persist = opts.persist;
    const storage = opts.storage;
    const stateP = opts.stateP;
    const stateSize = opts.stateSize;
    const mem = opts.mem;
    const render = opts.render;
    const renderLegend = opts.renderLegend;

    /** Service one pending save/restore request, if any. */
    function handleRequest() {
      if (!e.cc_shell_request_pending()) return;
      const kind = e.cc_shell_request_kind();
      const slot = e.cc_shell_request_slot();
      let ok = 0;
      if (kind === 1)
        ok = persist.saveSlot(storage, slot, e, stateP, stateSize, mem) ? 1 : 0;
      else if (kind === 2)
        ok = persist.loadSlot(storage, slot, e, stateP, stateSize, mem, true) ? 1 : 0;
      e.cc_shell_request_done(ok);
      if (kind === 2 && ok) renderLegend();
      render();
    }

    return { handleRequest };
  }

  return { create };
});
