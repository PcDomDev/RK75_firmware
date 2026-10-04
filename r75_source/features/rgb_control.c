#include "rgb_control.h"
#include "rgb_effects.h"
#include "custom_rgb.h"
#include "indicator_queue.h"
#include "rgb_profiles.h" // EEPROM persistence underneath this file's in-RAM working copy --
                            // see that file for the split rationale.
#include <lib/lib8tion/lib8tion.h> // qadd8/qsub8 -- see rgb_effects.c's include of the same
                                     // header for why this isn't pulled in transitively.

// --- Tunables ------------------------------------------------------------------------------

#define RGBCTL_HOLD_THRESHOLD_MS 180 // Deliberately much shorter than the spec's ~0.75-1.0s: this
// is the threshold for the *keymap* to commit to "this is a hold, not a tap" at all (so a tap can
// still cleanly fall through to sending \ instead of ever touching Layer 2) -- it is not the
// "how long the user waits looking at Layer 2 lighting up" number the spec means. Layer 2 lights
// up immediately at 180ms and stays on for as long as the key is held, with save-on-release when
// it's let go.
#define RGBCTL_FN_LAYER 1
#define RGBCTL_LAYER 2 // internally still QMK layer index 2 -- see HANDOFF.md for why this
// project keeps that numbering even though the spec's own prose calls it "Layer 3".
#define RGBCTL_BAR_LED_HIGH 21 // Esc -- same 14-LED Esc-Del strip used throughout
#define RGBCTL_BAR_LED_COUNT 14
#define RGBCTL_CONFIRM_LED 36 // the \ key's own LED
#define RGBCTL_BAR_HOLD_MS 900
#define RGBCTL_BAR_FADE_MS 200

#define DFU_HOLD_THRESHOLD_MS 5000
#define DFU_CANCEL_FADE_MS 120

// Spec sec 38 / update sec 30: any destructive/reset/clear action needs a RED indicator plus a
// safety delay before it actually executes -- 5000ms as of the spec update (was 2000ms).
#define RESET_HOLD_THRESHOLD_MS 5000
#define RESET_CANCEL_FADE_MS 150

#define HUE_STEP 8 // matches this fork's own RGB_MATRIX_HUE_STEP default (quantum/rgb_matrix/
                    // rgb_matrix.c) -- kept as its own literal since that macro isn't usable
                    // outside the _noeeprom helper it's baked into.
#define SAT_STEP 8

// Fifth pass: brightness stepping. Reported as the Fn-layer highlight's brightness having "fewer
// discrete adjustment steps (coarser granularity)" than the main backlight's, with the ask being to
// increase the number of steps so they match. Traced rather than assumed: on paper the step
// constants pointed the *other* way (every non-Base target used 8 here; Base goes through stock
// rgb_matrix_increase_val_noeeprom(), which steps by RGB_MATRIX_VAL_STEP = 16 -- checked in this
// tree's quantum/rgb_matrix/rgb_matrix.{h,c}, and no keyboard.json/rules.mk/config.h override of
// it exists), and Fn-help's own draw (indicators.c) applies no quantization of its own. Since a
// step constant that already looked finer can't by itself explain the coarseness described, and
// there is no hardware here to test against, the fix is aimed at the goal rather than at one
// suspected number: brightness now uses ONE shared stepping rule for every target *including
// Base* (so "match the standard backlight" is true by construction, not by two numbers happening
// to agree), and that rule is much finer than either old constant -- a single tap moves by 2
// (128 taps to cross the full range, vs. 16 or 32 before). A range that fine would make a full
// sweep painfully slow at the fixed 70ms hold-repeat, so that pass accelerated the step with hold
// time. Sixth pass: that acceleration was removed again at the person's request -- see VAL_STEP below.
// Fifth pass: speed is now adjustable per-target (rgbctl_adjust_speed() below) rather than only
// ever touching the one real global -- SPEED_STEP matches RGB_MATRIX_SPD_STEP (16), so Base's
// stock-driven speed and every other target's own stored speed step identically.
#define SPEED_STEP 16

// Spec update (2026-09) sec 23: holding an adjustment key repeats its action instead of firing
// once -- these keycodes are never sent as real HID (they `return false`), so OS-level key-repeat
// never applies to them; this is a from-scratch, in-firmware equivalent. Fires immediately on
// press (unchanged single-tap feel), waits REPEAT_INITIAL_DELAY_MS before the first repeat (so a
// quick, deliberate single tap never double-fires), then repeats every REPEAT_INTERVAL_MS.
#define REPEAT_INITIAL_DELAY_MS 380
#define REPEAT_INTERVAL_MS 70

// Sixth pass: brightness acceleration removed. Previously the step grew with hold time
// (2 -> 4 -> 8 -> 16, see this file's history), which made a long hold sweep faster the longer it
// lasted. Brightness now moves by one constant step per action, so a hold changes it at a constant
// rate: VAL_STEP per REPEAT_INTERVAL_MS (4 per 70 ms = roughly 4.5 s for the full 0-255 range), and
// a single tap moves by exactly VAL_STEP. Tune the feel by changing this one number.
#define VAL_STEP 4

typedef enum {
    BAR_PARAM_NONE = 0,
    BAR_PARAM_HUE,
    BAR_PARAM_SAT,
    BAR_PARAM_VAL,
    BAR_PARAM_SPEED,
} bar_param_t;

// Spec update sec 6/7/25: Caps Lock and the Fn/RGB indicator color are selected via their own
// dedicated keys rather than a numbered slot (see rgb_control.h) -- represented as two sentinel
// values current_slot can hold, one past the real 1-10 number-row range, rather than a second
// parallel variable, so there is always exactly one "current selection" to reason about.
#define SLOT_CAPS 11
#define SLOT_FNVIZ 12

// --- All module state, grouped by subsystem -------------------------------------------------
// (declared together, ahead of every function below, so no function ever needs a forward
// declaration just to reach a variable defined "later" in the file)

// Fn+\ hold / Layer 2 session
static bool        hold_key_down   = false;
static bool        layer_active    = false;
static uint16_t    hold_press_time = 0;
static bar_param_t bar_param       = BAR_PARAM_NONE;
static uint16_t    bar_touch_time  = 0;
static bool        session_dirty   = false;

// Fn+Enter DFU hold
static bool     dfu_active      = false;
static uint16_t dfu_press_time  = 0;
static bool     dfu_cancelling  = false;
static uint16_t dfu_cancel_time = 0;
static uint8_t  dfu_cancel_pct  = 0;

// Esc+profile destructive reset hold (spec sec 38/42)
static bool     esc_key_down                          = false;
static bool     profile_key_is_down[RGB_PROFILE_COUNT] = {false, false, false, false};
static bool     reset_hold_active                       = false;
static uint16_t reset_hold_start                        = 0;
static uint8_t  reset_hold_profile                      = 0;
static bool     reset_cancelling                        = false;
static uint16_t reset_cancel_time                       = 0;
static uint8_t  reset_cancel_pct                        = 0;

// Held-adjustment-key auto-repeat (spec update sec 23)
static rgbctl_repeat_fn_t active_repeat_fn    = NULL;
static uint16_t             repeat_press_time   = 0;
static uint16_t             repeat_last_fire_at = 0;

// Per-effect color targets / slots (spec sec 26/32-33; update sec 6/7/25)
static uint8_t current_slot = 1; // 1-10 (numbered), or SLOT_CAPS/SLOT_FNVIZ; see rgb_control.h
static HSV     effect_colors[RGBCTL_TARGET_COUNT]; // index 0 (BASE) unused for h/s -- BASE reads/
                                                      // writes the live global rgb_matrix hsv
                                                      // directly (its .v here is kept in sync
                                                      // anyway, purely so rgbctl_get_slot_color()
                                                      // doesn't need a BASE special case).
// Fifth pass: speed, per target, mirroring effect_colors[] above -- see rgb_control.h's own
// comment on rgbctl_get_effect_speed() for why. Index 0 (BASE) *is* meaningful here (unlike
// effect_colors[]'s index 0) and is kept in sync with the live rgb_matrix_get_speed() by
// rgbctl_adjust_speed(), since that's the one target every stock effect (and Darkening/Rainbow
// Wave (R->L)) can only ever read via the real global, not this array.
static uint8_t effect_speeds[RGBCTL_TARGET_COUNT];

// Caps Lock indicator enable/disable
static bool caps_indicator_enabled = true;

// Cleaning Mode
static bool cleaning_mode_active = false; // RAM-only by design -- see rgb_control.h

// Profiles (in-RAM working copy; features/rgb_profiles.c owns the actual struct shape and raw
// EEPROM read/write). Populated for real by keyboard_post_init_user() at the bottom of this file.
static rgb_profile_t profiles[RGB_PROFILE_COUNT];
static uint8_t         active_profile = 0;

// --- Forward declarations (only for the handful of static helpers genuinely called before
// their own definition reads better placed later in the file) --------------------------------
static void rgbctl_save_profile(void); // no longer declared in rgb_control.h -- see the "Profiles"
                                         // section below for why it's file-local now.
static uint8_t profile_key_led(uint8_t index);

// ================================================================================================
// Fn+\ hold detector
// ================================================================================================

void rgbctl_hold_key_event(bool pressed) {
    if (pressed) {
        hold_key_down   = true;
        layer_active     = false; // decided by rgbctl_task() once the threshold actually elapses
        hold_press_time = timer_read();
        return;
    }

    hold_key_down = false;
    if (layer_active) {
        // Was a HOLD session: exit Layer 2, save-if-dirty.
        layer_off(RGBCTL_LAYER);
        layer_active = false;
        if (session_dirty) {
            rgbctl_save_profile();
            session_dirty = false;
        }
    } else {
        // Was a TAP (released before the threshold): switch to the next RGB mode, same call
        // Layer 2's own [/] Effect Next key uses.
        rgbctl_step_effect(true);
        indicator_enqueue(RGBCTL_CONFIRM_LED, 120, 1, RGB_WHITE);
    }
}

bool rgbctl_hold_layer_is_active(void) {
    return layer_active;
}

static void rgbctl_dfu_cancel(void) {
    uint16_t elapsed = timer_elapsed(dfu_press_time);
    dfu_cancel_pct   = (elapsed >= DFU_HOLD_THRESHOLD_MS) ? 255 : (uint8_t)(((uint32_t)elapsed * 255) / DFU_HOLD_THRESHOLD_MS);
    dfu_active       = false;
    dfu_cancelling   = true;
    dfu_cancel_time  = timer_read();
}

void rgbctl_repeat_key_event(rgbctl_repeat_fn_t fn, bool pressed) {
    if (pressed) {
        fn(); // fire immediately once, same as before this feature existed
        active_repeat_fn    = fn;
        repeat_press_time   = timer_read();
        repeat_last_fire_at = repeat_press_time;
    } else if (active_repeat_fn == fn) {
        active_repeat_fn = NULL;
    }
}

void rgbctl_task(void) {
    if (hold_key_down && !layer_active && timer_elapsed(hold_press_time) >= RGBCTL_HOLD_THRESHOLD_MS) {
        layer_active = true;
        layer_on(RGBCTL_LAYER);
    }

    // Both the 5000ms threshold and "Fn released early" have to be caught by polling here, not
    // from the Enter-position key's own press/release events alone -- QMK resolves a held key's
    // identity fresh at every event, so releasing Fn doesn't fire a release event for *this*
    // keycode; layer_state_is(RGBCTL_FN_LAYER) has to be checked every scan to notice "Fn let go
    // while Enter-position is still held".
    if (dfu_active) {
        if (!layer_state_is(RGBCTL_FN_LAYER)) {
            rgbctl_dfu_cancel();
        } else if (timer_elapsed(dfu_press_time) >= DFU_HOLD_THRESHOLD_MS) {
            dfu_active = false;
            reset_keyboard(); // never returns
        }
    }

    // Same "must poll, can't rely on the qualifying keys' own release events alone" reasoning as
    // DFU above applies here too.
    if (reset_hold_active && timer_elapsed(reset_hold_start) >= RESET_HOLD_THRESHOLD_MS) {
        reset_hold_active = false;
        active_profile      = reset_hold_profile; // guarantee this resets the held profile even
                                                     // if some other one was left active_profile
                                                     // by an earlier, unrelated load.
        rgbctl_reset_profile();
        indicator_enqueue(RGBCTL_BAR_LED_HIGH, 220, 2, RGB_RED);           // Esc
        indicator_enqueue(profile_key_led(reset_hold_profile), 220, 2, RGB_RED); // that profile's F-key
    }

    // Spec update sec 23: auto-repeat for whichever adjustment key is currently held.
    if (active_repeat_fn && timer_elapsed(repeat_press_time) >= REPEAT_INITIAL_DELAY_MS) {
        if (timer_elapsed(repeat_last_fire_at) >= REPEAT_INTERVAL_MS) {
            active_repeat_fn();
            repeat_last_fire_at = timer_read();
        }
    }
}

void rgbctl_dfu_key_event(bool pressed) {
    if (pressed) {
        dfu_active      = true;
        dfu_press_time  = timer_read();
        dfu_cancelling  = false;
    } else if (dfu_active) {
        rgbctl_dfu_cancel(); // early release of the Enter-position key itself
    }
}

// White -> red as progress approaches 100% -- R held at full, G/B fade out together.
static RGB dfu_progress_color(uint8_t pct) {
    uint8_t fade = 255 - pct;
    return (RGB){.r = 255, .g = fade, .b = fade};
}

void rgbctl_render_dfu_bar(uint8_t led_min, uint8_t led_max) {
    if (dfu_active) {
        uint16_t elapsed = timer_elapsed(dfu_press_time);
        uint8_t  pct      = (elapsed >= DFU_HOLD_THRESHOLD_MS) ? 255 : (uint8_t)(((uint32_t)elapsed * 255) / DFU_HOLD_THRESHOLD_MS);
        draw_level_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, pct, dfu_progress_color(pct), 255);
    } else if (dfu_cancelling) {
        uint16_t elapsed = timer_elapsed(dfu_cancel_time);
        if (elapsed >= DFU_CANCEL_FADE_MS) {
            dfu_cancelling = false;
            return;
        }
        uint8_t fade_pct = 255 - (uint8_t)(((uint32_t)elapsed * 255) / DFU_CANCEL_FADE_MS);
        draw_level_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, dfu_cancel_pct, dfu_progress_color(dfu_cancel_pct), fade_pct);
    }
}

// ================================================================================================
// Per-effect color targets / slots
// ================================================================================================

HSV rgbctl_get_effect_color(rgbctl_color_target_t target) {
    if (target == RGBCTL_TARGET_BASE || target >= RGBCTL_TARGET_COUNT) {
        return rgb_matrix_get_hsv();
    }
    return effect_colors[target];
}

// Fifth pass: see rgb_control.h's own comment on this function for the BASE special case.
uint8_t rgbctl_get_effect_speed(rgbctl_color_target_t target) {
    if (target == RGBCTL_TARGET_BASE || target >= RGBCTL_TARGET_COUNT) {
        return rgb_matrix_get_speed();
    }
    return effect_speeds[target];
}

// Spec sec 32: "an effect should only expose/use as many color slots as it actually supports" --
// see rgb_control.h's longer comment on rgbctl_slot_count_for_active_mode() for the exact table.
// Only ever called with a genuine 1-10 numbered slot -- SLOT_CAPS/SLOT_FNVIZ are resolved
// directly in rgbctl_current_target() below and never reach this function.
static rgbctl_color_target_t target_for_slot(uint8_t slot) {
    uint8_t mode = rgb_matrix_get_mode();
    if (mode == RGB_MATRIX_CUSTOM_reactive_energy) {
        switch (slot) {
            case 1: return RGBCTL_TARGET_BASE;
            case 2: return RGBCTL_TARGET_ENTER;
            case 3: return RGBCTL_TARGET_BACKSPACE;
            case 4: return RGBCTL_TARGET_DELETE;
            case 5: return RGBCTL_TARGET_SPACE;
            case 6: return RGBCTL_TARGET_ESC; // sixth pass: was slot 7 (slot 6 used to be the encoder wave)
            default: return RGBCTL_TARGET_BASE;
        }
    }
    if (mode == RGB_MATRIX_CUSTOM_row_wave) {
        return (slot == 1) ? RGBCTL_TARGET_ROWWAVE : RGBCTL_TARGET_BASE;
    }
    // Fourth pass: same one-slot-is-its-own-accent-color treatment as Row Wave above, for the
    // three new standalone modes this pass adds.
    if (mode == RGB_MATRIX_CUSTOM_ripple_pool) {
        return (slot == 1) ? RGBCTL_TARGET_RIPPLE : RGBCTL_TARGET_BASE;
    }
    if (mode == RGB_MATRIX_CUSTOM_comet_trail) {
        return (slot == 1) ? RGBCTL_TARGET_COMET : RGBCTL_TARGET_BASE;
    }
    // Fifth pass: Fairy Orb gets its own target now (independent color/brightness/speed, per the
    // person's own request) instead of falling through to Base below.
    if (mode == RGB_MATRIX_CUSTOM_fairy_orb) {
        return (slot == 1) ? RGBCTL_TARGET_ORB : RGBCTL_TARGET_BASE;
    }
    return RGBCTL_TARGET_BASE; // every other mode (Darkening, Rainbow Wave (R->L), any stock
                                 // effect): one slot, always the live global color/speed.
}

uint8_t rgbctl_slot_count_for_active_mode(void) {
    return (rgb_matrix_get_mode() == RGB_MATRIX_CUSTOM_reactive_energy) ? 6 : 1;
}

bool rgbctl_slot_is_valid(uint8_t slot) {
    return slot >= 1 && slot <= rgbctl_slot_count_for_active_mode();
}

void rgbctl_select_color_slot(uint8_t slot) {
    if (!rgbctl_slot_is_valid(slot)) {
        return; // unrelated/inactive number key for the current mode -- no-op, stays OFF (sec 36)
    }
    // Spec update sec 20: the confirmation flash this used to trigger is removed -- selection is
    // now communicated entirely by the persistent, always-current indicator state (sec 24), which
    // doesn't need a transient cue on top of it.
    current_slot = slot;
}

void rgbctl_select_caps_target(void) {
    // First press (Caps not selected): select it as the color target and make sure the indicator is
    // enabled, since choosing to edit its color implies wanting to see it.
    // Second press (already selected): turn the Caps Lock indicator OFF entirely and leave the Caps
    // target (back to slot 1), so the hue/sat/val keys stop silently editing a color that is no
    // longer shown. Sixth pass: it used to stay selected while disabled, which made the layer keep
    // showing the Caps color and the second press look like a no-op. A third press re-selects and
    // re-enables it, so repeated presses cycle: configure -> off -> configure -> ...
    if (current_slot == SLOT_CAPS) {
        caps_indicator_enabled = false;
        current_slot            = 1;
    } else {
        current_slot            = SLOT_CAPS;
        caps_indicator_enabled = true;
    }
    session_dirty = true;
}

void rgbctl_select_fnviz_target(void) {
    current_slot = SLOT_FNVIZ;
}

uint8_t rgbctl_current_slot(void) {
    return current_slot;
}

rgbctl_color_target_t rgbctl_current_target(void) {
    if (current_slot == SLOT_CAPS) {
        return RGBCTL_TARGET_CAPS;
    }
    if (current_slot == SLOT_FNVIZ) {
        return RGBCTL_TARGET_FNVIZ;
    }
    return target_for_slot(current_slot);
}

void rgbctl_set_effect_target_raw(rgbctl_color_target_t target, uint8_t hue, uint8_t sat, uint8_t val, uint8_t speed) {
    if (target < RGBCTL_TARGET_COUNT) {
        effect_colors[target].h = hue;
        effect_colors[target].s = sat;
        effect_colors[target].v = val;
        effect_speeds[target]    = speed;
    }
}

HSV rgbctl_get_slot_color(uint8_t slot) {
    return rgbctl_get_effect_color(target_for_slot(slot));
}

// ================================================================================================
// Caps Lock indicator enable/disable
// ================================================================================================

bool rgbctl_caps_indicator_enabled(void) {
    return caps_indicator_enabled;
}

void rgbctl_caps_indicator_toggle(void) {
    caps_indicator_enabled = !caps_indicator_enabled;
    session_dirty            = true;
}

// ================================================================================================
// Mac-mode / Cleaning Mode shared state
// ================================================================================================

#define MAC_LAYER 4

bool rgbctl_is_mac_mode(void) {
    return layer_state_is(MAC_LAYER);
}

bool rgbctl_cleaning_mode_active(void) {
    return cleaning_mode_active;
}

void rgbctl_cleaning_mode_toggle(void) {
    cleaning_mode_active = !cleaning_mode_active;
}

// ================================================================================================
// Parameter bar visibility
// ================================================================================================

static void bar_touch(bar_param_t p) {
    bar_param      = p;
    bar_touch_time = timer_read();
}

void rgbctl_render_bar(uint8_t led_min, uint8_t led_max) {
    if (bar_param == BAR_PARAM_NONE) {
        return;
    }
    uint16_t elapsed = timer_elapsed(bar_touch_time);
    uint8_t  brightness_pct;
    if (elapsed < RGBCTL_BAR_HOLD_MS) {
        brightness_pct = 255;
    } else if (elapsed < RGBCTL_BAR_HOLD_MS + RGBCTL_BAR_FADE_MS) {
        uint16_t fade_elapsed = elapsed - RGBCTL_BAR_HOLD_MS;
        brightness_pct         = 255 - (uint8_t)(((uint32_t)fade_elapsed * 255) / RGBCTL_BAR_FADE_MS);
    } else {
        bar_param = BAR_PARAM_NONE;
        return;
    }

    HSV hsv = rgbctl_get_effect_color(rgbctl_current_target());
    switch (bar_param) {
        case BAR_PARAM_HUE:
            draw_hue_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, hsv.h, brightness_pct);
            break;
        case BAR_PARAM_SAT:
            draw_level_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, hsv.s, (RGB){RGB_WHITE}, brightness_pct);
            break;
        case BAR_PARAM_VAL:
            draw_level_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, hsv.v, (RGB){RGB_WHITE}, brightness_pct);
            break;
        case BAR_PARAM_SPEED:
            // Fifth pass: the selected target's own speed, not the one shared global -- same
            // reasoning as the Left/Right key indicator (indicators.c).
            draw_level_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, rgbctl_get_effect_speed(rgbctl_current_target()), (RGB){RGB_WHITE}, brightness_pct);
            break;
        default:
            break;
    }
}

// ================================================================================================
// Layer 2 actions
// ================================================================================================

void rgbctl_adjust_hue(bool increase) {
    if (rgbctl_current_target() == RGBCTL_TARGET_BASE) {
        if (increase) {
            rgb_matrix_increase_hue_noeeprom();
        } else {
            rgb_matrix_decrease_hue_noeeprom();
        }
    } else {
        rgbctl_color_target_t t = rgbctl_current_target();
        effect_colors[t].h += increase ? HUE_STEP : -HUE_STEP; // uint8_t: wraps, correct for a
                                                                   // circular hue
    }
    bar_touch(BAR_PARAM_HUE);
    session_dirty = true;
}

void rgbctl_adjust_sat(bool increase) {
    if (rgbctl_current_target() == RGBCTL_TARGET_BASE) {
        if (increase) {
            rgb_matrix_increase_sat_noeeprom();
        } else {
            rgb_matrix_decrease_sat_noeeprom();
        }
    } else {
        rgbctl_color_target_t t = rgbctl_current_target();
        uint8_t                  s = effect_colors[t].s;
        effect_colors[t].s          = increase ? qadd8(s, SAT_STEP) : qsub8(s, SAT_STEP);
    }
    bar_touch(BAR_PARAM_SAT);
    session_dirty = true;
}

void rgbctl_adjust_val(bool increase) {
    const uint8_t          step = VAL_STEP;
    rgbctl_color_target_t t    = rgbctl_current_target();
    if (t == RGBCTL_TARGET_BASE) {
        // Fifth pass: was rgb_matrix_increase_val_noeeprom()/decrease (stock, fixed step 16) --
        // now the same stepping rule as every other target, so Base and the rest are identical by
        // construction. rgb_matrix_sethsv_noeeprom() applies RGB_MATRIX_MAXIMUM_BRIGHTNESS itself.
        HSV     hsv = rgb_matrix_get_hsv();
        uint8_t v    = increase ? qadd8(hsv.v, step) : qsub8(hsv.v, step);
        rgb_matrix_sethsv_noeeprom(hsv.h, hsv.s, v);
    } else {
        uint8_t v          = effect_colors[t].v;
        effect_colors[t].v = increase ? qadd8(v, step) : qsub8(v, step);
    }
    bar_touch(BAR_PARAM_VAL);
    session_dirty = true;
}

// Fifth pass: previously always touched the one real global speed regardless of target -- now
// mirrors rgbctl_adjust_hue()/_sat()/_val() above exactly. For RGBCTL_TARGET_BASE, the real global
// is still authoritative (every stock effect, plus Darkening/Rainbow Wave (R->L), can only ever
// read that), so it's adjusted directly and effect_speeds[BASE] is kept in sync purely so
// rgbctl_get_effect_speed(BASE) never needs a special case of its own beyond what rgbctl_get_
// effect_speed() already has. Every other target now has a genuinely independent stored speed --
// this is what makes "individual animation speed... using the number keys" (the person's own
// request) and Fairy Orb's own independent speed (custom_rgb.c) both actually work.
void rgbctl_adjust_speed(bool increase) {
    rgbctl_color_target_t t = rgbctl_current_target();
    if (t == RGBCTL_TARGET_BASE) {
        if (increase) {
            rgb_matrix_increase_speed_noeeprom();
        } else {
            rgb_matrix_decrease_speed_noeeprom();
        }
        effect_speeds[RGBCTL_TARGET_BASE] = rgb_matrix_get_speed();
    } else {
        uint8_t s          = effect_speeds[t];
        effect_speeds[t]    = increase ? qadd8(s, SPEED_STEP) : qsub8(s, SPEED_STEP);
    }
    bar_touch(BAR_PARAM_SPEED);
    session_dirty = true;
}

// Spec update sec 23: no-argument wrappers so rgbctl_repeat_key_event() can hold a plain function
// pointer -- see rgb_control.h.
void rgbctl_hue_up(void) { rgbctl_adjust_hue(true); }
void rgbctl_hue_down(void) { rgbctl_adjust_hue(false); }
void rgbctl_sat_up(void) { rgbctl_adjust_sat(true); }
void rgbctl_sat_down(void) { rgbctl_adjust_sat(false); }
void rgbctl_val_up(void) { rgbctl_adjust_val(true); }
void rgbctl_val_down(void) { rgbctl_adjust_val(false); }
void rgbctl_speed_up(void) { rgbctl_adjust_speed(true); }
void rgbctl_speed_down(void) { rgbctl_adjust_speed(false); }

void rgbctl_step_effect(bool next) {
    if (next) {
        rgb_matrix_step_noeeprom();
    } else {
        rgb_matrix_step_reverse_noeeprom();
    }
    current_slot   = 1; // the new mode's own slot list starts fresh (spec sec 32) -- whatever
                          // slot number was selected under the old mode may not even exist under
                          // the new one, and slot 1 is always valid everywhere.
    session_dirty = true;
}

void rgbctl_solid_mode(void) {
    rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
    current_slot = 1;
    session_dirty = true;
}

void rgbctl_toggle(void) {
    rgb_matrix_toggle_noeeprom();
    session_dirty = true;
}

// ================================================================================================
// Profiles
// ================================================================================================

// The old standalone "Save" key is folded into the existing auto-save-on-Layer-2-release
// behavior in rgbctl_hold_key_event() above -- see HANDOFF.md.
//
// Fifth pass: every target's color+speed (Base included) now lives inside this one profile
// struct -- see rgb_profiles.h's own note on why Base's old standalone hue/sat/val/speed fields
// were folded into targets[RGBCTL_TARGET_BASE] instead of kept separate. Base's own values are
// captured from the live rgb_matrix state (the actual source of truth for it specifically, same
// as before); every other target is captured from this file's own effect_colors[]/effect_speeds[]
// arrays, same as the old shared block used to be, just written into *this* profile's own slot
// instead of one block shared by all four.
static void rgbctl_save_profile(void) {
    rgb_profile_t *p   = &profiles[active_profile];
    p->effect_id         = rgb_matrix_get_mode();
    HSV base_hsv         = rgb_matrix_get_hsv();
    p->targets[RGBCTL_TARGET_BASE].hue   = base_hsv.h;
    p->targets[RGBCTL_TARGET_BASE].sat   = base_hsv.s;
    p->targets[RGBCTL_TARGET_BASE].val   = base_hsv.v;
    p->targets[RGBCTL_TARGET_BASE].speed = rgb_matrix_get_speed();
    for (uint8_t t = 1; t < RGBCTL_TARGET_COUNT; t++) {
        p->targets[t].hue   = effect_colors[t].h;
        p->targets[t].sat   = effect_colors[t].s;
        p->targets[t].val   = effect_colors[t].v;
        p->targets[t].speed = effect_speeds[t];
    }
    p->caps_enabled = caps_indicator_enabled ? 1 : 0;
    rgb_profiles_write(active_profile, p);
}

// Fifth pass: loads *everything* this profile owns -- mode, Base's own live color/speed, and
// every other target's independent color/speed, plus Caps Lock's enabled state -- where this used
// to only load mode+Base+speed and leave every other target reading from the one block every
// profile shared. This is the change that actually makes profiles independent the way the
// person's own request asked for: switching profiles now can never leave another profile's Enter
// color (or Fairy Orb's speed, or anything else) bleeding into the one just loaded.
void rgbctl_load_profile(uint8_t profile) {
    if (profile >= RGB_PROFILE_COUNT) {
        return;
    }
    active_profile         = profile;
    current_slot            = 1; // a different profile can carry a different effect, so the old
                                   // slot number may not even apply to it -- see rgbctl_step_
                                   // effect()'s comment above.
    rgb_profile_t *p       = &profiles[profile];
    rgb_matrix_mode_noeeprom(p->effect_id);
    rgb_matrix_sethsv_noeeprom(p->targets[RGBCTL_TARGET_BASE].hue, p->targets[RGBCTL_TARGET_BASE].sat, p->targets[RGBCTL_TARGET_BASE].val);
    rgb_matrix_set_speed_noeeprom(p->targets[RGBCTL_TARGET_BASE].speed);
    for (uint8_t t = 0; t < RGBCTL_TARGET_COUNT; t++) {
        rgbctl_set_effect_target_raw((rgbctl_color_target_t)t, p->targets[t].hue, p->targets[t].sat, p->targets[t].val, p->targets[t].speed);
    }
    caps_indicator_enabled = p->caps_enabled != 0;
    // Deliberately does not touch session_dirty: loading a profile makes live state match a
    // saved slot exactly, which is the opposite of there being something new to save.
}

void rgbctl_profile_cycle_next(void) {
    rgbctl_load_profile((active_profile + 1) % RGB_PROFILE_COUNT);
}

uint8_t rgbctl_active_profile(void) {
    return active_profile;
}

// Resets *this one profile* back to its factory default -- mode, every target's color+speed, and
// Caps Lock's enabled state, all of it, now that all of it lives inside the one profile struct
// (fifth pass -- previously this already only ever touched the profile's own mode/Base/speed
// fields, since per-effect colors were a separate, shared block at the time; that carve-out no
// longer applies now that there is no shared block left to carve out from).
void rgbctl_reset_profile(void) {
    rgb_profiles_get_factory_default(active_profile, &profiles[active_profile]);
    rgbctl_load_profile(active_profile); // apply it live immediately
    rgb_profiles_write(active_profile, &profiles[active_profile]); // reset always persists, unconditionally
}

// ================================================================================================
// Destructive reset gesture (spec sec 38/42): "Esc + the currently selected profile", held for
// RESET_HOLD_THRESHOLD_MS.
// ================================================================================================

// F5/F6/F7/F8 LEDs, in profile order -- see keyboard.json's rgb_matrix.layout (matrix [0,5..8]).
static uint8_t profile_key_led(uint8_t index) {
    static const uint8_t leds[RGB_PROFILE_COUNT] = {16, 15, 14, 13};
    return (index < RGB_PROFILE_COUNT) ? leds[index] : GLOW_LED_NONE;
}

void rgbctl_reset_key_event(bool is_esc, uint8_t profile_index, bool pressed) {
    if (is_esc) {
        esc_key_down = pressed;
    } else if (profile_index < RGB_PROFILE_COUNT) {
        profile_key_is_down[profile_index] = pressed;
    }

    if (!reset_hold_active) {
        if (esc_key_down) {
            for (uint8_t i = 0; i < RGB_PROFILE_COUNT; i++) {
                if (profile_key_is_down[i]) {
                    reset_hold_active  = true;
                    reset_hold_start   = timer_read();
                    reset_hold_profile = i;
                    reset_cancelling    = false;
                    break;
                }
            }
        }
    } else if (!(esc_key_down && profile_key_is_down[reset_hold_profile])) {
        uint16_t elapsed = timer_elapsed(reset_hold_start);
        reset_cancel_pct  = (elapsed >= RESET_HOLD_THRESHOLD_MS) ? 255 : (uint8_t)(((uint32_t)elapsed * 255) / RESET_HOLD_THRESHOLD_MS);
        reset_hold_active = false;
        reset_cancelling   = true;
        reset_cancel_time  = timer_read();
    }
}

bool rgbctl_reset_hold_progress(uint8_t *pct_out, uint8_t *profile_index_out) {
    if (!reset_hold_active) {
        return false;
    }
    uint16_t elapsed = timer_elapsed(reset_hold_start);
    uint8_t  pct      = (elapsed >= RESET_HOLD_THRESHOLD_MS) ? 255 : (uint8_t)(((uint32_t)elapsed * 255) / RESET_HOLD_THRESHOLD_MS);
    if (pct_out) {
        *pct_out = pct;
    }
    if (profile_index_out) {
        *profile_index_out = reset_hold_profile;
    }
    return true;
}

static RGB reset_progress_color(uint8_t pct) {
    uint8_t g = 90 - (uint8_t)(((uint16_t)90 * pct) / 255);
    return (RGB){.r = 255, .g = g, .b = 0};
}

void rgbctl_render_reset_bar(uint8_t led_min, uint8_t led_max) {
    if (reset_hold_active) {
        uint8_t pct, profile_index;
        rgbctl_reset_hold_progress(&pct, &profile_index);
        draw_level_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, pct, reset_progress_color(pct), 255);
        RGB_MATRIX_INDICATOR_SET_COLOR(RGBCTL_BAR_LED_HIGH, 255, 0, 0); // Esc
        uint8_t fled = profile_key_led(profile_index);
        RGB_MATRIX_INDICATOR_SET_COLOR(fled, 255, 0, 0);
    } else if (reset_cancelling) {
        uint16_t elapsed = timer_elapsed(reset_cancel_time);
        if (elapsed >= RESET_CANCEL_FADE_MS) {
            reset_cancelling = false;
            return;
        }
        uint8_t fade_pct = 255 - (uint8_t)(((uint32_t)elapsed * 255) / RESET_CANCEL_FADE_MS);
        draw_level_bar(led_min, led_max, RGBCTL_BAR_LED_HIGH, RGBCTL_BAR_LED_COUNT, reset_cancel_pct, reset_progress_color(reset_cancel_pct), fade_pct);
    }
}

// Startup: populate the in-RAM working copy from EEPROM and apply whichever profile was active
// at the last save. Fifth pass: rgbctl_load_profile() now loads everything a profile owns in one
// call (see its own comment above) -- there's no longer a second, separate shared-block read to
// do after it.
void keyboard_post_init_user(void) {
    if (!rgb_profiles_layout_ok()) {
        rgb_profiles_reset_all_to_defaults(); // stale/old-layout EEPROM -- see rgb_profiles.h
    }
    for (uint8_t i = 0; i < RGB_PROFILE_COUNT; i++) {
        rgb_profiles_read(i, &profiles[i]);
    }
    rgbctl_load_profile(rgb_profiles_read_active_index());
}
