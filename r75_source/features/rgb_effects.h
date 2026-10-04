#pragma once

#include QMK_KEYBOARD_H

// Render function for the custom "reactive_energy" RGB Matrix effect, registered via
// ansi/keymaps/via/rgb_matrix_user.inc.
//
// Stage 5 (FINAL_SPECIFICATION.md sec 5.1-5.2): the 24-slot reactive-glow event pool (see
// custom_rgb.h) renders here -- standard glow for letters/numbers/~/-/=, smaller-radius dimmer
// glow for modifiers, larger brighter glow for Tab. Stage 6 (sec 5.5-5.6) added the Enter
// explosion and Backspace eraser. Stage 7 (sec 5.3, 5.7-5.10) added Space's rings, Shift-sync,
// F-groups, the Escape sequence, and encoder waves. The Caps Lock indicator (sec 5.4) is in
// features/indicators.c instead, not here.
bool reactive_energy_render(effect_params_t *params);

// Stage 8 (sec 6.1, 6.3): generic level-bar renderers over an arbitrary LED range, immediately
// drawing whatever value is passed in -- no internal visibility/timing state. Both fill from
// `led_high` (the 0%/start end) toward lower indices, matching how this project consistently
// describes the Esc-Del strip ("indices 21 down to 8"); pass led_high=21, count=14 for that
// strip. The 900ms-hold-then-fade *visibility* timing (sec 6.2) is Stage 9's job to drive, once
// Layer 2 exists to actually trigger adjustments from -- these two functions are the reusable
// drawing primitives underneath that, not the state machine deciding when to call them. Also
// reused verbatim for Stage 11's DFU hold-progress (sec 6.3), which is why led_range is a
// parameter rather than hardcoded to the Esc-Del strip.

// Linear fill bar (Saturation/Value/Speed, sec 6.1): `percent` 0-255 (representing 0-100%) of
// `count` LEDs in `color`, itself scaled by `brightness_pct` (0-255) so a caller can drive the
// 900ms-hold-then-~200ms-fade visibility timing (sec 6.2) without this function needing to know
// about timing at all -- pass 255 while holding, a decreasing value while fading. The boundary
// LED's brightness is additionally scaled by the fractional remainder for a smooth fill rather
// than a hard step.
void draw_level_bar(uint8_t led_min, uint8_t led_max, uint8_t led_high, uint8_t count, uint8_t percent, RGB color, uint8_t brightness_pct);

// Rainbow-spectrum bar with a pointer (Hue, sec 6.1): all `count` LEDs permanently show the
// spectrum in order; the one closest to `current_hue` (circular distance -- hue wraps) is
// boosted to full brightness as the pointer, the rest sit at a dim baseline. `brightness_pct`
// scales the whole bar the same way as draw_level_bar's, for the same fade timing.
void draw_hue_bar(uint8_t led_min, uint8_t led_max, uint8_t led_high, uint8_t count, uint8_t current_hue, uint8_t brightness_pct);

// R75 spec round (2026-09) sec 23/24/25: three new standalone RGB Matrix effects, registered in
// ansi/keymaps/via/rgb_matrix_user.inc (and mirrored for iso/) the same way reactive_energy_render
// above is. Each owns its own rgb_matrix_mode entirely -- unlike Enter/Backspace/Delete/Space/
// Escape/encoder above, which are overlays that only ever render while reactive_energy is the
// active mode, these three fully replace the board's rendering while selected.
bool row_wave_render(effect_params_t *params);       // sec 23
bool darkening_glow_render(effect_params_t *params); // sec 24
bool fairy_orb_render(effect_params_t *params);       // sec 25

// Fourth pass (2026-09): three more new standalone RGB Matrix effects, same registration pattern
// as the three above (ansi/keymaps/via/rgb_matrix_user.inc, mirrored for iso/).
bool ripple_pool_render(effect_params_t *params);  // board-wide analogue of Row Wave
bool comet_trail_render(effect_params_t *params);  // fading comet launched from the pressed key

// Fourth pass follow-up: the stock CYCLE_LEFT_RIGHT effect only ever sweeps hue in one direction
// (its own quantum/rgb_matrix/animations/cycle_left_right_anim.h: hsv.h = x - time) -- reported
// as "2 identical rainbow wave animations going left to right" alongside RAINBOW_MOVING_CHEVRON
// (disabled in config.h this same pass: its own formula also has an (x - time) term at its core,
// plus a chevron-shaped offset from board center that reads as minor on this board's short row
// count, so the two looked practically the same here). A genuine mirror -- the same wave, the
// other direction -- isn't reachable by toggling stock ENABLE_RGB_MATRIX_* flags (no stock effect
// does x + time), and modifying the stock file itself would be a core patch, which this project
// avoids throughout; implemented as a small custom effect instead, following stock's own math
// (including its exact time/speed formula, replicated from quantum/rgb_matrix/animations/runners/
// effect_runner_i.h so it responds to the speed control identically) with the one sign flipped.
bool rainbow_wave_rtl_render(effect_params_t *params);
// Sixth pass: "Rainbow Right" -- the stock CYCLE_LEFT_RIGHT effect re-registered as a custom effect so it can
// be placed directly next to Rainbow Left (rainbow_wave_rtl) in the effect list. Identical visuals.
bool rainbow_wave_ltr_render(effect_params_t *params);
