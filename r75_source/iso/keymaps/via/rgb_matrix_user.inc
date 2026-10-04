// Registers the custom "reactive_energy" RGB Matrix effect (enum value
// RGB_MATRIX_CUSTOM_reactive_energy). Required by RGB_MATRIX_CUSTOM_USER=yes (rules.mk).
// Mirrors ansi/keymaps/via/rgb_matrix_user.inc verbatim -- see that file for the full
// explanation of why this needs to be included this way. This copy exists because the ISO
// keymap previously had no such registration at all (continuation-pass bugfix; config.h's
// RGB_MATRIX_DEFAULT_MODE names RGB_MATRIX_CUSTOM_reactive_energy unconditionally for both
// board variants, and features/indicators.c unconditionally calls into features/rgb_control.c,
// so ISO cannot compile/link without this any more than ansi can).
//
// NOTE (see HANDOFF.md): the rendering this dispatches to (features/rgb_effects.c) hardcodes
// several LED indices/coordinates that were derived from and verified against the ANSI
// keyboard.json's physical layout specifically (Enter/Backspace/Space/Delete/encoder origins,
// the Esc-Del protected strip, etc). This registration makes the ISO+via build compile and run
// the same effect, but those geometry constants have not been re-derived or verified against
// ISO's own keyboard.json/matrix -- treat ISO's special-effect visuals as unverified until
// someone does that pass with real ISO hardware or its keyboard.json coordinates.

// R75 spec round (2026-09) sec 23/24/25: three new standalone modes, mirrored from the ANSI copy
// of this file. Unlike reactive_energy above, Row Wave/Darkening/Fairy Orb are written entirely
// against g_led_config/RGB_MATRIX_LED_COUNT rather than hardcoded ANSI LED indices (see
// rgb_effects.c), so -- unlike the note above about the bespoke single-key effects -- these three
// should render geometrically correctly on ISO's own layout without needing a separate
// derivation pass.
//
// Fourth pass (2026-09): three more new modes (Ripple Pool/Comet Trail/Equalizer), mirrored the
// same way, plus reordering to match the ANSI copy -- static-ish standalone modes first, most
// complex (reactive_energy, Custom Mode) last; see the ANSI copy of this file for the full
// per-mode reasoning, identical here. Same geometry note as the three above applies to all three
// of these too: all written against g_led_config/RGB_MATRIX_LED_COUNT, no hardcoded ANSI indices,
// so they carry over to ISO's own layout without a separate derivation pass. RGBCTL_TARGET_ESC
// (rgb_control.h) and the numbered-slot system it extends are reactive_energy/Layer-2-specific,
// and ISO has no Layer 2 (see this directory's own keymap.c) -- nothing to mirror there.
// Fourth pass follow-up: Equalizer removed at the person's request -- see the ANSI copy of this
// file for the full reasoning, identical here. rainbow_wave_rtl added at the front for the same
// reason as the ANSI copy (genuine right-to-left companion to stock Cycle Left/Right) -- same
// geometry-generic reasoning as the other standalone modes applies (g_led_config-based, no
// hardcoded ANSI LED indices), so it carries over to ISO's layout the same way.
// Fifth pass: same simple -> complex order as the ANSI copy (see its comment for the full reasoning
// -- ambient modes, then reactive ones, Custom Mode last). reactive_energy was registered FIRST in
// this file (above the other modes) until now, which contradicted "Custom Mode at the end" for ISO
// specifically -- moved to the end here to match.
// Sixth pass: Rainbow Right (stock Cycle Left/Right, re-registered) and Rainbow Left sit next to each other,
// first among the custom effects -- see config.h's note on ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT.
RGB_MATRIX_EFFECT(rainbow_wave_ltr)
RGB_MATRIX_EFFECT(rainbow_wave_rtl)
RGB_MATRIX_EFFECT(fairy_orb)
RGB_MATRIX_EFFECT(darkening_glow)
RGB_MATRIX_EFFECT(row_wave)
RGB_MATRIX_EFFECT(ripple_pool)
RGB_MATRIX_EFFECT(comet_trail)
RGB_MATRIX_EFFECT(reactive_energy)
#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

#include "../../../features/rgb_effects.h"

bool reactive_energy(effect_params_t* params) {
    return reactive_energy_render(params);
}

bool rainbow_wave_ltr(effect_params_t* params) {
    return rainbow_wave_ltr_render(params);
}

bool row_wave(effect_params_t* params) {
    return row_wave_render(params);
}

bool darkening_glow(effect_params_t* params) {
    return darkening_glow_render(params);
}

bool fairy_orb(effect_params_t* params) {
    return fairy_orb_render(params);
}

bool ripple_pool(effect_params_t* params) {
    return ripple_pool_render(params);
}

bool comet_trail(effect_params_t* params) {
    return comet_trail_render(params);
}

bool rainbow_wave_rtl(effect_params_t* params) {
    return rainbow_wave_rtl_render(params);
}

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
