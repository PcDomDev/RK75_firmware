ENCODER_MAP_ENABLE = yes
VIA_ENABLE = yes
TAP_DANCE_ENABLE = yes
LTO_ENABLE = yes
OPENRGB_ENABLE = yes
RAW_ENABLE = yes
SIGNALRGB_SUPPORT_ENABLE = yes
RGB_MATRIX_CUSTOM_USER = yes
# Bugfix (continuation pass): OPENRGB_ENABLE/SIGNALRGB_SUPPORT_ENABLE are project-local flags,
# not stock QMK rules.mk variables -- QMK does not turn an arbitrary "_ENABLE = yes" into a
# compiler -D define by itself (unlike RGB_MATRIX_CUSTOM_USER above, which core already wires in
# builddefs/common_features.mk). Without this, the #ifdef OPENRGB_ENABLE / #ifdef
# SIGNALRGB_SUPPORT_ENABLE guards in keymap.c were always false and those two keycodes' bodies
# silently compiled to nothing. (RGB_MATRIX_KEYREACTIVE_ENABLED, previously also listed here, was
# a harmless no-op the other direction: quantum/rgb_matrix/rgb_matrix_types.h #defines the real
# macro of that name unconditionally, so every stock reactive-style effect this project enables
# in config.h -- SOLID_MULTISPLASH, JELLYBEAN_RAINDROPS, DIGITAL_RAIN, TYPING_HEATMAP -- was
# already correctly registered regardless of this line; removed to avoid implying otherwise.)
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