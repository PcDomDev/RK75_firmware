// Copyright 2026 Saurabh Nakkarike (@snakkarike)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "quantum.h"

#include "../../../features/defines.h"
#include "../../../features/indicator_queue.h"
#include "../../../features/tap_hold.h"
#include "../../../features/indicators.h"
#include "../../../features/rgb_keys.h"
#include "../../../features/socd_cleaner.h"
#include "../../../features/custom_rgb.h" // Stage 6: Enter explosion / Backspace eraser triggers
                                            // only -- STAGE_STRUCTURE.md's Stage 6 entry is
                                            // explicit that this event-capture line is the one
                                            // documented exception to keeping RGB-effect logic
                                            // out of keymap.c.
#include "../../../features/rgb_control.h" // Stage 9: same exception, for Fn+\ (sec 3) and the
                                             // matrix_scan_user hold-poll below.

// Stage 9 (FINAL_SPECIFICATION.md sec 3.1): polls for the Fn+\ 180ms hold threshold. Has to be
// a periodic poll rather than a one-shot deferred callback, because rgbctl_task() needs to see
// "still held, threshold now reached" as it happens, not just resolve everything at release.
void matrix_scan_user(void) {
    rgbctl_task();
    glow_track_real_input(); // spec update sec 5: keeps the idle timer accurate regardless of
                               // which RGB mode is active -- see that function's own comment.
    glow_idle_simulation_tick(); // fourth pass: the mode-independent idle-simulation scheduler --
                                   // same "call every scan regardless of active mode" convention
                                   // as glow_track_real_input() just above, and for the same
                                   // reason (custom_rgb.h's own comment on this function).
}

void housekeeping_task_user(void) {
    // Bugfix (continuation pass): this used to also fire on IS_LAYER_ON(3) (Options), left over
    // from before Stage 1 renumbered what's now Options/Mac from layers 2/3 to 3/4 -- Options is
    // a momentary layer (held via Fn+RShift to reach its reset/SnapTap/etc toggles) that has
    // nothing to do with Mac mode, so it should not flicker this LED. rgbctl_is_mac_mode()
    // (features/rgb_control.h) is now the one place "is Mac mode active" is decided -- the
    // Windows/Mac shortcut-feedback feature (sec 9) reads the exact same function, so this LED
    // and that feature can no longer disagree with each other about what mode the board is in.
    if (rgbctl_is_mac_mode()) {
        gpio_write_pin_low(LED_MAC_PIN);    // low means turn on
    } else {
        gpio_write_pin_high(LED_MAC_PIN); // high means turn off
    }

    // Win-Lock LED reflects keymap_config.no_gui (toggled via Fn+LWin / RGB_GUI_TOGG), not layer
    // state. ansi.c's housekeeping_task_kb() already writes this same pin from the same field;
    // this just has to stop overwriting that write with an unrelated layer condition afterward.
    if (keymap_config.no_gui) {
        gpio_write_pin_low(LED_WIN_LOCK_PIN);
    } else {
        gpio_write_pin_high(LED_WIN_LOCK_PIN);
    }
}


// *************
// * SOCD *
// *************
socd_cleaner_t socd_v = {{KC_W, KC_S}, SOCD_CLEANER_LAST};
socd_cleaner_t socd_h = {{KC_A, KC_D}, SOCD_CLEANER_LAST};

// *************
// * Tap Dance *
// *************
enum tap_dance_keys {
    TD_RESET, // require 3 taps to reset board
    TD_CLEAR, // require 3 taps to clear eeprom
    TD_CTL_TG // require double tap to enable ctl layer
};

// *****************************
// * Custom processing of keys *
// *****************************
enum custom_keycodes {
    SOCDON = SAFE_RANGE,
    SOCDOFF,
    SOCDTOG,
    SWITCH_MODE,
    SIGNAL_MODE,
    RGB_GUI_TOGG,
    // Stage 9 (FINAL_SPECIFICATION.md sec 3): Layer 2 RGB control. RGBCTL_HOLD replaces Layer
    // 1's KC_NO at the \ position (reserved for this since Stage 1); everything else only ever
    // appears in Layer 2's own LAYOUT() below, so process_record_user's handling of them can't
    // fire from any other layer.
    RGBCTL_HOLD,
    RGBCTL_HUE_DOWN, RGBCTL_HUE_UP,
    RGBCTL_SAT_DOWN, RGBCTL_SAT_UP,
    RGBCTL_VAL_DOWN, RGBCTL_VAL_UP,
    RGBCTL_SPEED_DOWN, RGBCTL_SPEED_UP,
    RGBCTL_EFFECT_PREV, RGBCTL_EFFECT_NEXT,
    RGBCTL_SOLID_MODE,
    RGBCTL_TOGGLE,
    RGBCTL_PROFILE_1, RGBCTL_PROFILE_2, RGBCTL_PROFILE_3, RGBCTL_PROFILE_4,
    // R75 spec round (2026-09) sec 29: Home cycles to the next profile in sequence, a second way
    // to reach the same four profiles above without needing to remember which F-key is which.
    RGBCTL_PROFILE_CYCLE,
    // R75 spec round sec 32: one keycode per color slot (physical number row). Spec update
    // (2026-09) sec 6/7/25: Caps Lock and the Fn/RGB indicator color moved off the number row
    // entirely onto their own dedicated keys (RGBCTL_SELECT_CAPS/RGBCTL_SELECT_FNVIZ below).
    // Fourth pass (2026-09): reactive_energy -- the only mode that ever exposes more than one
    // numbered slot -- now uses 6 (Base/Enter/Backspace/Delete/Space/Esc, see rgb_
    // control.h's RGBCTL_TARGET_ESC; sixth pass removed the encoder wave slot), so slots 7-9 and 0 don't exist; those physical keys
    // stay plain KC_NO on Layer 2 (sec 33: no controls kept just because they used to exist).
    RGBCTL_SLOT_1, RGBCTL_SLOT_2, RGBCTL_SLOT_3, RGBCTL_SLOT_4, RGBCTL_SLOT_5, RGBCTL_SLOT_6,
    // R75 spec round sec 38/42: Esc's own Layer-2 binding -- not a real HID keycode (Esc's usual
    // job, triggering the Escape-wave animation, only applies to genuine KC_ESC on Layer 0/1; see
    // process_record_user's dedicated physical-position block for why this needs press *and*
    // release tracking the same way RGBCTL_HOLD/FN_OPTIONS_LYR do, rather than a plain switch
    // case).
    RGBCTL_RESET_ESC,
    // Spec update (2026-09) sec 6/7/25/27/32: Caps Lock (its own key) and ~/Alt/Enter (three
    // redundant keys, see rgb_control.h) select RGBCTL_TARGET_CAPS/_FNVIZ as the current color
    // target on Layer 2. RGBCTL_SELECT_CAPS is Caps Lock's *Layer-2* binding specifically --
    // RGBCTL_CAPS_ENABLE_TOGGLE above is unrelated, kept for any other caller that just wants a
    // plain on/off toggle without going through target selection.
    RGBCTL_SELECT_CAPS,
    RGBCTL_SELECT_FNVIZ,
    // Safety net added alongside the RAlt/Mac-layer fix above (2026-09, third pass): Fn+M now
    // toggles Mac mode on/off from the same key, rather than only ever turning it on (TO(4)
    // unconditionally jumps to layer 4 regardless of current state, so pressing it again while
    // already there was a no-op). Gives a direct, single-combo way back to Base that doesn't
    // depend on Fn+RAlt/the Options layer at all -- redundant with that fix on purpose, so a
    // future layer remapping some *other* physical key can't strand anyone the same way again.
    RGBCTL_MAC_TOGGLE,
    // Stage 11 (sec 8): Fn+Enter DFU hold. Layer 1 only (replaces the KC_NO reserved there
    // since Stage 1), same "not reachable from any other layer" reasoning as RGBCTL_HOLD above.
    RGBCTL_DFU_HOLD,
    // Continuation pass (sec 14): replaces Layer 1's raw MO(3) at the RShift position. A bare
    // MO() placed on a non-base layer is a known QMK footgun -- QMK resolves a held key's
    // identity fresh at every event from current layer state, so if Fn is released before
    // RShift, RShift's release no longer resolves to MO(3) (Layer 3's own entry there is
    // transparent, falling through to base KC_RSFT) and layer_off(3) never fires, leaving
    // Options stuck active until RShift happens to be pressed again some other time. This
    // keycode instead only ever *fires* the initial layer_on(3) on press (see
    // process_record_user); the matching layer_off(3) is guaranteed by tracking this key's own
    // physical down/up state (row 4, col 11) directly, the same way this file already
    // disambiguates the encoder's physical position from KC_MUTE elsewhere.
    FN_OPTIONS_LYR,
};

// clang-format off
tap_dance_action_t tap_dance_actions[] = {
    [TD_RESET]  = ACTION_TAP_DANCE_FN(safe_reset),
    [TD_CLEAR]  = ACTION_TAP_DANCE_FN(safe_clear),
    [TD_CTL_TG] = ACTION_TAP_DANCE_LAYER_TOGGLE(KC_RCTL, 3) // renumbered Stage 1: Options layer 2 -> 3
};
// clang-format on

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [0] = LAYOUT( /* Base Layer */
        KC_ESC,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   KC_DEL,   KC_MPLY,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,  KC_WHOM,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,  KC_PGUP,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_ENT,   KC_PGDN,
        KC_LSFT,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,  KC_RSFT,            KC_UP,
        KC_LCTL,  KC_LCMD,  KC_LALT,                      KC_SPC,                                 KC_RALT,  MO(1),              KC_LEFT,  KC_DOWN,  KC_RGHT
        ),

    // Fn Layer -- continuation pass sec 11/12/14: every position not in sec 11's exact list is
    // KC_NO (never _______), so an "unused" Fn combo can never leak through to its base-layer
    // action (sec 12) -- with the one necessary exception of this table's own [5][10] entry
    // (this layer's own Fn key), which must stay _______ so its release keeps resolving to
    // base's MO(1) consistently; see FN_OPTIONS_LYR's declaration above for why that matters.
    // Esc (Cleaning Mode, sec 12A) is handled by physical position in process_record_user, not
    // through this table at all -- see there.
    [1] = LAYOUT(
        KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_MUTE,  KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_PSCR,  KC_NO,
        KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_CALC,  KC_SLEP,
        KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    RGBCTL_HOLD,  KC_NO,
        KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,              RGBCTL_DFU_HOLD,    KC_NO,
        // Spec update (2026-09) sec 8/10: M = direct Mac-mode switch (same TO(4) the Options
        // layer's own F2 already uses -- see rgb_control.h's rgbctl_is_mac_mode(), which reads
        // layer state directly and doesn't care how that layer was reached); RShift (index 11,
        // previously FN_OPTIONS_LYR) is now plain KC_NO -- see this table's own FN_OPTIONS_LYR
        // comment above for where that moved to.
        KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    RGBCTL_MAC_TOGGLE,    KC_NO,    KC_NO,    KC_NO,    KC_NO,              KC_NO,
        KC_NO,    RGB_GUI_TOGG,  KC_NO,                      KC_NO,                                  FN_OPTIONS_LYR,    _______,              KC_NO,    KC_NO,    KC_NO
        ),

    // Stage 9 (FINAL_SPECIFICATION.md sec 3.2): everything not listed is KC_NO -- deliberately
    // NOT _______ (transparent), unlike every other layer here, so a stray key while adjusting
    // lighting can't fall through to an unrelated Layer-1 shortcut. Reachable only by holding
    // \ (RGBCTL_HOLD, Layer 1) past the 180ms threshold -- see features/rgb_control.c.
    // R75 spec round (2026-09) sec 27-42: the RGB Control layer's contents were substantially
    // redesigned around this spec round's exact key list (sec 28-34) and visual-language rules
    // (sec 35-41) -- see features/indicators.c's render_rgbctl_layer() for how each of these is
    // actually drawn, and features/rgb_control.c for the slot/profile/reset-gesture logic behind
    // them. Everything not listed below is deliberately KC_NO (see the note above this array),
    // same reasoning as before: a stray keypress while adjusting lighting must never fall through
    // to an unrelated action.
    [2] = LAYOUT( /* RGB Control Layer */
        RGBCTL_RESET_ESC,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  RGBCTL_PROFILE_1,  RGBCTL_PROFILE_2,  RGBCTL_PROFILE_3,  RGBCTL_PROFILE_4,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,
        RGBCTL_SELECT_FNVIZ,    RGBCTL_SLOT_1,  RGBCTL_SLOT_2,  RGBCTL_SLOT_3,  RGBCTL_SLOT_4,  RGBCTL_SLOT_5,  RGBCTL_SLOT_6,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  RGBCTL_HUE_DOWN,  RGBCTL_HUE_UP,  KC_NO,  RGBCTL_PROFILE_CYCLE,
        KC_NO,    KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  RGBCTL_EFFECT_PREV,  RGBCTL_EFFECT_NEXT,  KC_NO,    KC_NO,
        RGBCTL_SELECT_CAPS,    KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,  KC_NO,    KC_NO,  KC_NO,            KC_NO,    KC_NO,
        KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    KC_NO,    RGBCTL_SAT_DOWN,  RGBCTL_SAT_UP,  KC_NO,  KC_NO,              RGBCTL_VAL_UP,
        KC_NO,    KC_NO,    KC_NO,        RGBCTL_TOGGLE,                          KC_NO,    KC_NO,              RGBCTL_SPEED_DOWN,  RGBCTL_VAL_DOWN,  RGBCTL_SPEED_UP
        ),

    [3] = LAYOUT( /* Options Layer */
        _______,  TO(0),    TO(4),    TO(5),   _______,  _______,    _______,    _______,    _______,    _______,    _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  TD_KB_RST,  _______,  _______,  _______,  SOCDTOG,  _______,  _______,  _______,  SWITCH_MODE,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  SIGNAL_MODE,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,         _______,  _______,
        _______,  TD_KB_CLR,  _______,  _______,  _______,  _______,  NK_TOGG,  _______,  _______,  _______,  _______,  _______,           _______,
        _______,  _______,  _______,                      _______,                                 _______,  _______,            _______,  _______,  _______
    ),

    [4] = LAYOUT( /* Mac Layer */
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,
        _______,  KC_LALT,  KC_LGUI,                      _______,                                 KC_RGUI,  _______,            _______,  _______,  _______
        ),


    [5] = LAYOUT( /* Num Layer */
        _______,  TO(0),    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  KC_NUM,   KC_P7,  KC_P8,  KC_P9,  KC_PAST,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  XXXXXXX,  KC_P4,  KC_P5,  KC_P6,  KC_PMNS,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  XXXXXXX,  KC_P1,  KC_P2,  KC_P3,  KC_PPLS,  _______,            _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  XXXXXXX,  KC_P0,  KC_PDOT,  KC_PSLS,  KC_PENT,  _______,            _______,
        _______,  _______,  _______,                      _______,                                 _______,  _______,            _______,  _______,  _______
        ),
};

#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},  // Volume control on layer 0
    [1] = {ENCODER_CCW_CW(KC_MRWD, KC_MFFD)},  // No action on layer 1
    // Stage 9 (FINAL_SPECIFICATION.md sec 3.2): "Encoder, rotate CW/CCW | Hue +/-".
    [2] = {ENCODER_CCW_CW(RGBCTL_HUE_DOWN, RGBCTL_HUE_UP)},
    [3] = {ENCODER_CCW_CW(_______, _______)},  // trans, renumbered from [2] (was Options)
    [4] = {ENCODER_CCW_CW(_______, _______)},  // trans, renumbered from [3] (was Mac)
    [5] = {ENCODER_CCW_CW(_______, _______)},  // trans, renumbered from [4] (was Num)
};
#endif
// clang-format on

// This board's g_led_config is generated by QMK's build system from ansi/keyboard.json's
// "rgb_matrix.layout" (data-driven keyboards.json convention), not declared by hand here. See
// HANDOFF.md for the full derived LED-index <-> matrix <-> physical-xy map this project's RGB
// code (features/custom_rgb.c, features/rgb_effects.c) depends on.

// Bugfix (continuation pass): these were previously only ever `extern`-declared (also in
// features/indicators.c) with no definition anywhere in the project, which cannot link. This is
// their one real definition; indicators.c keeps its extern declaration and links against this.
uint8_t is_orgb_mode = 0;
uint8_t is_srgb_mode = 0;


// Cleaning / Input Lock Mode (sec 12A). While active, every key below is fed to this instead of
// its normal handling, so the board's few "press a key, see a dedicated animation" effects still
// play (as opposed to *only* relying on the always-on generic reactive glow, which needs no help
// from here at all -- see custom_rgb.h's header comment on why that keeps working automatically
// regardless of what process_record_user returns). F-key groups are intentionally still not
// included -- Enter/Backspace/Delete/Space/Esc/encoder are this board's visually largest effects,
// and keeping this list short keeps it easy to audit that nothing here does anything beyond
// calling a glow_trigger_*/rgb_matrix-visual function (never register_code, never a layer/EEPROM/
// settings change) -- see HANDOFF.md. R75 spec round (2026-09) sec 5.3 added the encoder (press/
// CW/CCW) to this list -- previously the disabled-key mode also silently disabled encoder visual
// feedback, which the spec calls out directly as something that must not happen.
static void cleaning_mode_visual_passthrough(uint16_t keycode, keyrecord_t *record) {
    uint8_t row = record->event.key.row, col = record->event.key.col;
    if (row == 3 && col == 13) { // Enter
        if (record->event.pressed) glow_trigger_enter_explosion();
    } else if (row == 1 && col == 13) { // Backspace
        if (record->event.pressed) glow_trigger_backspace_eraser();
    } else if (row == 0 && col == 13) { // Delete
        if (record->event.pressed) glow_trigger_delete_vacuum();
    } else if (row == 5 && col == 5) { // Space
        if (record->event.pressed) glow_trigger_space_wave();
    } else if (row == 0 && col == 0) { // Esc (bare -- Fn+Esc is the toggle itself, handled
                                          // separately before this is ever reached)
        if (record->event.pressed) glow_trigger_escape_sequence();
    } else if (row == 0 && col >= 1 && col <= 12) { // F1-F12
        // Spec update (2026-09) sec 12: F-row reactive behavior (the group-of-4 cascade flash,
        // same as normal typing -- see glow_trigger_fkey_group()) previously wasn't in this list
        // at all, so it silently did nothing while Cleaning Mode was active; the only way an
        // F-row key ever visibly reacted was as a side effect of an Escape wave (above) happening
        // to sweep across it, which the spec calls out directly as the wrong mechanism to rely on.
        // F1-F12 occupy columns 1-12 of matrix row 0 (Esc=0, Delete=13, encoder=14) -- see
        // keyboard.json's rgb_matrix.layout, cross-referenced against this file's own LAYOUT()
        // call, same method used throughout this project.
        static const uint8_t fkey_led[12] = {20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9}; // F1..F12
        if (record->event.pressed) glow_trigger_fkey_group(fkey_led[col - 1]);
    }
    // Sixth pass: the encoder's visual ring was removed, so there is nothing left to trigger for
    // encoder events here.
}

// Continuation pass (sec 14): tracks whether FN_OPTIONS_LYR (RAlt, Layer 1) is the reason
// Layer 3 is currently on, so its release can be caught by physical position even if Fn was
// released first and the position no longer resolves to this keycode -- see its enum
// declaration and process_record_user's row/col check below for the full explanation.
//
// Spec update (2026-09) sec 8: relocated from RShift to RAlt -- "remove all existing functions
// assigned to Fn+Shift" removes RShift's role here specifically (it was the only non-KC_NO
// Shift-related Layer 1 binding), but the Options layer's own 8 functions (reset, EEPROM clear,
// SOCD/NKRO/OpenRGB/SignalRGB toggles, Base/Mac/Num layer switching) aren't mentioned anywhere as
// things to remove, so they needed a new physical way in rather than becoming unreachable --
// preserving existing functionality unless explicitly changed takes priority here. RAlt was free
// on Layer 1 (plain KC_NO) and isn't used for anything else Fn-held.
static bool options_layer_via_fn_rshift = false;

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Checked first, unconditionally, by physical position rather than keycode -- Layer 1
    // remaps most of the keyboard, but Fn+Esc has to toggle the same way no matter what else is
    // going on, the same reasoning as FN_OPTIONS_LYR/RGBCTL_HOLD's position-tracking below.
    // R75 spec round (2026-09) sec 38/42: excludes Layer 2 (RGB Control) specifically -- while
    // that layer is active, Esc's own position is repurposed as part of the destructive-reset
    // gesture (RGBCTL_RESET_ESC, handled in its own physical-position block below) instead of the
    // Cleaning Mode toggle, so pressing it mid-adjustment doesn't unexpectedly exit to a different
    // mode entirely.
    if (record->event.key.row == 0 && record->event.key.col == 0 && IS_LAYER_ON(1) && !IS_LAYER_ON(2)) {
        if (record->event.pressed) {
            rgbctl_cleaning_mode_toggle();
        }
        return false;
    }
    if (rgbctl_cleaning_mode_active()) {
        // Fn itself must keep working (base layer's own MO(1)), purely so Layer 1 -- and
        // therefore the Fn+Esc combo above -- stays reachable to toggle back off.
        if (record->event.key.row == 5 && record->event.key.col == 10) {
            return true;
        }
        cleaning_mode_visual_passthrough(keycode, record);
        return false; // no SOCD, no stock RGB keycodes, no keymap action, no layer change, no
                       // EEPROM/profile write -- nothing below this line runs.
    }

    // Robustness for Layer 1's two "hold this to reach a higher layer" keys (FN_OPTIONS_LYR at
    // RAlt, RGBCTL_HOLD at \): QMK resolves a held key's identity fresh at every event from
    // whatever layer is active *right then*. If Fn is released before the other key, that key's
    // release no longer resolves to the same keycode it pressed as (Layer 3/2's own entries at
    // these exact positions are transparent/KC_NO, not FN_OPTIONS_LYR/RGBCTL_HOLD), so the
    // matching layer_off() would otherwise never fire and the layer would stay stuck on --
    // checking by physical position instead of by keycode catches that release regardless of
    // what it currently nominally resolves to. See FN_OPTIONS_LYR's enum declaration above and
    // rgb_control.h's rgbctl_hold_layer_is_active() for the full explanation.
    //
    // Real bug found and fixed (2026-09, third pass): the PRESS side used to gate on
    // `keycode == FN_OPTIONS_LYR` too, which silently broke the instant *any other currently-
    // active layer numbered higher than Layer 1* had its own non-transparent binding at this same
    // physical position -- exactly what happens on the Mac layer (Layer 4), whose row5 remaps
    // RAlt to KC_RGUI (part of the Alt/Cmd swap, unrelated to this). Since layer resolution checks
    // the highest-numbered active layer first, holding Fn+RAlt while on the Mac layer resolved to
    // KC_RGUI, never FN_OPTIONS_LYR -- making the Options layer, and therefore its F1 "back to
    // Base" key, completely unreachable from Mac mode. There was no key-based way back at all.
    // Fixed by checking IS_LAYER_ON(1) directly instead of the resolved keycode -- Fn+RAlt must
    // always mean "enter the Options layer" regardless of what any other active layer would
    // otherwise have resolved bare RAlt to, the same way the *release* check just below already
    // didn't depend on keycode for exactly this reason.
    if (record->event.key.row == 5 && record->event.key.col == 4) { // RAlt
        if (record->event.pressed && IS_LAYER_ON(1) && !options_layer_via_fn_rshift) {
            options_layer_via_fn_rshift = true;
            layer_on(3);
            return false;
        }
        if (!record->event.pressed && options_layer_via_fn_rshift) {
            options_layer_via_fn_rshift = false;
            layer_off(3);
            return false;
        }
        // else: a normal, unrelated KC_RALT press/release on some other layer -- fall through.
    }
    if (record->event.key.row == 2 && record->event.key.col == 13) { // \ (backslash)
        if (keycode == RGBCTL_HOLD) {
            rgbctl_hold_key_event(record->event.pressed);
            return false;
        }
        if (!record->event.pressed && rgbctl_hold_layer_is_active()) {
            rgbctl_hold_key_event(false);
            return false;
        }
        // else: a normal, unrelated KC_BSLS press/release on some other layer -- fall through.
    }

    // R75 spec round (2026-09) sec 38/42: "Esc + the currently selected profile", held --
    // rgbctl_reset_key_event() needs reliable press *and* release tracking for both keys
    // involved, the same reasoning (and the same physical-position approach) as RGBCTL_HOLD/
    // FN_OPTIONS_LYR above. Only meaningful while Layer 2 is active; harmless no-op otherwise
    // (rgbctl_reset_key_event() only ever starts a countdown while both a tracked Esc-down and a
    // tracked profile-key-down are true at once, and Layer 2's own KC_NO/RGBCTL_PROFILE_1..4
    // bindings are the only way either keycode can arrive here in the first place).
    if (keycode == RGBCTL_RESET_ESC) {
        rgbctl_reset_key_event(true, 0, record->event.pressed);
        return false;
    }
    if (keycode == RGBCTL_PROFILE_1 || keycode == RGBCTL_PROFILE_2 || keycode == RGBCTL_PROFILE_3 || keycode == RGBCTL_PROFILE_4) {
        uint8_t index = keycode - RGBCTL_PROFILE_1;
        rgbctl_reset_key_event(false, index, record->event.pressed);
        if (record->event.pressed) {
            rgbctl_load_profile(index); // tap-to-load (sec 29) -- also what "selects" a profile
                                          // for the hold-to-reset gesture above, see rgb_control.h
        }
        return false;
    }

    if (!process_socd_cleaner(keycode, record, &socd_v)) {
        return false;
    }
    if (!process_socd_cleaner(keycode, record, &socd_h)) {
        return false;
    }
    if (!process_rgb_keys(keycode, record)) {
        return false;
    }
    // Spec update (2026-09) sec 19: the Windows/Mac Ctrl+letter shortcut-feedback feature
    // (rgbctl_shortcut_feedback(), rgb_control.c) is removed -- it only ever covered a single
    // modifier + one letter, and the spec explicitly doesn't want it extended to cover more
    // combinations, just removed. RGBCTL_TARGET_SHORTCUT and its EEPROM storage are removed to
    // match (see rgb_control.h/rgb_profiles.h) rather than left as orphaned dead state.
    if (!record->event.pressed && (keycode == KC_LSFT || keycode == KC_RSFT)) {
        // Spec update sec 4: starts a fresh release-fade for both Shift LEDs the instant either
        // is let go, regardless of how long it was held -- see rgb_effects.c's
        // overlay_shift_held() for the live, hold-duration-aware rendering this hands off from.
        // Deliberately falls through afterward (no `return`) -- KC_LSFT/KC_RSFT must still
        // process completely normally as real modifier keys.
        glow_trigger_shift_release();
    }

    switch (keycode) {
case SWITCH_MODE:
#ifdef OPENRGB_ENABLE
    if (record->event.pressed) {
        // Toggle the OpenRGB mode and disable SignalRGB
        is_orgb_mode = !is_orgb_mode;
        if (is_orgb_mode) {
            is_srgb_mode = false;  // Disable SignalRGB mode
            indicator_enqueue(40, 200, 3, 0x00, 0xFF, 0xFF); // Blink O Cyan
        } else {
            indicator_enqueue(40, 200, 3, 0xFF, 0x00, 0xFF); // Blink O Magenta (returning to VIA)
        }
    }
    // If both OpenRGB and SignalRGB are off, use the solid color mode
    if (!is_orgb_mode && !is_srgb_mode) {
        rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);  // Set solid color mode
        rgb_matrix_sethsv_noeeprom(RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, RGB_MATRIX_DEFAULT_VAL);  // Set default HSV
    }
#endif
    return false;

case SIGNAL_MODE:
#ifdef SIGNALRGB_SUPPORT_ENABLE
    if (record->event.pressed) {
        // Toggle the SignalRGB mode and disable OpenRGB
        is_srgb_mode = !is_srgb_mode;
        if (is_srgb_mode) {
            is_orgb_mode = false;  // Disable OpenRGB mode
            indicator_enqueue(52, 200, 3, 0x00, 0xFF, 0x00); // Blink S Green
        } else {
            indicator_enqueue(52, 200, 3, 0xFF, 0x00, 0xFF); // Blink S Magenta (returning to VIA)
        }
    }
    // If both OpenRGB and SignalRGB are off, use the solid color mode
    if (!is_orgb_mode && !is_srgb_mode) {
        rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);  // Set solid color mode
        rgb_matrix_sethsv_noeeprom(RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, RGB_MATRIX_DEFAULT_VAL);  // Set default HSV
    }
#endif
    return false;

case RGB_GUI_TOGG:
    if (record->event.pressed) {
        keymap_config.no_gui = !keymap_config.no_gui;
    }
    return false;



        // case SIGNAL_MODE:  // Replace SWITCH_MODE with your custom key
        //     if (record->event.pressed) {
        //         if (!is_orgb_mode) {  // Check if OpenRGB mode is off
        //             is_signalrgb_active = !is_signalrgb_active;  // Toggle SignalRGB state
        //             if (is_signalrgb_active) {
        //                 signalrgb_mode_enable();  // Enable SignalRGB if active
        //             } else {
        //                 signalrgb_mode_disable();  // Disable SignalRGB if inactive
        //             }
        //         }
        //     }
        //     return false;

        case QK_MAGIC_TOGGLE_NKRO:
            if (record->event.pressed) {
                clear_keyboard(); // clear first buffer to prevent stuck keys
                wait_ms(50);
                keymap_config.nkro = !keymap_config.nkro;
                blink_NKRO(keymap_config.nkro);
                wait_ms(50);
                clear_keyboard(); // clear first buffer to prevent stuck keys
                wait_ms(50);
            }
            return false;
        case SOCDON: // Turn SOCD Cleaner on.
            if (record->event.pressed) {
                socd_cleaner_enabled = true;
            }
            return false;
        case SOCDOFF: // Turn SOCD Cleaner off.
            if (record->event.pressed) {
                socd_cleaner_enabled = false;
            }
            return false;
        case SOCDTOG: // Toggle SOCD Cleaner.
            if (record->event.pressed) {
                socd_cleaner_enabled = !socd_cleaner_enabled;
            }
            return false;
        // Continuation pass (sec 2-4/20): trigger only, on press. Both are real keycodes that
        // must still be sent normally -- return true, don't swallow the keypress. Enter/
        // Backspace/Delete/Space now each run from their own independent small pool
        // (features/custom_rgb.h) rather than a single shared preempting slot -- a repeat press
        // of any of them, or of any of the others, always starts an additional independent wave
        // instead of resetting or cancelling whatever's already travelling.
        case KC_ENT:
            if (record->event.pressed) {
                glow_trigger_enter_explosion();
            }
            return true;
        case KC_BSPC:
            if (record->event.pressed) {
                glow_trigger_backspace_eraser();
            }
            return true;
        case KC_DEL:
            if (record->event.pressed) {
                glow_trigger_delete_vacuum();
            }
            return true;
        // Stage 7 (sec 5.3, 5.8-5.10): same trigger-only pattern as above.
        case KC_SPC:
            if (record->event.pressed) {
                glow_trigger_space_wave();
            }
            return true;
        case KC_ESC:
            if (record->event.pressed) {
                glow_trigger_escape_sequence();
            }
            return true;
        case KC_F1:
            if (record->event.pressed) { glow_trigger_fkey_group(20); }
            return true;
        case KC_F2:
            if (record->event.pressed) { glow_trigger_fkey_group(19); }
            return true;
        case KC_F3:
            if (record->event.pressed) { glow_trigger_fkey_group(18); }
            return true;
        case KC_F4:
            if (record->event.pressed) { glow_trigger_fkey_group(17); }
            return true;
        case KC_F5:
            if (record->event.pressed) { glow_trigger_fkey_group(16); }
            return true;
        case KC_F6:
            if (record->event.pressed) { glow_trigger_fkey_group(15); }
            return true;
        case KC_F7:
            if (record->event.pressed) { glow_trigger_fkey_group(14); }
            return true;
        case KC_F8:
            if (record->event.pressed) { glow_trigger_fkey_group(13); }
            return true;
        case KC_F9:
            if (record->event.pressed) { glow_trigger_fkey_group(12); }
            return true;
        case KC_F10:
            if (record->event.pressed) { glow_trigger_fkey_group(11); }
            return true;
        case KC_F11:
            if (record->event.pressed) { glow_trigger_fkey_group(10); }
            return true;
        case KC_F12:
            if (record->event.pressed) { glow_trigger_fkey_group(9); }
            return true;
        // Encoder rotation (sec 5.10). ENCODER_MAP_ENABLE dispatches rotation as a synthetic
        // ENCODER_CW_EVENT/ENCODER_CCW_EVENT keypress of whatever's assigned on the current
        // layer (KC_VOLU/KC_VOLD on layer 0) -- IS_ENCODEREVENT() distinguishes that from a real
        // key sharing the same keycode. Continuation pass: Layer 1 no longer binds either
        // keycode at all (sec 11/12 -- F9-F12 are now Mute/Prev/Play/Next), so there's no longer
        // an actual collision on this board, but the check is left in place as cheap, correct
        // defense against a future remap reintroducing one.
        // Sixth pass: the encoder wave (and its typing-proximity guard) is gone. A rotation tick is
        // still a real interaction for the idle timer, which the removed trigger used to record.
        // Sixth pass: Caps Lock LED latency -- tell the indicator code about the keypress right away
        // so it lights on this very frame instead of waiting for the host's lock-state report. (On the
        // RGB config layer Caps is RGBCTL_SELECT_CAPS, which returns before reaching here.)
        case KC_CAPS:
            if (record->event.pressed) { indicators_caps_key_pressed(); }
            return true;
        case KC_VOLU:
        case KC_VOLD:
            if (record->event.pressed && IS_ENCODEREVENT(record->event)) {
                glow_note_encoder_input();
            }
            return true;
        // Encoder press. KC_MPLY is also independently bound on the Fn layer (Fn+F11), so this
        // checks the encoder's own real matrix position (row 0, col 14) rather than the keycode alone.
        case KC_MPLY:
            if (record->event.pressed && record->event.key.row == 0 && record->event.key.col == 14) {
                if (!glow_encoder_press_debounce_ok()) {
                    return false; // contact bounce of the press just accepted -- drop it entirely
                }
                glow_note_encoder_input();
            }
            return true;
        // Spec update (2026-09) sec 7: Caps Lock's Layer-2 binding -- selects it as the current
        // color target (or, if already selected, toggles its indicator) -- see rgb_control.c.
        case RGBCTL_SELECT_CAPS:
            if (record->event.pressed) { rgbctl_select_caps_target(); }
            return false;
        // Spec update sec 25/27/32: ~, Alt, and Enter all select the Fn/RGB indicator color --
        // three keycodes bound to the same action, see rgb_control.h for why.
        case RGBCTL_SELECT_FNVIZ:
            if (record->event.pressed) { rgbctl_select_fnviz_target(); }
            return false;
        case RGBCTL_MAC_TOGGLE:
            if (record->event.pressed) {
                layer_move(rgbctl_is_mac_mode() ? 0 : 4);
            }
            return false;
        // Layer-2-only actions, one call each into features/rgb_control.c. None of these are
        // real HID keycodes -- return false. (RGBCTL_HOLD itself is handled above, by physical
        // position, before this switch is reached at all -- see there for why.)
        //
        // Spec update sec 23: routed through rgbctl_repeat_key_event() so holding the key
        // auto-repeats the adjustment instead of firing exactly once -- see rgb_control.h.
        case RGBCTL_HUE_DOWN:
            rgbctl_repeat_key_event(rgbctl_hue_down, record->event.pressed);
            return false;
        case RGBCTL_HUE_UP:
            rgbctl_repeat_key_event(rgbctl_hue_up, record->event.pressed);
            return false;
        case RGBCTL_SAT_DOWN:
            rgbctl_repeat_key_event(rgbctl_sat_down, record->event.pressed);
            return false;
        case RGBCTL_SAT_UP:
            rgbctl_repeat_key_event(rgbctl_sat_up, record->event.pressed);
            return false;
        case RGBCTL_VAL_DOWN:
            rgbctl_repeat_key_event(rgbctl_val_down, record->event.pressed);
            return false;
        case RGBCTL_VAL_UP:
            rgbctl_repeat_key_event(rgbctl_val_up, record->event.pressed);
            return false;
        case RGBCTL_SPEED_DOWN:
            rgbctl_repeat_key_event(rgbctl_speed_down, record->event.pressed);
            return false;
        case RGBCTL_SPEED_UP:
            rgbctl_repeat_key_event(rgbctl_speed_up, record->event.pressed);
            return false;
        case RGBCTL_EFFECT_PREV:
            if (record->event.pressed) { rgbctl_step_effect(false); }
            return false;
        case RGBCTL_EFFECT_NEXT:
            if (record->event.pressed) { rgbctl_step_effect(true); }
            return false;
        case RGBCTL_SOLID_MODE:
            if (record->event.pressed) { rgbctl_solid_mode(); }
            return false;
        case RGBCTL_TOGGLE:
            if (record->event.pressed) { rgbctl_toggle(); }
            return false;
        // RGBCTL_PROFILE_1..4's tap-to-load and reset-gesture down/up tracking are both handled
        // together, above, in the same physical-position block RGBCTL_HOLD/FN_OPTIONS_LYR use --
        // see there. RGBCTL_PROFILE_RESET no longer exists as its own tap-to-reset keycode (sec
        // 38's safety-delay requirement replaced it with that same hold gesture).
        case RGBCTL_PROFILE_CYCLE:
            if (record->event.pressed) { rgbctl_profile_cycle_next(); }
            return false;
        // R75 spec round (2026-09) sec 32; fourth pass: one case per color slot (1-6 -- see the
        // enum declaration above for why slots 7-9/0 don't exist). rgbctl_select_color_slot()
        // itself no-ops for a slot the active RGB mode doesn't expose, so no per-mode branching is
        // needed here.
        case RGBCTL_SLOT_1:
            if (record->event.pressed) { rgbctl_select_color_slot(1); }
            return false;
        case RGBCTL_SLOT_2:
            if (record->event.pressed) { rgbctl_select_color_slot(2); }
            return false;
        case RGBCTL_SLOT_3:
            if (record->event.pressed) { rgbctl_select_color_slot(3); }
            return false;
        case RGBCTL_SLOT_4:
            if (record->event.pressed) { rgbctl_select_color_slot(4); }
            return false;
        case RGBCTL_SLOT_5:
            if (record->event.pressed) { rgbctl_select_color_slot(5); }
            return false;
        case RGBCTL_SLOT_6:
            if (record->event.pressed) { rgbctl_select_color_slot(6); }
            return false;
        // Stage 11 (sec 8): needs both press and release, like RGBCTL_HOLD -- not a real HID
        // keycode, return false.
        case RGBCTL_DFU_HOLD:
            rgbctl_dfu_key_event(record->event.pressed);
            return false;
        default:
            return true;
    }

    return true;
}
