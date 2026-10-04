// Continuation-pass bugfix: keyboards/r75/config.h (shared by both PCB variants) hardcodes
// RGB_MATRIX_LED_COUNT 80, which is correct for ansi/keyboard.json's rgb_matrix.layout (80
// entries, verified) but not for this variant's -- iso/keyboard.json's rgb_matrix.layout has 81
// entries (ISO's extra key next to Left Shift), so the generated g_led_config array didn't match
// this constant and the ISO build failed outright (excess/missing initializer elements). QMK
// resolves a variant's own config.h after its parent's, so this override is enough on its own;
// nothing else in the parent config.h needs duplicating here.
#pragma once

#undef RGB_MATRIX_LED_COUNT
#define RGB_MATRIX_LED_COUNT 81
