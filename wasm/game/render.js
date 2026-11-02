/* Crystal Caves - browser rendering helpers.
 *
 * The C side owns both framebuffers.  This module only asks it to draw the
 * current gameplay or shell frame and presents the 320x200 RGBA view through
 * the page's 2D canvas.  Keeping it separate from game.js means the frame loop
 * and the ?test=1 hook can share one renderer without either owning the canvas
 * details.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.CCGameRender = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const W = 320;
  const H = 200;

  /**
   * Bind the rendering helpers to one wasm instance and canvas context.
   * @param {{e: object, ctx: CanvasRenderingContext2D}} opts
   * @returns {{
   *   present: function(): void,
   *   renderGame: function(): void,
   *   renderShell: function(): void,
   *   render: function(): void
   * }}
   */
  function create(opts) {
    const e = opts.e;
    const ctx = opts.ctx;

    /** Copy the wasm view framebuffer into the canvas. */
    function present() {
      const fb = e.cc_view_framebuffer();
      const img = new ImageData(
        new Uint8ClampedArray(e.memory.buffer, fb, W * H * 4), W, H);
      ctx.putImageData(img, 0, 0);
    }

    /** Render cells, live objects, HUD and any shell overlay for gameplay. */
    function renderGame() {
      const level = e.cc_cur_level();
      const camX = e.cc_cam_x(), camY = e.cc_cam_y();
      e.cc_render_view_cells(level, camX, camY);
      for (let s = 0; s < e.cc_obj_count(); s++) {
        if (e.cc_obj_state(s) === 2 || e.cc_obj_kind(s) === 0) continue;
        const blk = e.cc_obj_tile(s);
        const sh = e.cc_obj_draw_shape(s);
        if (!blk || !sh) continue;
        e.cc_draw_object_view(blk, sh, e.cc_obj_x(s) - camX, e.cc_obj_y(s) - camY);
      }
      if (e.cc_shell_hud_active()) e.cc_hud_render_screen();
      if (e.cc_shell_overlay()) e.cc_shell_render();
      present();
    }

    /** Render one shell page (title, menus, text windows, ...). */
    function renderShell() {
      e.cc_shell_render();
      present();
    }

    /** Pick the renderer the current shell state expects. */
    function render() {
      if (e.cc_shell_state() === 4 || e.cc_shell_overlay()) renderGame();
      else renderShell();
    }

    return { present, renderGame, renderShell, render };
  }

  return { W, H, create };
});
