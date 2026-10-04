#include "indicators.h"
#include "indicator_queue.h"
#include "custom_rgb.h"
#include "rgb_control.h"
#include QMK_KEYBOARD_H
#include <lib/lib8tion/lib8tion.h> // qadd8/scale8 for the value/saturation/speed indicator
                                     // intensity scaling below -- not pulled in transitively.

// Defined in keymap.c (both ansi and iso via keymaps) -- see that file for why it's the one real
// definition and this the only extern declaration outside it.
extern uint8_t is_orgb_mode;
extern uint8_t is_srgb_mode;

// Confirmation-blink helpers -- unrelated to the advanced-indicators overlay below. blink_arrows/
// blink_space are reachable via features/rgb_keys.c's legacy stock-RGB-keycode handling (kept for
// VIA remap flexibility, see HANDOFF.md); blink_NKRO is called directly from keymap.c's
// QK_MAGIC_TOGGLE_NKRO case.
void blink_arrows(void) {
    indicator_enqueue(2, 200, 3, RGB_WHITE);  // left
    indicator_enqueue(3, 200, 3, RGB_WHITE);  // down
    indicator_enqueue(63, 200, 3, RGB_WHITE); // up
    indicator_enqueue(4, 200, 3, RGB_WHITE);  // right
}

void blink_space(bool isEnabling) {
    (void)isEnabling;
    indicator_enqueue(79, 200, 3, RGB_WHITE);
}

void blink_NKRO(bool isEnabling) {
    static const uint8_t led_indexes[] = {48, 47, 46, 45, 44, 43, 42, 41, 40, 39}; // Q W E R T Y U I O P
    if (isEnabling) {
        for (uint8_t i = 0; i < sizeof(led_indexes); i++) {
            indicator_enqueue(led_indexes[i], 200, 3, RGB_WHITE);
        }
    }
}

// --- Caps Lock indicator ---------------------------------------------------------------------
#define CAPS_LOCK_LED 50

// Sixth pass -- Caps Lock latency fix. Two things used to delay the LED after pressing Caps Lock:
//   1. The "just turned on" transition enqueued a 200 ms indicator-queue entry. That queue draws the
//      COMPLEMENTARY color for the first interval (see indicator_queue.c) and only then the real
//      color, on top of the steady-state draw -- so the real color showed up 200 ms late, and for a
//      white Caps color the complement is black, i.e. the LED looked dead for 200 ms. That pulse is
//      removed; the steady-state draw below is all there is now.
//   2. The state only changed once the HOST echoed its lock-LED report back (host_keyboard_led_state()),
//      which takes a variable few-to-tens of milliseconds depending on the OS/USB polling.
//      indicators_caps_key_pressed() (called from the keymap on a real Caps Lock keypress) now
//      predicts the new state immediately; the prediction is dropped as soon as the host agrees with
//      it, or after CAPS_PREDICT_TIMEOUT_MS if it never does (e.g. the OS ignores Caps Lock), so the
//      host stays the source of truth and the LED can't get stuck out of sync.
#define CAPS_PREDICT_TIMEOUT_MS 250

static bool     caps_predict_active = false;
static bool     caps_predicted_on   = false;
static uint16_t caps_predict_time   = 0;

// The Caps Lock state the indicator should show right now (prediction if one is pending, else host).
static bool caps_lock_effective_on(void) {
    bool host_on = host_keyboard_led_state().caps_lock;
    if (!caps_predict_active) {
        return host_on;
    }
    if (host_on == caps_predicted_on || timer_elapsed(caps_predict_time) >= CAPS_PREDICT_TIMEOUT_MS) {
        caps_predict_active = false; // host caught up (or never will) -- trust it again
        return host_on;
    }
    return caps_predicted_on;
}

void indicators_caps_key_pressed(void) {
    // Flip whatever is currently displayed, so two quick presses toggle twice even if the host's
    // report for the first one hasn't arrived yet.
    caps_predicted_on   = !caps_lock_effective_on();
    caps_predict_active = true;
    caps_predict_time   = timer_read();
}

static void update_caps_lock_indicator(uint8_t led_min, uint8_t led_max) {
    // Evaluated first, every frame, so an expired prediction is always retired promptly even while
    // the indicator is disabled or the RGB config layer is up (no stale prediction can linger).
    bool caps_lock_is_on = caps_lock_effective_on();

    if (!rgbctl_caps_indicator_enabled()) {
        return;
    }
    // On the RGB config layer the Caps key's LED is that layer's own control (selected color / white /
    // dim gray, see render_rgbctl_layer()), so the live Caps Lock state must not paint over it.
    if (IS_LAYER_ON(2)) {
        return;
    }

    if (caps_lock_is_on) {
        RGB rgb = hsv_to_rgb(rgbctl_get_effect_color(RGBCTL_TARGET_CAPS));
        RGB_MATRIX_INDICATOR_SET_COLOR(CAPS_LOCK_LED, rgb.r, rgb.g, rgb.b);
    }
}

// --- Fn-help highlight ---------------------------------------------------------------------
// Every key that has an actual, usable function while Layer 1 (Fn) is held: F9-F12, Backspace,
// Delete, Home, Win, M (direct Mac-mode switch, spec update sec 10), plus \ (hold for the RGB
// Settings layer), Enter (hold for DFU), and RAlt (hold for the Options layer -- moved off
// RShift, spec update sec 8). F1-F8 must not be shown.
//
// Spec update (2026-09) sec 16/17/41: this is now a pure *additive* overlay -- it no longer
// blanks the board to black first the way Layer 2/3/5 still do below. Previously, the instant Fn
// was held past the delay below, the whole board went black and only these ~11 LEDs relit,
// exactly like Layer 2/3/5 -- which is indistinguishable, from the *rendering* side, from briefly
// replacing whatever RGB mode was actually running (reactive_energy, Row Wave, a stock effect,
// anything) with nothing. The mode's own render function was never actually paused or reset by
// this -- QMK calls it every frame regardless of what this indicator hook does on top -- but
// hiding its output for as long as Fn was held, then suddenly revealing however far it had
// silently continued once Fn let go, reads exactly like "the animation reset" even though nothing
// about its internal state ever did. Sec 41 names the general principle this now follows
// directly: "underlying animation state + temporary UI indicators, rather than replace the
// animation with indicators." Layer 2/3/5 keep blanking on purpose -- sec 34/35's "unrelated key
// -> OFF" is a *different*, explicit requirement for those three specifically, which pure overlay
// can't enforce (there'd be no way to tell "unrelated, should be off" apart from "whatever the
// live animation put there").
static bool fn_help_should_render(void) {
    return IS_LAYER_ON(1) && !IS_LAYER_ON(2) && !IS_LAYER_ON(3) && !IS_LAYER_ON(5);
}

// Fourth pass (2026-09): RAlt (LED 0) removed from this list -- reported as "remove the backlight
// highlight from the Alt key when Fn is pressed". RAlt is the only "Alt" this list ever included
// (Fn+RAlt held opens the Options layer, sec 27 -- LAlt is unrelated: it's one of the three keys
// that pick the Fn-indicator's own color on the *RGB Settings* layer, a completely different
// context covered by fnviz_key_leds below, not this Fn-held overlay, and the request was
// specifically about the highlight while Fn is held). 12 -> 11.
// Fifth pass (2026-09): Esc (LED 21) added -- `Fn`+`Esc` toggles Cleaning Mode (README §17), a
// real Fn-function that simply had no highlight, the same gap M had before the second pass added
// it. Checked against every other thing that draws Esc (LED 21) before adding it, since the
// person specifically asked for no visual artifacts with Esc appearing where it shouldn't:
//   - Cleaning Mode's own red Esc (render_cleaning_mode(), below): rgb_matrix_indicators_advanced_
//     user() returns immediately after drawing it, so render_fn_help() never runs at all while
//     Cleaning Mode is active -- no overlap possible, and toggling it on/off via this very
//     highlighted key hands off cleanly frame to frame (Fn-help vanishes as Esc goes red).
//   - The reset-hold Esc/F-key red (rgbctl_render_reset_bar()): only ever happens on Layer 2,
//     where fn_help_should_render() is false by definition, and is drawn last regardless.
//   - Layer 2/3/5's own rendering: all blank the board first and fn_help_should_render() excludes
//     them, so this list can only ever render on Layer 1 alone -- it cannot leak into any other
//     mode or layer.
// 11 -> 12.
#define FN_HELP_LED_COUNT 12
static const uint8_t fn_help_leds[FN_HELP_LED_COUNT] = {21, 12, 11, 10, 9, 35, 8, 7, 77, 36, 62, 68}; // Esc F9 F10 F11 F12 Backspace Delete Home Win \ Enter M
#define FN_HELP_DELAY_MS 150

static void render_fn_help(uint8_t led_min, uint8_t led_max) {
    static bool     layer1_was_on = false;
    static uint16_t layer1_on_at  = 0;

    bool layer1_is_on = IS_LAYER_ON(1);
    if (layer1_is_on && !layer1_was_on) {
        layer1_on_at = timer_read();
    }
    layer1_was_on = layer1_is_on;
    if (!fn_help_should_render() || timer_elapsed(layer1_on_at) < FN_HELP_DELAY_MS) {
        return;
    }

    HSV hsv = rgbctl_get_effect_color(RGBCTL_TARGET_FNVIZ);
    RGB rgb  = hsv_to_rgb(hsv);
    for (uint8_t i = 0; i < FN_HELP_LED_COUNT; i++) {
        RGB_MATRIX_INDICATOR_SET_COLOR(fn_help_leds[i], rgb.r, rgb.g, rgb.b);
    }
}

// --- Layer 2 (RGB Control / "RGB Settings") -----------------------------------------------------
// Every lit key is one of exactly three things (spec sec 35): an actual interactive control (its
// own appropriate/current-state color), purely informational (WHITE), or destructive (RED, with a
// safety delay -- see rgbctl_render_reset_bar()). A key never lights up just because it does
// something on another layer or in another RGB mode's context (sec 34/36).
//
// Spec update (2026-09) sec 6/20/24: color-slot selection no longer flashes -- selection is
// entirely persistent state now. The *selected* slot shows its own actual color; every other
// available slot shows plain WHITE instead of a dimmed version of its own color, so the selected
// one is unambiguous at a glance without needing brightness contrast to carry the distinction
// (sec 24's own worked example: "Color 2 selected and configured as blue: key 2 = blue, other
// color-selection keys = white"). The same selected-color/white-otherwise language now also
// covers Caps Lock and the Fn/RGB-indicator keys (sec 6/7/25/27/32), which moved off the number
// row entirely onto their own dedicated keys -- see rgb_control.h's rgbctl_select_caps_target()/
// rgbctl_select_fnviz_target(). L/K (sec 28) and `/` (sec 31) are removed outright, no longer
// bound to anything on this layer's keymap.c table, so there is nothing left to indicate for them.
static const uint8_t slot_leds[6] = {23, 24, 25, 26, 27, 28}; // number-row LEDs for slots
                                                                      // 1-6 (sixth pass: the old slot 6
                                                                      // -- encoder wave -- was removed,
                                                                      // so Esc moved from key 7 to key 6)
                                                                      // --
                                                                      // reactive_energy is the
                                                                      // only mode that ever exposes
                                                                      // more than 1
// F5-F8 LEDs, in profile order -- matches rgb_control.c's own profile_key_led() table exactly.
static const uint8_t profile_leds[4] = {16, 15, 14, 13};
// Sixth pass: ~ (grave) alone selects the Fn/RGB indicator color. Alt and Enter used to select it too
// (three redundant keys); both were unbound so the color is configured solely with Tilde.
#define FNVIZ_KEY_LED 22 // ` (tilde/grave)

static void render_rgbctl_layer(uint8_t led_min, uint8_t led_max) {
    rgbctl_color_target_t current = rgbctl_current_target();

    // --- Color slots (sec 32/33): number row 1-6 while reactive_energy is active, only slot 1
    // otherwise. Selected = that slot's actual color; every other available slot = white.
    uint8_t slot_count    = rgbctl_slot_count_for_active_mode();
    uint8_t selected_slot = rgbctl_current_slot();
    for (uint8_t s = 1; s <= slot_count; s++) {
        RGB rgb;
        if (s == selected_slot) {
            rgb = hsv_to_rgb(rgbctl_get_slot_color(s));
        } else {
            rgb = (RGB){RGB_WHITE};
        }
        RGB_MATRIX_INDICATOR_SET_COLOR(slot_leds[s - 1], rgb.r, rgb.g, rgb.b);
    }

    // --- Caps Lock color target (sec 7): disabled -> dim gray (checked FIRST, sixth pass: a disabled
    // indicator used to keep showing its color while still selected, so the second press looked like
    // it did nothing); enabled + selected -> its actual color; enabled + not selected -> white.
    if (!rgbctl_caps_indicator_enabled()) {
        RGB_MATRIX_INDICATOR_SET_COLOR(50, 60, 60, 60);
    } else if (current == RGBCTL_TARGET_CAPS) {
        RGB rgb = hsv_to_rgb(rgbctl_get_effect_color(RGBCTL_TARGET_CAPS));
        RGB_MATRIX_INDICATOR_SET_COLOR(50, rgb.r, rgb.g, rgb.b);
    } else {
        RGB_MATRIX_INDICATOR_SET_COLOR(50, 255, 255, 255);
    }

    // --- Fn/RGB indicator color target (sec 25/27/32): Tilde only (sixth pass) -- selected -> its
    // actual color; not selected -> white.
    RGB fnviz_rgb;
    if (current == RGBCTL_TARGET_FNVIZ) {
        fnviz_rgb = hsv_to_rgb(rgbctl_get_effect_color(RGBCTL_TARGET_FNVIZ));
    } else {
        fnviz_rgb = (RGB){RGB_WHITE};
    }
    RGB_MATRIX_INDICATOR_SET_COLOR(FNVIZ_KEY_LED, fnviz_rgb.r, fnviz_rgb.g, fnviz_rgb.b);

    // --- Hue adjust (sec 22/34): -/= -- previews the *selected* target's own current hue, full
    // saturation so it reads clearly regardless of that color's own stored saturation/brightness.
    HSV target_hsv = rgbctl_get_effect_color(current);
    RGB hue_rgb     = hsv_to_rgb((HSV){target_hsv.h, 255, 220});
    RGB_MATRIX_INDICATOR_SET_COLOR(33, hue_rgb.r, hue_rgb.g, hue_rgb.b); // -
    RGB_MATRIX_INDICATOR_SET_COLOR(34, hue_rgb.r, hue_rgb.g, hue_rgb.b); // =

    // --- Profile selection (sec 29): Home = informational "cycle profile"; F5-F8 = direct
    // selection, active one shown brighter than the other three.
    RGB_MATRIX_INDICATOR_SET_COLOR(7, 255, 255, 255); // Home -- informational (white)
    uint8_t active_profile = rgbctl_active_profile();
    for (uint8_t p = 0; p < 4; p++) {
        uint8_t v = (p == active_profile) ? 235 : 70;
        RGB_MATRIX_INDICATOR_SET_COLOR(profile_leds[p], v, v, v);
    }

    // --- Mode select: [ / ] ---------------------------------------------------------------------
    RGB_MATRIX_INDICATOR_SET_COLOR(38, 150, 90, 220); // [ -- violet
    RGB_MATRIX_INDICATOR_SET_COLOR(37, 150, 90, 220); // ]

    // --- Saturation (sec 21/22: moved to ,/. -- the unshifted/shifted </> keys -- replacing the
    // brightness duplicate that used to live there; K/L are removed, sec 28). Sec 24/29: scaled
    // by the *actual* configured saturation of the current target, same as hue's live preview
    // above, rather than a fixed color -- a low-saturation target reads as a dim, washed-out gray
    // here, a fully-saturated one as a crisp white, so the control visibly reflects the state it
    // would change.
    uint8_t sat_intensity = target_hsv.s; // 0 (least saturated) still shown as a visible dim gray,
                                            // not fully off, via the floor add below -- sec 24's
                                            // "always immediately communicate ... current color"
    uint8_t sat_shown      = qadd8(sat_intensity, 40); // small floor so "0 sat" still reads as a
                                                          // dim key, not OFF (saturating add)
    RGB_MATRIX_INDICATOR_SET_COLOR(66, sat_shown, sat_shown, sat_shown); // . (>)
    RGB_MATRIX_INDICATOR_SET_COLOR(67, sat_shown, sat_shown, sat_shown); // , (<)

    // --- Brightness (sec 29): Up/Down -- scaled by the current target's actual brightness, for
    // the same reason saturation is above (fixes sec 29's "brightness level ... not represented
    // correctly on the Up Arrow" directly: previously this was a fixed white regardless of state).
    uint8_t val_shown = qadd8(target_hsv.v, 20);
    RGB_MATRIX_INDICATOR_SET_COLOR(63, val_shown, val_shown, val_shown); // Up
    RGB_MATRIX_INDICATOR_SET_COLOR(3, val_shown, val_shown, val_shown);  // Down

    // --- Speed: Left/Right -- fourth pass: reported as not matching the rest of the board (along
    // with `\` below) -- this used to be a fixed decorative "sky blue" (0, scale8(v,160), v)
    // regardless of the configured color, the only one of the four adjustable-parameter controls
    // (saturation/brightness/speed) not using the shared white-family "intensity readout" style
    // the other two already use just above. Switched to that same style for consistency: white,
    // scaled by the board's actual configured speed, exactly like Up/Down/,/. already are.
    // Fifth pass: reads the *selected target's* own speed now (rgbctl_get_effect_speed(), which is
    // Base's real live global for Base and this target's own stored value for everything else) --
    // previously always the one global regardless of what was selected, which would have shown
    // the wrong number the moment a non-Base target's speed became independently adjustable.
    uint8_t speed_shown = qadd8(rgbctl_get_effect_speed(rgbctl_current_target()), 20);
    RGB_MATRIX_INDICATOR_SET_COLOR(2, speed_shown, speed_shown, speed_shown); // Left
    RGB_MATRIX_INDICATOR_SET_COLOR(4, speed_shown, speed_shown, speed_shown); // Right

    // --- \  (fourth pass) ------------------------------------------------------------------------
    // Reported as not matching the rest of the board: this is the layer's own hold-to-stay-here
    // key (RGBCTL_HOLD, keymap.c) and had no indicator color at all before this pass -- meaning it
    // sat OFF the entire time the layer is active, next to over a dozen other lit controls. Lit
    // plain informational white, the same treatment Home already gets above, since (like Home)
    // it isn't itself a color-bearing control -- it's "you're here".
    RGB_MATRIX_INDICATOR_SET_COLOR(36, 255, 255, 255);

    // --- RGB on/off toggle: Space (sec 31: `/` removed, no longer a duplicate) ------------------
    RGB_MATRIX_INDICATOR_SET_COLOR(79, 230, 180, 20); // Space -- amber

    // Esc and Delete are both deliberately left untouched here (Delete stays OFF on this layer;
    // Esc only lights up -- red -- while an actual reset hold is in progress, drawn by
    // rgbctl_render_reset_bar() below). Every other LED not mentioned above is genuinely
    // unrelated on this layer (sec 34/36) and stays OFF via the caller's clear-to-black.
}

// --- Layer 3 (Options) indicator -- unchanged, except its entry key moved off RShift (sec 8) --
// nothing here needed to change, this layer's own indicators don't depend on which physical key
// was held to reach it.
static void render_options_layer(uint8_t led_min, uint8_t led_max) {
    RGB_MATRIX_INDICATOR_SET_COLOR(48, 0xFF, 0xFF, 0x00); // Q = reset
    RGB_MATRIX_INDICATOR_SET_COLOR(44, 0xFF, 0xA5, 0x00); // T = SnapTap toggle
    RGB_MATRIX_INDICATOR_SET_COLOR(74, 0x80, 0x00, 0x80); // Z = clear eeprom

    // OpenRGB (O) / SignalRGB (S) mode indicator -- whichever (if either) is active takes over
    // both keys' colors so the pair always reads as one shared mode indicator, matching how
    // keymap.c's SWITCH_MODE/SIGNAL_MODE cases already treat the two as mutually exclusive.
    if (is_orgb_mode) {
        RGB_MATRIX_INDICATOR_SET_COLOR(40, 0x00, 0xFF, 0xFF); // O = OpenRGB active
        RGB_MATRIX_INDICATOR_SET_COLOR(52, 0x00, 0xFF, 0xFF);
    } else if (is_srgb_mode) {
        RGB_MATRIX_INDICATOR_SET_COLOR(40, 0x00, 0xFF, 0x00);
        RGB_MATRIX_INDICATOR_SET_COLOR(52, 0x00, 0xFF, 0x00); // S = SignalRGB active
    } else {
        RGB_MATRIX_INDICATOR_SET_COLOR(40, 0xFF, 0x00, 0xFF); // neither -- VIA mode
        RGB_MATRIX_INDICATOR_SET_COLOR(52, 0xFF, 0x00, 0xFF);
    }
    RGB_MATRIX_INDICATOR_SET_COLOR(69, 0xFF, 0x00, 0x00); // H = NKRO toggle
    RGB_MATRIX_INDICATOR_SET_COLOR(20, 0x80, 0x00, 0xFF); // F1 = back to Base
    RGB_MATRIX_INDICATOR_SET_COLOR(19, 0x80, 0x00, 0xFF); // F2 = to Mac
    RGB_MATRIX_INDICATOR_SET_COLOR(18, 0x80, 0x00, 0xFF); // F3 = to Num
}

// --- Layer 5 (Num) indicator -- unrelated to this spec round, unchanged -----------------------
static void render_num_layer(uint8_t led_min, uint8_t led_max) {
    static const uint8_t numpad_leds[16] = {29, 30, 31, 32, 42, 41, 40, 39, 57, 58, 59, 60, 68, 67, 66, 65};
    for (uint8_t i = 0; i < 16; i++) {
        RGB_MATRIX_INDICATOR_SET_COLOR(numpad_leds[i], 0x00, 0xFF, 0x00);
    }
    RGB_MATRIX_INDICATOR_SET_COLOR(28, 0xFF, 0x00, 0x00); // Num Lock toggle
    RGB_MATRIX_INDICATOR_SET_COLOR(20, 0x80, 0x00, 0xFF); // F1 = back to Base
}

// --- Cleaning / Input Lock Mode indicator ------------------------------------------------------
// Marks only Escape -- every other key in the protected strip goes back to whatever the active RGB
// mode/effect would otherwise show there, same as during normal typing.
//
// Sixth pass: exit hint. Esc rests red while Cleaning Mode is on. If Fn is pressed AGAIN, Esc fades
// to the Fn-help color (RGBCTL_TARGET_FNVIZ -- the same color every other Fn function key gets on
// Layer 1) to say "press Esc now to leave". Only Esc ever changes -- none of the other Fn-help keys
// light up, because render_fn_help() never runs while Cleaning Mode is active.
//   - "Again": Fn is still physically held from the Fn+Esc that turned the mode on, so the hint is
//     "armed" only after Fn has been released once since entering.
//   - Timing matches the rest of the firmware's UI backlight: the same FN_HELP_DELAY_MS wait Fn-help
//     uses before showing (a quick Fn tap doesn't flash), then a CLEAN_HINT_FADE_IN_MS crossfade
//     (same length as the RGB bar's fade), and a shorter fade back to red on release.
// The fade level is derived from timestamps (not accumulated per call), because this hook can run
// several times per frame (once per LED chunk) and the result must not depend on how often it does.
#define CLEAN_HINT_FADE_IN_MS 200
#define CLEAN_HINT_FADE_OUT_MS 120

static bool     clean_hint_armed   = false; // Fn released at least once since Cleaning Mode began
static bool     clean_fn_was_on    = false;
static uint16_t clean_fn_on_at     = 0;
static bool     clean_hint_target  = false; // is the hint currently meant to be showing
static uint8_t  clean_hint_from    = 0;     // fade level when the target last changed (0 = red, 255 = Fn color)
static uint32_t clean_hint_t0      = 0;

static void cleaning_hint_reset(void) {
    clean_hint_armed  = false;
    clean_hint_target = false;
    clean_hint_from   = 0;
    clean_hint_t0     = timer_read32();
    clean_fn_was_on   = IS_LAYER_ON(1); // Fn is normally still held right now (Fn+Esc) -- not a fresh press
    clean_fn_on_at    = timer_read();
}

// Current fade level 0-255, and (re)targets the fade if `want` changed.
static uint8_t cleaning_hint_level(bool want) {
    uint32_t now = timer_read32();
    uint32_t el  = now - clean_hint_t0;
    uint8_t  level;
    if (clean_hint_target) {
        uint32_t v = (uint32_t)clean_hint_from + (el * 255u) / CLEAN_HINT_FADE_IN_MS;
        level      = (v > 255u) ? 255 : (uint8_t)v;
    } else {
        uint32_t sub = (el * 255u) / CLEAN_HINT_FADE_OUT_MS;
        level        = (sub >= clean_hint_from) ? 0 : (uint8_t)(clean_hint_from - sub);
    }
    if (want != clean_hint_target) {
        clean_hint_from   = level;
        clean_hint_t0     = now;
        clean_hint_target = want;
    }
    return level;
}

static void render_cleaning_mode(uint8_t led_min, uint8_t led_max) {
    bool fn_on = IS_LAYER_ON(1);
    if (fn_on && !clean_fn_was_on) {
        clean_fn_on_at = timer_read();
    }
    clean_fn_was_on = fn_on;
    if (!fn_on) {
        clean_hint_armed = true; // Fn is up: the next press counts as "pressing Fn again"
    }

    bool    want  = clean_hint_armed && fn_on && timer_elapsed(clean_fn_on_at) >= FN_HELP_DELAY_MS;
    uint8_t level = cleaning_hint_level(want);

    // Crossfade red -> Fn-help color by `level`.
    RGB fn = hsv_to_rgb(rgbctl_get_effect_color(RGBCTL_TARGET_FNVIZ));
    uint8_t r = (uint8_t)(((uint16_t)255 * (255 - level) + (uint16_t)fn.r * level) / 255);
    uint8_t g = (uint8_t)(((uint16_t)0   * (255 - level) + (uint16_t)fn.g * level) / 255);
    uint8_t b = (uint8_t)(((uint16_t)0   * (255 - level) + (uint16_t)fn.b * level) / 255);
    RGB_MATRIX_INDICATOR_SET_COLOR(21, r, g, b); // Esc only
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    static bool cleaning_was_active = false;
    bool        cleaning_active     = rgbctl_cleaning_mode_active();
    if (cleaning_active && !cleaning_was_active) {
        cleaning_hint_reset(); // just entered: Fn is still held from Fn+Esc, hint must wait for a re-press
    }
    cleaning_was_active = cleaning_active;

    if (cleaning_active) {
        render_cleaning_mode(led_min, led_max);
        process_indicator_queue(led_min, led_max);
        return true;
    }

    // Layer 2/3/5 still blank to black first -- sec 34/35's "unrelated key -> OFF" is a specific,
    // explicit requirement for those three that pure-overlay rendering can't satisfy (see
    // render_fn_help()'s own comment for why Fn-help-alone no longer does this). Mac (Layer 4) is
    // deliberately excluded -- it's mostly transparent passthrough for normal typing, so blanking
    // it here would leave anyone in Mac mode with no reactive glow at all for as long as they
    // stayed on it.
    //
    // At most one of Layer 2/3/5's own content is ever drawn in a single frame (explicit
    // if/else-if precedence below, not independent ifs) -- holding e.g. Fn+\+RAlt at once can't
    // draw two of them simultaneously and partially overwrite each other.
    bool blanking_layer_active = IS_LAYER_ON(2) || IS_LAYER_ON(3) || IS_LAYER_ON(5);
    if (blanking_layer_active) {
        for (uint8_t i = led_min; i < led_max; i++) {
            RGB_MATRIX_INDICATOR_SET_COLOR(i, 0, 0, 0);
        }
    }

    if (IS_LAYER_ON(2)) {
        render_rgbctl_layer(led_min, led_max);
    } else if (IS_LAYER_ON(3)) {
        render_options_layer(led_min, led_max);
    } else if (IS_LAYER_ON(5)) {
        render_num_layer(led_min, led_max);
    } else {
        // Fn-help alone (Layer 1, none of 2/3/5): pure additive overlay, no blanking -- see its
        // own comment above render_fn_help() for why. fn_help_should_render()'s internal delay
        // timer is updated by render_fn_help() itself every time Layer 1 is on, even on frames
        // where blanking_layer_active is true and this branch doesn't run, so it stays current;
        // but this branch only runs when it isn't, so that's handled just by calling it here.
        render_fn_help(led_min, led_max);
    }
    // Keep Fn-help's own delay timer current even on frames where one of Layer 2/3/5 is what
    // actually renders above (fn_help_should_render() gates the body, so this paints nothing).
    if (IS_LAYER_ON(1) && blanking_layer_active) {
        render_fn_help(led_min, led_max);
    }

    update_caps_lock_indicator(led_min, led_max); // takes final precedence over any of the above
                                                    // that happen to also touch LED 50

    rgbctl_render_bar(led_min, led_max);       // hue/sat/val/speed adjustment bar
    rgbctl_render_dfu_bar(led_min, led_max);   // DFU-hold progress bar, drawn after so it visually
                                                 // wins in the near-impossible case both were
                                                 // somehow active at once
    rgbctl_render_reset_bar(led_min, led_max); // destructive-reset-hold progress + Esc/F-key red,
                                                 // drawn last so it visually wins over that one
                                                 // profile key's normal indicator color

    process_indicator_queue(led_min, led_max);

    return true;
}
