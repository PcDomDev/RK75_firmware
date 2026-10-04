// Registers this project's custom RGB Matrix effects. Required by RGB_MATRIX_CUSTOM_USER=yes
// (rules.mk).
//
// IMPORTANT: this file is textually #included multiple times, by both rgb_matrix.h and
// rgb_matrix.c, under different RGB_MATRIX_EFFECT macro definitions (enum entry, dispatch
// switch case, effect-name-string switch case) and with RGB_MATRIX_CUSTOM_EFFECT_IMPLS
// defined only during one specific pass (verified directly against this fork's source, not
// assumed from stock QMK). Critically, the enum-generation pass (rgb_matrix.h) places this
// file's content directly inside an `enum { ... }` body -- an ordinary #include of a header
// full of declarations would not be legal C there. The #include below is guarded so it only
// ever fires during the one pass where a normal top-level file scope actually exists.
//
// Order matters here beyond just registration: quantum/rgb_matrix's own enum lists every stock
// effect first (fixed by QMK core -- verified directly against the pinned commit's own
// quantum/rgb_matrix/animations/rgb_matrix_effects.inc, not assumed -- and not something this
// project patches, consistent with "Core patches: NONE" throughout, see HANDOFF.md), then every
// RGB_MATRIX_EFFECT() registered here, in the exact order they're declared below -- that's the
// order `[`/`]`/tap-\ step through with rgb_matrix_step_noeeprom() (features/rgb_keys.c/rgb_
// control.c). Fourth pass (person's request): "sort them from Static modes first, transitioning
// to the more complex/unusual animated modes, with my Custom Mode placed at the very end" -- the
// stock portion of that ordering is inherited from QMK's own upstream list (SOLID_COLOR first,
// generally trending toward more dynamic effects further down -- not something this project
// reorders, for the reason above); the portion actually under this project's control is the
// custom effects below, ordered simple -> complex, reactive_energy (Custom Mode) last.
//
// Fifth pass (person's request: "sort modes by complexity in ascending order, from the simplest
// (static solid color) to the most unique and custom ones at the end") -- regrouped by what kind of
// thing each mode actually is, rather than by when it was written:
//   Ambient (never reacts to a keypress at all -- the same character as QMK's own stock rainbow/
//   cycle effects that precede this block in the list):
//   1. rainbow_wave_rtl -- a continuous, stateless hue sweep: the simplest thing here.
//   2. fairy_orb         -- a single light wandering on its own with a short trail. Stateful, but
//      since the fifth pass it no longer reacts to keypresses either, so it belongs with the
//      ambient modes rather than after the reactive ones where it used to sit.
//   Reactive (one simple visual response per keypress, dark or dimmed at rest):
//   3. darkening_glow   -- board sits lit, only *dims* around a press: closest to "static plus a
//      light reactive touch".
//   4. row_wave          -- one band along the pressed key's own row.
//   5. ripple_pool       -- a ring spreading across the whole board from the key.
//   6. comet_trail       -- adds directional travel and a trailing tail on top of a reactive trigger.
//   Everything:
//   7. reactive_energy   -- Custom Mode: every special-key animation, the numbered color slots with
//      their own per-animation speeds, idle simulation's richest behavior -- the most feature-rich
//      mode here, placed last as asked.
//
// One limit on "from the simplest": QMK's own stock effects always sort ahead of every custom effect
// (see above), and their relative order is fixed by core. The stock list already runs Solid Color ->
// gradients -> Breathing -> cycling/rainbow effects -> raindrops/pendulum, and only ends with two
// *reactive* stock effects (Typing Heatmap, Solid Multisplash) -- those two are therefore ahead of
// this project's own ambient modes in the cycle rather than after them. Reordering that would mean
// patching core (this project doesn't), or disabling them outright (not what was asked).
//
// (A briefly-existing equalizer mode was removed in the fourth pass's own follow-up round at the
// person's request, along with its RGBCTL_TARGET_EQUALIZER/EQ_* state -- see rgb_effects.h.)
//
// rainbow_wave_rtl: see rgb_effects.h's comment on rainbow_wave_rtl_render() for why it exists (the
// genuine right-to-left companion to stock Cycle Left/Right, not reachable as a config.h toggle).
// Like stock effects of its kind, its whole hue range is the point, so it has no accent-color slot
// of its own (falls through to Base in target_for_slot(), rgb_control.c).
// Sixth pass: Rainbow Right (stock Cycle Left/Right, re-registered) and Rainbow Left sit next to each other,
// first among the custom effects -- see config.h's note on ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT.
RGB_MATRIX_EFFECT(rainbow_wave_ltr)
RGB_MATRIX_EFFECT(rainbow_wave_rtl)
RGB_MATRIX_EFFECT(fairy_orb)
RGB_MATRIX_EFFECT(darkening_glow)
RGB_MATRIX_EFFECT(row_wave)
RGB_MATRIX_EFFECT(ripple_pool)
RGB_MATRIX_EFFECT(comet_trail)
RGB_MATRIX_EFFECT(reactive_energy)
#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#include "../../../features/rgb_effects.h"

bool reactive_energy(effect_params_t* params) {
    return reactive_energy_render(params);
}

bool rainbow_wave_ltr(effect_params_t* params) {
    return rainbow_wave_ltr_render(params);
}

bool row_wave(effect_params_t* params) {
    return row_wave_render(params);
}

bool darkening_glow(effect_params_t* params) {
    return darkening_glow_render(params);
}

bool fairy_orb(effect_params_t* params) {
    return fairy_orb_render(params);
}

bool ripple_pool(effect_params_t* params) {
    return ripple_pool_render(params);
}

bool comet_trail(effect_params_t* params) {
    return comet_trail_render(params);
}

bool rainbow_wave_rtl(effect_params_t* params) {
    return rainbow_wave_rtl_render(params);
}

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS


