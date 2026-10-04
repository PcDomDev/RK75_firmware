#pragma once

#include QMK_KEYBOARD_H
#include "rgb_control.h" // rgbctl_color_target_t/RGBCTL_TARGET_COUNT -- targets[] below is sized
                           // and indexed by these directly.

// Stage 10 (FINAL_SPECIFICATION.md sec 7): EEPROM persistence for the profile store Stage 9
// built in-RAM (features/rgb_control.c). This file owns raw EEPROM access and the canonical
// profile shape/factory defaults; rgb_control.c owns the live in-RAM copy and when to read/
// write it -- same split as custom_rgb.c (state) / rgb_effects.c (behavior) elsewhere in this
// project.

// Fifth pass (2026-09): per the person's own explicit request ("all parameters -- color, speed,
// brightness -- must be saved independently for each user profile AND for each mode"), this file
// was restructured from two separate stores (a small per-profile {effect,hue,sat,val,speed} plus
// one *shared* [hue,sat,val]-per-target block used by every profile alike) into a single, fully
// per-profile shape: every profile now carries its own complete copy of every target's own
// [hue,sat,val,speed] -- Enter/Backspace/Delete/Space/Encoder/Esc/Caps/FnViz/RowWave/Ripple/Comet/
// Orb, *and* Base itself (previously handled as separate loose fields on rgb_profile_t) -- so
// switching profiles can never bleed one profile's Enter color, or Fairy Orb's speed, or anything
// else, into another's. See rgb_control.c's keyboard_post_init_user()/rgbctl_save_profile()/
// rgbctl_load_profile() for how the in-RAM working copy follows whichever profile is active.

// On-EEPROM AND in-RAM shape for one color/speed target -- one hue/sat/val/brightness plus one
// independent animation speed, per rgbctl_color_target_t entry (rgb_control.h). Widened from
// [hue,sat,val] to include speed this pass -- see rgb_control.h's own note on rgbctl_get_effect_
// speed() for why speed needed the same per-target treatment color/brightness already had.
typedef struct {
    uint8_t hue, sat, val, speed;
} rgb_target_settings_t;

// On-EEPROM AND in-RAM shape for one profile. targets[t] is target t's own complete settings,
// t=0 (RGBCTL_TARGET_BASE) included -- Base's targets[] entry *is* what used to be this struct's
// standalone hue/sat/val/speed fields, just folded into the same array as every other target
// instead of living beside it, since Base needed the exact same four fields anyway.
typedef struct {
    uint8_t                 effect_id;
    rgb_target_settings_t targets[RGBCTL_TARGET_COUNT]; // index by rgbctl_color_target_t directly
    uint8_t                 caps_enabled;
} rgb_profile_t;

#define RGB_PROFILE_COUNT 4
// config.h's EECONFIG_KB_DATA_SIZE must equal RGB_TOTAL_KB_DATA_SIZE below. A plain literal there
// rather than computed from this macro because config.h is included too broadly across the
// codebase (deep into quantum/ core files that have no reason to know about features/) to safely
// depend on a features/ header -- rgb_profiles.c below _Static_assert-checks the two stay in sync
// instead.
// +1: active index, +1: layout version byte (fifth pass -- see rgb_profiles_layout_ok() below)
#define RGB_PROFILES_KB_DATA_SIZE ((uint16_t)(sizeof(rgb_profile_t) * RGB_PROFILE_COUNT + 2))

// Raw per-profile EEPROM access. index: 0-3.
void rgb_profiles_read(uint8_t index, rgb_profile_t *out);
void rgb_profiles_write(uint8_t index, const rgb_profile_t *in);

// Sec 7.2's "last active profile index persisted" across power cycles.
uint8_t rgb_profiles_read_active_index(void);
void rgb_profiles_write_active_index(uint8_t index);

// Sec 7.2's exact factory defaults (Profile 1 Solid/Profile 2 our custom effect same color/
// Profile 3 CYCLE_ALL/Profile 4 spare=Profile 1) for the given slot -- used both by this file's
// own eeconfig_init_kb() override (fresh EEPROM / after Clear-EEPROM) and by rgb_control.c's
// "=" reset action, so there's exactly one place these values are written down.
void rgb_profiles_get_factory_default(uint8_t index, rgb_profile_t *out);

// Fifth pass: the profile layout changed shape (not just size) this pass, and QMK does not re-run
// eeconfig_init_kb() just because EECONFIG_KB_DATA_SIZE changed -- old bytes would simply be read as
// the new layout (garbage: dark board, mode/speed nonsense) until someone did a Clear-EEPROM blind.
// A stored layout-version byte lets the firmware notice this itself at boot: if it doesn't match
// RGB_PROFILES_LAYOUT_VERSION (rgb_profiles.c), rgb_control.c's keyboard_post_init_user() resets every
// profile to factory defaults before loading. Bump the version whenever rgb_profile_t's shape changes.
bool rgb_profiles_layout_ok(void);
void rgb_profiles_reset_all_to_defaults(void); // all 4 profiles + active index 0 + version byte

#define RGB_TOTAL_KB_DATA_SIZE RGB_PROFILES_KB_DATA_SIZE // fifth pass: effect settings no longer
                                                            // have a separate shared block/size of
                                                            // their own -- see above. Kept as its
                                                            // own macro name (rather than every
                                                            // caller just using RGB_PROFILES_KB_
                                                            // DATA_SIZE directly) so config.h's own
                                                            // comment/_Static_assert reads the same
                                                            // as before.
