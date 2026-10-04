// Copyright 2026 Saurabh Nakkarike (@snakkarike)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#define DISABLE_MOUSE_KEYS
#define DISABLE_EXTRA_KEYS
#define DISABLE_MIDI
#define DISABLE_SERIAL
#define DISABLE_STENO
#define NKRO_DEFAULT_ON true

// #define DYNAMIC_KEYMAP_LAYER_COUNT 6 // 9 -- defaults to 4, now defined in keyboard.json
// #define WEAR_LEVELING_LOGICAL_SIZE 2048 // defined below
// #define WEAR_LEVELING_BACKING_SIZE 4096 // defined in keyboard.json

// #define LED_CAPS_LOCK_PIN B0 // defined in keyboard.json
#define LED_WIN_LOCK_PIN B9
#define LED_MAC_PIN B8

#define RGB_MATRIX_LED_COUNT 80
/* SPI */
#define SPI_DRIVER SPIDQ
#define SPI_SCK_PIN B3
#define SPI_MOSI_PIN B5
#define SPI_MISO_PIN B4

/* Flash */
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12
// #define WEAR_LEVELING_BACKING_SIZE // defined in keyboard.json
#define WEAR_LEVELING_LOGICAL_SIZE (WEAR_LEVELING_BACKING_SIZE / 2)

// EEPROM data for the RGB profile store (features/rgb_profiles.c). Fifth pass restructuring: each
// profile now stores every target's own [hue,sat,val,speed] directly (13 targets * 4 bytes = 52,
// plus 1 effect-mode byte plus 1 caps-enabled byte = 54 bytes/profile) rather than a small
// per-profile record plus one block shared by all four -- 4 profiles * 54 = 216, plus 1
// active-index byte = 217 total. A plain literal rather than computed from rgb_profiles.h's
// RGB_TOTAL_KB_DATA_SIZE macro, since this file is included too broadly across the codebase to
// safely depend on a features/ header; rgb_profiles.c _Static_assert-checks the two stay in sync
// instead.
//
// Spec update (2026-09) sec 19: shrank from 53 to 50 (-3 B) -- the Windows/Mac shortcut-feedback
// target was removed along with the feature it belonged to (9 stored targets -> 8).
// Fourth pass (2026-09): grew from 50 to 62 (+12 B) -- four new stored targets (Esc, Ripple Pool,
// Comet Trail, Equalizer; 8 -> 12, see rgb_profiles.h's RGB_EFFECT_COLOR_COUNT).
// Fourth pass follow-up: Equalizer removed again (see rgb_control.h) -- 62 -> 59 (-3 B), 12 -> 11
// stored targets.
// Fifth pass (2026-09): grew again, substantially, to 217 -- per the person's own explicit
// request ("all parameters... saved independently for each user profile and for each mode"),
// every target's color *and now speed* moved from one shared block into each profile's own copy,
// and RGBCTL_TARGET_ORB was added (Fairy Orb's own target, 13 stored targets total including
// Base, which is now stored as just another target rather than separate loose fields). As with
// every previous EEPROM layout change in this project, a Clear-EEPROM after flashing this build
// is required, not just recommended, this time -- the shape of the data itself changed (per-
// profile instead of shared), not just its size, so old EEPROM contents cannot be reinterpreted
// as the new layout at all.
// Fifth pass addendum: +1 byte for the layout-version byte (rgb_profiles.h) = 218 -- lets the firmware
// reset stale old-layout profile data by itself on first boot instead of needing a manual Clear-EEPROM.
// Sixth pass: RGBCTL_TARGET_ENCODER removed -> 12 stored targets (4 B each) + effect_id + caps_enabled = 50 B
// per profile; 4 profiles = 200 B, +1 active index, +1 layout version = 202. The layout-version byte
// (rgb_profiles.c, now 6) makes the firmware reset stale 218-byte-layout profiles itself on first boot.
#define EECONFIG_KB_DATA_SIZE 202

/* Rotary encoder */
// Sixth pass -- direction-reversal fix. QMK's quadrature driver (4x resolution, the default here) only
// reports a tick after counting a full 4 pulses, and the count carries over between detents. If it is
// ever left non-zero at rest (contact bounce at the end of a click, or a click that ends one pulse
// short), the first detent after reversing direction only cancels that leftover and produces no
// event -- exactly "the first tick in the opposite direction is ignored, the second one works".
// ENCODER_DEFAULT_POS tells the driver which pin state is the detent's resting position (0x3 = both
// pins pulled high, which is how this board's encoder is wired: pins B7/B6 are configured as
// input-with-pull-up), so it emits any pending movement and zeroes the counter every time the knob
// settles there. Nothing in this repo's own code drops encoder ticks (rotation always passes through
// to the host); this is purely the driver-level fix QMK documents for this symptom.
#define ENCODER_DEFAULT_POS 0x3

/* RGB Matrix */
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES

/* WS2812 */
#define WS2812_SPI_DRIVER SPIDM2
#define WS2812_SPI_DIVISOR 32
#define WS2812_DRIVER spi

// /* Default Effects */
// #define RGB_MATRIX_DEFAULT_ON true
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_CUSTOM_reactive_energy
#define RGB_MATRIX_DEFAULT_HUE 166
#define RGB_MATRIX_DEFAULT_SAT 255
#define RGB_MATRIX_DEFAULT_VAL 128
// #define RGB_MATRIX_DEFAULT_HUE 0
// #define RGB_MATRIX_DEFAULT_SAT 255

// Fifth pass (2026-09): Base's own saturation/brightness adjustment step, matched to what every
// other color target already used (features/rgb_control.c's SAT_STEP/VAL_STEP = 8) instead of
// stock QMK's 16 -- reported as the Fn layer's brightness having coarser granularity than the
// main backlight's, and asked to have more steps to match. See rgb_control.c's own comment on
// SAT_STEP/VAL_STEP for the full reasoning (including why the fix goes toward *finer* steps
// everywhere rather than picking whichever value matches the stated direction). HUE_STEP already
// matches at 8 (stock default) and isn't overridden here; speed's step (16) is left at stock too.
// Also applies to any stock brightness/saturation keycode bound elsewhere (e.g. ISO's own Fn-layer
// QK_RGB_MATRIX_VALUE_UP/DOWN keys) -- same direction, finer, so consistent with the request.
#define RGB_MATRIX_VAL_STEP 8
#define RGB_MATRIX_SAT_STEP 8

#ifdef OPENRGB_ENABLE

#endif

    /* RGB Matrix effect */
    #define ENABLE_RGB_MATRIX_SOLID_COLOR
    // ENABLE_RGB_MATRIX_ALPHAS_MODS disabled (fourth pass follow-up): reported as "2 identical
    // modes... just a single solid color" at the start of the list, alongside Solid Color just
    // above. Checked against this stock effect's own source (quantum/rgb_matrix/animations/
    // alpha_mods_anim.h, not assumed): its second ("mod") color is Solid Color's own hue shifted
    // by the *configured animation speed value itself* (hsv.h += rgb_matrix_config.speed) -- at
    // whatever speed this board is actually set to, that shift can be small enough that alphas
    // and mods read as effectively the same color, which is exactly the "identical" symptom
    // reported. This is stock QMK code this project doesn't patch (see the note on
    // RAINBOW_MOVING_CHEVRON below, same reasoning) -- disabling it here, a plain config.h toggle
    // like every other ENABLE_RGB_MATRIX_* line, is the available fix rather than a core change.
    #define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
    #define ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
    #define ENABLE_RGB_MATRIX_BREATHING
    #define ENABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
    #define ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
    #define ENABLE_RGB_MATRIX_CYCLE_ALL
    // ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT disabled (sixth pass): replaced one-for-one by the custom
    // "Rainbow Right" effect (rainbow_wave_ltr, rgb_effects.c -- same math). Stock effect order is fixed in
    // QMK core, so the only way to put Rainbow Left directly next to it is to make both custom.
    // ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON disabled (fourth pass follow-up): reported as "2
    // identical rainbow wave animations going left to right" alongside Cycle Left/Right just
    // above. Checked against its own source (rainbow_moving_chevron_anim.h): its hue formula is
    // `abs8(y - center_y) + (x - time)` -- the exact same `x - time` sweep Cycle Left/Right uses,
    // plus a chevron-shaped offset from vertical center that only varies over this board's own
    // short row count (6 distinct row y-values, keyboard.json), so on this specific board the two
    // read as practically the same effect rather than genuinely different ones. Disabling the more
    // redundant-looking of the two (keeping the more clearly-named Cycle Left/Right) is the
    // config-level fix available without patching stock code -- see rainbow_wave_rtl_render()
    // (rgb_effects.c/.h, registered below) for the genuine right-to-left companion added in its
    // place, since a config toggle alone can't make a stock effect run in reverse.
    #define ENABLE_RGB_MATRIX_CYCLE_SPIRAL
    #define ENABLE_RGB_MATRIX_RAINBOW_BEACON
    #define ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
    #define ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
    #define ENABLE_RGB_MATRIX_HUE_PENDULUM
    #define ENABLE_RGB_MATRIX_TYPING_HEATMAP
    // R75 spec round (2026-09) sec 20: this stock effect (quantum/rgb_matrix/animations/
    // digital_rain_anim.h) drives its "falling" positions purely from raw MATRIX_ROWS/
    // MATRIX_COLS + rgb_matrix_map_row_column_to_led() -- i.e. it assumes the switch matrix
    // itself is a clean visual grid, not this board's actual physical LED geometry (uneven row
    // spacing, stepped bottom row, ANSI gap -- see g_led_config, keyboards/r75/*/keyboard.json).
    // That mismatch is exactly why it reads as "very broken" on real hardware. It's a stock QMK
    // effect, not this project's code, so properly fixing it would mean either patching quantum/
    // core (which this project has deliberately avoided everywhere else -- see HANDOFF.md) or
    // writing a full bespoke replacement, neither of which the spec asks for; it explicitly
    // allows removal when a clean fix isn't proportionate ("a broken mode is worse than not
    // having the mode"). Removed rather than kept broken. #undef, not just leaving it enabled
    // unused, so it can't be re-selected via VIA/OpenRGB/mode-cycling either.
    // #define ENABLE_RGB_MATRIX_DIGITAL_RAIN
    #define ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
    #define ENABLE_RGB_MATRIX_OPENRGB

    #define OPENRGB_DIRECT_MODE_UNBUFFERED
