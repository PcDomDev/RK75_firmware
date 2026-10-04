#pragma once

#include QMK_KEYBOARD_H

void blink_arrows(void);
void blink_space(bool);
void blink_NKRO(bool);
// Call on every real Caps Lock keypress (keymap.c) so the Caps Lock LED lights instantly instead of
// waiting for the host's lock-state report -- see indicators.c.
void indicators_caps_key_pressed(void);
// blink_numbers()/highlight_fn_keys() removed (continuation pass): both were fully unreferenced
// (highlight_fn_keys()'s only call site was already commented out) -- see HANDOFF.md.
