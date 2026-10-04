#include "rgb_profiles.h"
#include "rgb_control.h" // RGBCTL_TARGET_COUNT -- for the _Static_assert below only

_Static_assert(EECONFIG_KB_DATA_SIZE == RGB_TOTAL_KB_DATA_SIZE,
               "config.h's EECONFIG_KB_DATA_SIZE has drifted out of sync with rgb_profile_t's "
               "actual size -- update config.h's literal to match RGB_TOTAL_KB_DATA_SIZE.");

#define ACTIVE_INDEX_OFFSET ((uint32_t)(sizeof(rgb_profile_t) * RGB_PROFILE_COUNT)) // byte right after the profiles
#define LAYOUT_VERSION_OFFSET (ACTIVE_INDEX_OFFSET + 1)                                 // last byte of the region
#define RGB_PROFILES_LAYOUT_VERSION 6 // sixth pass: RGBCTL_TARGET_ENCODER removed (shape changed). Bump on any shape change.

void rgb_profiles_read(uint8_t index, rgb_profile_t *out) {
    eeconfig_read_kb_datablock(out, (uint32_t)index * sizeof(rgb_profile_t), sizeof(rgb_profile_t));
}

void rgb_profiles_write(uint8_t index, const rgb_profile_t *in) {
    eeconfig_update_kb_datablock(in, (uint32_t)index * sizeof(rgb_profile_t), sizeof(rgb_profile_t));
}

uint8_t rgb_profiles_read_active_index(void) {
    uint8_t index = 0;
    eeconfig_read_kb_datablock(&index, ACTIVE_INDEX_OFFSET, sizeof(index));
    if (index >= RGB_PROFILE_COUNT) {
        index = 0; // defensive: a corrupted/never-written byte must never index out of bounds
                   // later. Shouldn't happen given eeconfig_init_kb() below always writes a
                   // valid value, but this is a single stray byte, not worth trusting blindly.
    }
    return index;
}

void rgb_profiles_write_active_index(uint8_t index) {
    eeconfig_update_kb_datablock(&index, ACTIVE_INDEX_OFFSET, sizeof(index));
}

// Fifth pass (2026-09): restructured from two separate stores -- a small per-profile
// {effect,hue,sat,val,speed} plus one *shared* [hue,sat,val]-per-target block every profile used
// alike -- into the single, fully independent-per-profile shape rgb_profiles.h now declares. See
// that header's own comment for the full reasoning. Factory defaults follow the same split the
// old design had, just no longer split across two files/EEPROM regions: each profile keeps its
// own distinct mode + Base color (matching sec 7.2's named defaults: Profile 1 Solid, Profile 2
// this project's own reactive effect, Profile 3 Cycle All, Profile 4 a spare starting as a copy of
// Profile 1), and every *other* target (Enter/Backspace/.../Orb) defaults the same way across all
// four profiles -- a fresh board should look identical to how it always has, out of the box, in
// whichever profile you happen to be on; the person can diverge them from there.
//
// RGB_MATRIX_DEFAULT_SPD 127 = QMK's own stock fallback (UINT8_MAX/2, quantum/rgb_matrix/
// rgb_matrix.h) -- matches what the board already boots with rather than inventing a different
// number, same reasoning this project has used for this value since Stage 7.
static void fill_default_targets(rgb_target_settings_t targets[RGBCTL_TARGET_COUNT], uint8_t base_hue, uint8_t base_sat, uint8_t base_val) {
    targets[RGBCTL_TARGET_BASE] = (rgb_target_settings_t){base_hue, base_sat, base_val, RGB_MATRIX_DEFAULT_SPD};

    // Every other target: same [hue,sat,val,speed] defaults across all four profiles. All default
    // to plain {RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, 255, RGB_MATRIX_DEFAULT_SPD}
    // unless noted -- for Esc specifically, this also keeps its wave looking exactly like it did
    // before it had its own target at all (it used to just read the live base color directly).
    const rgb_target_settings_t plain = {RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, 255, RGB_MATRIX_DEFAULT_SPD};
    targets[RGBCTL_TARGET_ENTER]     = plain;
    targets[RGBCTL_TARGET_BACKSPACE] = (rgb_target_settings_t){0, 0, 255, RGB_MATRIX_DEFAULT_SPD}; // white
    targets[RGBCTL_TARGET_DELETE]    = plain;
    targets[RGBCTL_TARGET_SPACE]     = plain;
    targets[RGBCTL_TARGET_ESC]       = plain;
    targets[RGBCTL_TARGET_CAPS]      = (rgb_target_settings_t){0, 255, 255, RGB_MATRIX_DEFAULT_SPD}; // red
    targets[RGBCTL_TARGET_FNVIZ]     = (rgb_target_settings_t){128, 200, 255, RGB_MATRIX_DEFAULT_SPD}; // teal-ish blue
    targets[RGBCTL_TARGET_ROWWAVE]   = plain;
    targets[RGBCTL_TARGET_RIPPLE]    = plain;
    targets[RGBCTL_TARGET_COMET]     = plain;
    targets[RGBCTL_TARGET_ORB]       = plain; // fifth pass: Fairy Orb's new own target
}

void rgb_profiles_get_factory_default(uint8_t index, rgb_profile_t *out) {
    static const uint8_t effect_ids[RGB_PROFILE_COUNT] = {
        RGB_MATRIX_SOLID_COLOR,
        RGB_MATRIX_CUSTOM_reactive_energy,
        RGB_MATRIX_CYCLE_ALL,
        RGB_MATRIX_SOLID_COLOR,
    };
    if (index >= RGB_PROFILE_COUNT) {
        index = 0;
    }
    out->effect_id = effect_ids[index];
    fill_default_targets(out->targets, RGB_MATRIX_DEFAULT_HUE, RGB_MATRIX_DEFAULT_SAT, RGB_MATRIX_DEFAULT_VAL);
    out->caps_enabled = 1;
}

// Sec 7.2: fresh EEPROM (first boot) and the existing Clear-EEPROM tap-dance both funnel through
// eeconfig_init_quantum() -> this hook (verified by reading quantum/eeconfig.c directly, not
// assumed) -- overriding it is what makes Clear-EEPROM correctly wipe profile data too, per
// STAGE_STRUCTURE.md's named risk for this stage. eeconfig_init_kb_datablock() (this region's
// zero-fill) has already run by the time this executes -- this only needs to overwrite that
// zero-fill with real factory values, not handle the region existing at all.
bool rgb_profiles_layout_ok(void) {
    uint8_t version = 0;
    eeconfig_read_kb_datablock(&version, LAYOUT_VERSION_OFFSET, sizeof(version));
    return version == RGB_PROFILES_LAYOUT_VERSION;
}

void rgb_profiles_reset_all_to_defaults(void) {
    for (uint8_t i = 0; i < RGB_PROFILE_COUNT; i++) {
        rgb_profile_t defaults;
        rgb_profiles_get_factory_default(i, &defaults);
        rgb_profiles_write(i, &defaults);
    }
    rgb_profiles_write_active_index(0);
    uint8_t version = RGB_PROFILES_LAYOUT_VERSION;
    eeconfig_update_kb_datablock(&version, LAYOUT_VERSION_OFFSET, sizeof(version));
}

void eeconfig_init_kb(void) {
    rgb_profiles_reset_all_to_defaults();
    eeconfig_init_user(); // preserve the default (weak) eeconfig_init_kb()'s existing tail-call
                           // -- this override replaces that function, not just adds to it, so
                           // skipping this would silently stop user-level EEPROM init from
                           // running on a fresh/cleared board.
}
