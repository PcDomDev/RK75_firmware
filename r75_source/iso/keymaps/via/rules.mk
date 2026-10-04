ENCODER_MAP_ENABLE = yes
VIA_ENABLE = yes
TAP_DANCE_ENABLE = yes
LTO_ENABLE = yes
OPENRGB_ENABLE = yes
RAW_ENABLE = yes
SIGNALRGB_SUPPORT_ENABLE = yes
RGB_MATRIX_CUSTOM_USER = yes
# Bugfix (continuation pass): this keymap's indicators.c (always compiled, see SRC below) calls
# rgbctl_render_bar()/rgbctl_render_dfu_bar() (features/rgb_control.h) unconditionally, and
# config.h's RGB_MATRIX_DEFAULT_MODE names RGB_MATRIX_CUSTOM_reactive_energy -- both require the
# same custom-effect registration and features/*.c set already used by the ansi/via keymap. This
# keymap previously had neither, which cannot compile/link. See ansi/keymaps/via/rules.mk for why
# the OPT_DEFS block below is also required (OPENRGB_ENABLE/SIGNALRGB_SUPPORT_ENABLE are project-
# local names, not stock QMK ones).
ifeq ($(strip $(OPENRGB_ENABLE)), yes)
    OPT_DEFS += -DOPENRGB_ENABLE
endif
ifeq ($(strip $(SIGNALRGB_SUPPORT_ENABLE)), yes)
    OPT_DEFS += -DSIGNALRGB_SUPPORT_ENABLE
endif
SRC += ../../../features/indicator_queue.c
SRC += ../../../features/tap_hold.c
SRC += ../../../features/indicators.c
SRC += ../../../features/rgb_keys.c
SRC += ../../../features/socd_cleaner.c
SRC += ../../../features/custom_rgb.c
SRC += ../../../features/rgb_effects.c
SRC += ../../../features/orb_motion.c
SRC += ../../../features/rgb_control.c
SRC += ../../../features/rgb_profiles.c

# Fourth pass (2026-09): this ISO variant's LTO link step (this keymap only -- ansi/keymaps/via
# needs no such override) fails with "-Werror=lto-type-mismatch" on g_led_config, reported between
# quantum/rgb_matrix/rgb_matrix.h's own extern declaration and default_keyboard.c's generated
# definition -- both files this project doesn't touch. Confirmed a GCC 13.2/LTO false positive,
# not a real ODR/type violation: the identical source (this keymap's features/*.c set, unchanged
# by this line) links and runs cleanly with LTO_ENABLE=no, and the two "mismatched" declarations
# are QMK core/generated files, not application code. Likely tipped over by this pass's added code
# size shifting GCC's LTO type-debug-info merging across the ChibiOS HAL's very large number of
# translation units (a known category of GCC LTO fragility, not specific to any one symbol's real
# type). Downgrading just this one diagnostic back to a warning (rather than disabling LTO
# entirely, which would cost real code-size/performance for both variants) is the narrower fix --
# CFLAGS rather than LDFLAGS so it's baked into each translation unit's own LTO section the same
# way -Werror already is, since that's what the link-time LTO backend re-invocation actually reads.
CFLAGS += -Wno-error=lto-type-mismatch