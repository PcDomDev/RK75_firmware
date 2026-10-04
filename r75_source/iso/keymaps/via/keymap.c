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
#include "../../../features/custom_rgb.h" // glow_track_real_input() -- see matrix_scan_user below

void housekeeping_task_user(void) {
    // Note: We can decide what to do with the MAC Led in this function
    if (IS_LAYER_ON(2) || IS_LAYER_ON(3)) { // if the Ctl layer is active
        gpio_write_pin_low(LED_MAC_PIN);    // low means turn on
    } else {
        gpio_write_pin_high(LED_MAC_PIN); // high means turn off
    }

    if (IS_LAYER_ON(1) || IS_LAYER_ON(4)) { // Check if the layer 2 is active
        // Turn on the Win Lock LED when layer 2 is active
        gpio_write_pin_low(LED_WIN_LOCK_PIN);
    } else {
        // Turn off the Win Lock LED when not on layer 2
        gpio_write_pin_high(LED_WIN_LOCK_PIN);
    } // else we have enabled no_gui, skip re-using the LED
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
    SIGNAL_MODE
};

// clang-format off
tap_dance_action_t tap_dance_actions[] = {
    [TD_RESET]  = ACTION_TAP_DANCE_FN(safe_reset),
    [TD_CLEAR]  = ACTION_TAP_DANCE_FN(safe_clear),
    [TD_CTL_TG] = ACTION_TAP_DANCE_LAYER_TOGGLE(KC_RCTL, 2)
};
// clang-format on

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    [0] = LAYOUT( /* Base */
        KC_ESC,   KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   KC_DEL,   KC_MUTE,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,  KC_HOME,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_ENT,   KC_PGUP,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_BSLS,  KC_PGDN,
        KC_LSFT,  KC_NUBS,  KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,  KC_RSFT,  KC_UP,
        KC_LCTL,  KC_LCMD,  KC_LALT,                      KC_SPC,                                 KC_RALT,  MO(1),              KC_LEFT,  KC_DOWN,  KC_RGHT
        ),

    [1] = LAYOUT( /* Fn Layer */
        _______,  KC_MYCM,  KC_WHOM,  KC_MAIL,  KC_CALC,  KC_MSEL,  KC_MSTP,  KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,   KC_SCRL,  KC_PAUSE,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,   _______,  _______,  _______,  _______,  KC_PSCR,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  QK_RGB_MATRIX_HUE_DOWN,  QK_RGB_MATRIX_HUE_UP,  RGB_M_P,  QK_RGB_MATRIX_MODE_PREVIOUS,  QK_RGB_MATRIX_MODE_NEXT,  QK_RGB_MATRIX_TOGGLE,  KC_INS,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  QK_RGB_MATRIX_SATURATION_DOWN,  QK_RGB_MATRIX_SATURATION_UP,  QK_RGB_MATRIX_SPEED_DOWN,  _______,             _______,  KC_END,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  QK_RGB_MATRIX_VALUE_DOWN,  QK_RGB_MATRIX_VALUE_UP,  _______,   MO(2),    QK_RGB_MATRIX_VALUE_UP,
        _______,  _______,  _______,                      _______,                                _______,  _______,              QK_RGB_MATRIX_SPEED_DOWN, QK_RGB_MATRIX_VALUE_DOWN,  QK_RGB_MATRIX_SPEED_UP
        ),

    [2] = LAYOUT( /* Options Layer */
        _______,  TO(0),    TO(3),    TO(4),   _______,  _______,    _______,    _______,    _______,    _______,    _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  TD_KB_RST,  _______,  _______,  _______,  SOCDTOG,  _______,  _______,  _______,  SWITCH_MODE,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  SIGNAL_MODE,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,         _______,  _______,
        _______,  _______,  TD_KB_CLR,   _______,  _______,  _______,  NK_TOGG,  _______,  _______,  _______,  _______,  _______, _______,  _______,
        _______,  _______,  _______,                      _______,                                 _______,  _______,            _______,  _______,  _______
        ),

    [3] = LAYOUT( /* Mac Layer */
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  KC_LALT,  KC_LGUI,                      _______,                                 KC_RGUI,  _______,            _______,  _______,  _______
        ),


    [4] = LAYOUT( /* Num Layer */
        _______,  TO(0),    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  KC_NUM,   KC_P7,  KC_P8,  KC_P9,  KC_PAST,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  XXXXXXX,  KC_P4,  KC_P5,  KC_P6,  KC_PMNS,  _______,  _______,  _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  XXXXXXX,  KC_P1,  KC_P2,  KC_P3,  KC_PPLS,  _______,            _______,  _______,
        _______,  _______,  _______,  _______,  _______,  _______,  XXXXXXX,  KC_P0,  KC_PDOT,  KC_PSLS,  KC_PENT,  _______, _______,      _______,
        _______,  _______,  _______,                      _______,                                 _______,  _______,            _______,  _______,  _______
        ),
};

#ifdef ENCODER_MAP_ENABLE
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},  // Volume control on layer 0
    [1] = {ENCODER_CCW_CW(KC_MRWD, KC_MFFD)},  // No action on layer 1
    [2] = {ENCODER_CCW_CW(_______, _______)}, // Media rewind/forward on layer 2
    [3] = {ENCODER_CCW_CW(_______, _______)},  // No action on layer 3
    [4] = {ENCODER_CCW_CW(_______, _______)},  // No action on layer 4
};
#endif
// clang-format on

// led_config_t g_led_config = {
//     // Key Matrix to LED Index mapping
//     {
//         { 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8 },       // Row 1
//         { 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 7 },  // Row 2
//         { 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 6 },  // Row 3
//         { 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 5 },      // Row 4 (NO_LED removed)
//         { 75, 74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63 },         // Row 5 (adjusted to remove NO_LED)
//         { 76, 77, 78, 79, 0, 1, 2, 3, 4 }                              // Row 6 (adjusted to fill the row and remove NO_LED)
//     },
//     // LED Index to Physical XY Positions (x, y positions of each LED)
//     {
//         {  0,  0 }, { 10,  0 }, { 20,  0 }, { 30,  0 }, { 40,  0 }, { 50,  0 }, { 60,  0 }, { 70,  0 }, { 80,  0 }, { 90,  0 }, {100,  0 }, {110,  0 }, {120,  0 }, {130,  0 }, // Row 1
//         {  0, 10 }, { 10, 10 }, { 20, 10 }, { 30, 10 }, { 40, 10 }, { 50, 10 }, { 60, 10 }, { 70, 10 }, { 80, 10 }, { 90, 10 }, {100, 10 }, {110, 10 }, {120, 10 }, {130, 10 }, {140, 10 }, // Row 2
//         {  0, 20 }, { 10, 20 }, { 20, 20 }, { 30, 20 }, { 40, 20 }, { 50, 20 }, { 60, 20 }, { 70, 20 }, { 80, 20 }, { 90, 20 }, {100, 20 }, {110, 20 }, {120, 20 }, {130, 20 }, {140, 20 }, // Row 3
//         {  0, 30 }, { 10, 30 }, { 20, 30 }, { 30, 30 }, { 40, 30 }, { 50, 30 }, { 60, 30 }, { 70, 30 }, { 80, 30 }, { 90, 30 }, {100, 30 }, {110, 30 }, {120, 30 }, {130, 30 }, // Row 4
//         {  0, 40 }, { 10, 40 }, { 20, 40 }, { 30, 40 }, { 40, 40 }, { 50, 40 }, { 60, 40 }, { 70, 40 }, { 80, 40 }, { 90, 40 }, {100, 40 }, {110, 40 }, {120, 40 }, // Row 5
//         {  0, 50 }, { 10, 50 }, { 20, 50 }, { 30, 50 }, { 40, 50 }, { 50, 50 }, { 60, 50 }, { 70, 50 }, { 80, 50 } // Row 6
//     },
//     // LED Index to flags
//     {
//         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Row 1 (14 LEDs)
//         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Row 2 (15 LEDs)
//         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Row 3 (15 LEDs)
//         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Row 4 (14 LEDs)
//         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Row 5 (12 LEDs)
//         1, 1, 1, 1, 1, 1, 1, 1 // Row 6 (8 LEDs)
//     }
// };

// Bugfix (continuation pass): these were previously only ever `extern`-declared (also in
// features/indicators.c) with no definition anywhere in the project, which cannot link. This is
// their one real definition; indicators.c keeps its extern declaration and links against this.
uint8_t is_orgb_mode = 0;
uint8_t is_srgb_mode = 0;


bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!process_socd_cleaner(keycode, record, &socd_v)) {
        return false;
    }
    if (!process_socd_cleaner(keycode, record, &socd_h)) {
        return false;
    }
    if (!process_rgb_keys(keycode, record)) {
        return false;
    }

    switch (keycode) {
case KC_CAPS: // Sixth pass: instant Caps Lock LED (see features/indicators.c)
    if (record->event.pressed) { indicators_caps_key_pressed(); }
    return true;

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
        default:
            return true;
    }

    return true;
}

// R75 spec update (2026-09) sec 5: keeps the reactive-glow idle-animation timer accurate
// regardless of which RGB mode is active, the same fix applied to the ANSI keymap -- see
// custom_rgb.h's glow_track_real_input() for the bug this avoids (previously that tracking only
// ran while reactive_energy/Darkening happened to be the actively-rendering mode, so switching
// back to either from Row Wave/Fairy Orb/a stock effect after a stretch of real typing could
// read as long-idle and fire an idle event almost immediately). ISO doesn't have the rest of this
// project's Fn/RGBCTL-layer scaffolding (see HANDOFF.md) but shares every RGB-mode/effect file
// with ANSI, including the idle animation itself, so this one-line fix applies here too.
void matrix_scan_user(void) {
    glow_track_real_input();
    glow_idle_simulation_tick(); // fourth pass: mirrors the ANSI keymap's own matrix_scan_user --
                                   // see custom_rgb.h's comment on this function. any_overlay_
                                   // suppressing_now()'s layer checks (custom_rgb.c) are numbered
                                   // against ANSI's layer scheme; ISO's differs (this file's own
                                   // layer 2/3 are Options/Mac, not RGB Control -- ISO has none),
                                   // so idle events here also happen to pause while those are
                                   // active. Harmless (ISO has no numbered-slot/Esc-color system
                                   // this pass touches to begin with -- see rgb_matrix_user.inc's
                                   // own note on ISO's effects being a best-effort mirror, not a
                                   // separately-verified pass) and not worth a second, ISO-specific
                                   // suppression check for.
}
