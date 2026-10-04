#pragma once

#include QMK_KEYBOARD_H

// The Fn+\ tap/hold state machine, the RGB Control layer's (internally still QMK layer 2 --
// see the note above its LAYOUT() in keymap.c for why this project keeps that numbering even
// though the spec's own prose calls it "Layer 3") action handlers, the parameter-visibility bar,
// the per-effect color-target/slot system, and the 4-profile store interface (backed by
// features/rgb_profiles.c).

// --- Fn+\ hold detector ----------------------------------------------------------------------
// Called from keymap.c's process_record_user for the \ position (Layer 1), on both press and
// release. Layer 2 activation itself happens from rgbctl_task() (matrix_scan_user, keymap.c),
// not here -- it has to fire at the 180ms mark while the key is still *held*, which a press/
// release-only event handler can't do on its own.
void rgbctl_hold_key_event(bool pressed);
void rgbctl_task(void); // poll for held-key thresholds AND held-adjustment-key auto-repeat
                          // (spec update sec 23); matrix_scan_user must call this every scan

// True while a hold session has actually entered Layer 2 (i.e. layer_on(RGBCTL_LAYER) has been
// called and not yet matched by layer_off()). Lets keymap.c close Layer 2 by *position* if Fn is
// released before \ -- QMK resolves a held key's release against whatever's active *right then*,
// and Layer 2's own entry at the \ position is KC_NO, so without this the release would silently
// fail to match `case RGBCTL_HOLD` and Layer 2 would stay on forever (the same class of bug as
// the Fn+RAlt Options-layer one keymap.c also fixes, and for the same underlying QMK reason: a
// momentary-style layer key must not depend on a *different*, higher, layer being active at
// release time to still resolve to itself).
bool rgbctl_hold_layer_is_active(void);

// --- Fn+Enter DFU hold -----------------------------------------------------------------------
// Same event-capture pattern as rgbctl_hold_key_event() above, at the Enter position instead of
// \ -- keymap.c's process_record_user calls this on press/release; rgbctl_task() (same poll as
// above) handles both the 5000ms threshold and noticing an early Fn release.
void rgbctl_dfu_key_event(bool pressed);

// --- Layer 2 actions -----------------------------------------------------------------------
// Each of these performs the action (via QMK's stock _noeeprom RGB Matrix calls when the current
// color target is Base, see below) and marks the session dirty for the save-on-release logic,
// except rgbctl_load_profile() -- loading isn't a change to save, it's the opposite.
void rgbctl_adjust_hue(bool increase);
void rgbctl_adjust_sat(bool increase);
// Target-aware, same as hue/sat: if the currently selected color target is RGBCTL_TARGET_BASE,
// adjusts the live board-global value exactly as before; otherwise adjusts *that target's own
// stored* .v and leaves every other color's brightness untouched (spec sec 26/33).
void rgbctl_adjust_val(bool increase);
void rgbctl_adjust_speed(bool increase);
void rgbctl_step_effect(bool next);
void rgbctl_solid_mode(void);
void rgbctl_toggle(void);
void rgbctl_load_profile(uint8_t profile);   // profile: 0-3
void rgbctl_reset_profile(void);             // active profile -> factory default (destructive --
                                              // see the safety-delay gesture below; nothing calls
                                              // this directly on a bare keypress any more)

// Spec update (2026-09) sec 23: every adjustment above should auto-repeat while its key is held,
// rather than firing exactly once per press the way a keycode that's never actually sent as HID
// otherwise would (OS-level key-repeat never kicks in for a keycode that's suppressed with
// `return false` and never reaches the host at all). One shared repeat engine, driven from
// rgbctl_task(): call rgbctl_repeat_key_event() with a concrete no-argument wrapper function on
// press/release; it fires the action immediately once (matching the previous, non-repeating
// behavior for a quick tap) and then, if still held past an initial delay, keeps firing it at a
// fixed interval until released. `fn` must be one of the RGBCTL_REPEAT_* wrappers below -- a
// second held key with a *different* fn simply takes over repeating (only one repeat stream is
// ever active at a time, matching that only one hand is realistically holding one adjustment key
// at once).
typedef void (*rgbctl_repeat_fn_t)(void);
void rgbctl_repeat_key_event(rgbctl_repeat_fn_t fn, bool pressed);
void rgbctl_hue_up(void);
void rgbctl_hue_down(void);
void rgbctl_sat_up(void);
void rgbctl_sat_down(void);
void rgbctl_val_up(void);
void rgbctl_val_down(void);
void rgbctl_speed_up(void);
void rgbctl_speed_down(void);

// --- Per-effect color targets / slots (spec sec 26/32-33; update sec 6/7/25/27/32) ------------
// Every effect the spec calls out for independent color configuration. RGBCTL_TARGET_BASE is
// the special case that isn't its own stored color at all -- it reads/writes the live global
// rgb_matrix hue/sat/val directly, so a person who never touches color-slot selection sees
// identical behavior to a board with no per-effect color system at all.
//
// RGBCTL_TARGET_SHORTCUT (Windows/Mac Ctrl+letter shortcut feedback) removed this round (spec
// update sec 19) along with the feature it belonged to.
//
// Fourth pass (2026-09): RGBCTL_TARGET_ESC added -- Custom Mode's (reactive_energy's) Escape
// cascade (rgb_effects.c's overlay_escape_sequence_all()) previously always rendered in the raw
// board base color with no color target of its own, unlike every one of its sibling special keys
// (Enter/Backspace/Delete/Space/Encoder all already had one). Inserted directly after ENCODER so
// the numbered-slot run (1=Base..7=Esc, see target_for_slot() below) stays contiguous; CAPS/FNVIZ/
// ROWWAVE shift up by one enum value accordingly, which is harmless -- nothing outside this file
// and rgb_profiles.c (which derives its own storage count from RGBCTL_TARGET_COUNT, not a
// hardcoded copy of it) depends on their absolute numeric values. RGBCTL_TARGET_RIPPLE/_COMET
// added at the end for two of the three new standalone modes this pass also added, each getting
// its own single configurable accent color the same way Row Wave already does (the third,
// Equalizer, was removed again in this pass's own follow-up round -- see rgb_matrix_user.inc).
//
// Fifth pass (2026-09): RGBCTL_TARGET_ORB added -- Fairy Orb previously had no target of its own
// at all (fell through to RGBCTL_TARGET_BASE, sharing Darkening's and Rainbow Wave (R->L)'s color/
// brightness/speed), which is exactly what the person's "per mode" settings request and the same
// pass's own Fairy Orb redesign (now independently speed-adjustable, custom_rgb.c) both need it
// not to do. Darkening and Rainbow Wave (R->L) deliberately keep sharing Base -- their whole
// visual identity *is* "reflect the board's own base color/speed", not an accent of their own --
// so only Orb gets a new target here.
typedef enum {
    RGBCTL_TARGET_BASE = 0, // the board's main color/animation -- not a stored slot, see above
    RGBCTL_TARGET_ENTER,
    RGBCTL_TARGET_BACKSPACE,
    RGBCTL_TARGET_DELETE,
    RGBCTL_TARGET_SPACE,
    // (Sixth pass: RGBCTL_TARGET_ENCODER removed together with the encoder wave animation.)
    RGBCTL_TARGET_ESC,
    RGBCTL_TARGET_CAPS,
    RGBCTL_TARGET_FNVIZ,
    RGBCTL_TARGET_ROWWAVE,
    RGBCTL_TARGET_RIPPLE,
    RGBCTL_TARGET_COMET,
    RGBCTL_TARGET_ORB,
    RGBCTL_TARGET_COUNT,
} rgbctl_color_target_t;

// Sec 32: "an effect should only expose/use as many color slots as it actually supports" -- the
// set of valid *numbered* slots is a function of whichever rgb_matrix mode is currently active.
//
// Spec update (2026-09) sec 6/7/25: Caps Lock and the Fn/RGB indicator color moved *off* the
// number row entirely, onto their own dedicated keys (rgbctl_select_caps_target()/
// rgbctl_select_fnviz_target() below) -- both indicators render in every RGB mode (they're drawn
// by features/indicators.c's mode-independent overlay, not by whichever effect happens to be
// active), so tying their color access to "is reactive_energy currently the active mode" the way
// the number row still does for Enter/Backspace/etc never made much sense, and the previous
// number-row scheme for them was one of the sources of "the current implementation is confusing
// and visually incorrect" sec 6 describes. They're reachable regardless of which numbered-slot
// mode is active.
//
// - reactive_energy active: 6 numbered slots. 1=Base (the normal reactive glow's own color),
//   2=Enter, 3=Backspace, 4=Delete, 5=Space, 6=Esc. 7-9/0 are not used by anything. (Sixth pass:
//   the old slot 6 -- the encoder wave -- was removed and Esc moved down from 7 to 6.)
// - Row Wave / Ripple Pool / Comet Trail / Fairy Orb active (fourth pass: Ripple Pool/Comet Trail
//   are standalone modes added that pass, same treatment Row Wave already had; fifth pass: Fairy
//   Orb joins them now that it has a target of its own, RGBCTL_TARGET_ORB, instead of falling
//   through to Base): 1 numbered slot, that mode's own single accent color/speed -- none of these
//   four have a separate "resting" base color to speak of (all are black/off at rest, or -- Orb --
//   simply don't share Base's identity the way Darkening/Rainbow Wave (R->L) intentionally do), so
//   slot 1 *is* the accent, not RGBCTL_TARGET_BASE.
// - every other mode (Darkening, Rainbow Wave (R->L), Solid Color, any stock effect, etc.): 1
//   numbered slot, always RGBCTL_TARGET_BASE.
uint8_t                rgbctl_slot_count_for_active_mode(void);
bool                    rgbctl_slot_is_valid(uint8_t slot); // slot: 1-based
void                    rgbctl_select_color_slot(uint8_t slot); // no-op if !rgbctl_slot_is_valid(slot)
uint8_t                 rgbctl_current_slot(void); // 1-based numbered slot, meaningful only when
                                                     // rgbctl_current_target() isn't CAPS/FNVIZ
rgbctl_color_target_t   rgbctl_current_target(void); // numbered slot, or CAPS/FNVIZ if one of the
                                                        // two dedicated keys below was pressed
                                                        // more recently than any numbered slot

// Spec update sec 7: Caps Lock itself, on Layer 2, selects RGBCTL_TARGET_CAPS as the current
// color target (so hue/sat/val controls immediately start affecting Caps Lock's own color), and
// also ensures the Caps Lock indicator is enabled (selecting it to edit implies wanting to see
// it). Pressing it *again* while it's already the selected target instead turns the Caps Lock
// indicator OFF and deselects it (sixth pass); a third press re-selects and re-enables it, so
// repeated presses cycle configure -> off -> configure -> ... on the same physical key.
void rgbctl_select_caps_target(void);
// Spec update sec 25/27/32: selects RGBCTL_TARGET_FNVIZ (the Fn-help indicator color). Sixth pass:
// only the Tilde (~) key is bound to this on Layer 2 now -- Alt and Enter used to be redundant extra
// entry points and were unbound, so the color is configured solely with Tilde.
void rgbctl_select_fnviz_target(void);

HSV rgbctl_get_effect_color(rgbctl_color_target_t target); // .v is that target's own stored
                                                              // brightness (sec 26) -- callers
                                                              // still layer their own falloff/
                                                              // temporal fade on top of it, same
                                                              // as before, just scaled from this
                                                              // value now instead of a hardcoded
                                                              // 255.
// Fifth pass: every target's own independently stored/adjustable animation speed (the person's
// own explicit request -- "individual animation speed... using the number keys", plus "for each
// mode" persistence more generally). For RGBCTL_TARGET_BASE specifically this mirrors (and
// rgbctl_adjust_speed() below keeps in sync with) the *real* live rgb_matrix_get_speed(), since
// every stock QMK effect -- and Darkening/Rainbow Wave (R->L), which deliberately share Base's
// identity -- can only ever read that one true global, not this project's own per-target storage;
// every other target is authoritative in its own right, read by that target's own effect/overlay
// instead of the global (see rgb_effects.c).
uint8_t rgbctl_get_effect_speed(rgbctl_color_target_t target);
// For indicators.c: the color that slot N (1-based) currently represents, regardless of whether
// it's the *selected* slot -- i.e. what that number key's own LED should show while lit.
// rgbctl_slot_is_valid() first to know whether it should be lit at all.
HSV rgbctl_get_slot_color(uint8_t slot);

// Raw setter used only by features/rgb_profiles.c when loading a profile/factory default into
// RAM -- everything else should go through rgbctl_adjust_hue()/_sat()/_val()/_speed() so the bar/
// session-dirty bookkeeping stays correct. Fifth pass: takes speed too now (was color-only) --
// one call loads a target's complete stored state atomically, rather than needing a second call
// that could theoretically land the in-RAM state between the two on a corrupted/interrupted read.
void rgbctl_set_effect_target_raw(rgbctl_color_target_t target, uint8_t hue, uint8_t sat, uint8_t val, uint8_t speed);

// Caps Lock indicator on/off -- independent of its color (RGBCTL_TARGET_CAPS above). Still a
// plain query/toggle pair for indicators.c/anything else that just needs the on/off state without
// going through target selection.
bool rgbctl_caps_indicator_enabled(void);
void rgbctl_caps_indicator_toggle(void);

// True while the Mac layer (persistent, entered/left via the Options layer's F2/F1/F3 TO() keys,
// or directly via Fn+M on Layer 1 -- spec update sec 10) is active. Single source of truth shared
// by indicators.c's/ansi.c's Mac-LED housekeeping.
bool rgbctl_is_mac_mode(void);

// Cleaning / Input Lock Mode (Fn+Esc). State lives here (not keymap.c) purely so
// features/indicators.c -- which already includes this header -- can show it without a new
// cross-file include; the actual input-blocking logic is keymap.c's process_record_user, since
// that's the only place with access to every key event before anything else sees it. Defaults to
// off and is never written to EEPROM/profiles -- a plain RAM bool already satisfies both by
// construction.
bool rgbctl_cleaning_mode_active(void);
void rgbctl_cleaning_mode_toggle(void);

// --- Profile selection (spec sec 29) -----------------------------------------------------------
// F5-F8 (RGBCTL_PROFILE_1..4 keycodes) call rgbctl_load_profile() directly, unchanged. Home is
// the general "profile selection" key sec 29 names separately from the four direct slots: cycles
// to the next profile in sequence (0->1->2->3->0...) without needing to remember which F-key is
// which.
void rgbctl_profile_cycle_next(void);
uint8_t rgbctl_active_profile(void); // 0-3, for indicators.c to know which F5-F8 key to highlight

// --- Destructive reset gesture (spec sec 38/42; update sec 30) --------------------------------
// "Esc + the currently selected profile", held for RGBCTL_RESET_HOLD_MS (5000ms as of the spec
// update -- was 2000ms) -- releasing either key early cancels with no effect; holding to
// completion resets that profile (whichever F-key was held, matching "the currently selected
// profile" since pressing an F-key is also how a profile is selected -- see rgbctl_load_profile
// above) to its factory default. `is_esc`=true/pressed reports Esc's own down/up state;
// `is_esc`=false reports one of the four profile keys' down/up state via `profile_index` (0-3).
// Called from keymap.c's process_record_user by physical position, same pattern as
// rgbctl_hold_key_event()/rgbctl_dfu_key_event() above (needed for reliable press/release
// tracking regardless of what Layer 2's own table says at that position).
void rgbctl_reset_key_event(bool is_esc, uint8_t profile_index, bool pressed);
// For indicators.c: is a reset hold currently counting down, and how far along (0-255) is it --
// used to draw the same progress-bar primitive the DFU hold already uses, in red, plus flash
// Esc + the qualifying F-key red for the duration. Returns false if no reset hold is in progress.
bool rgbctl_reset_hold_progress(uint8_t *pct_out, uint8_t *profile_index_out);
// Self-contained render (progress bar + Esc/qualifying-F-key red), same shape as
// rgbctl_render_dfu_bar() below -- call after rgbctl_render_bar()/the Layer 2 indicator overlay
// so the red destructive state visually wins over that key's normal profile-indicator color.
void rgbctl_render_reset_bar(uint8_t led_min, uint8_t led_max);

// --- Parameter bar wiring (consuming rgb_effects.h's renderers) -------------------------------
// Called from features/indicators.c's existing rgb_matrix_indicators_advanced_user() hook. No-op
// if no bar is currently visible.
void rgbctl_render_bar(uint8_t led_min, uint8_t led_max);

// Same reasoning, for the DFU hold-progress bar, reusing the same renderer. Rendered after
// rgbctl_render_bar() so DFU visually takes precedence in the near-impossible case both were
// somehow active at once (requires physically holding Fn+\+Enter all together).
void rgbctl_render_dfu_bar(uint8_t led_min, uint8_t led_max);
