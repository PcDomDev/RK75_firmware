#pragma once

#include <stdint.h>
#include <stdbool.h>

// ================================================================================================
// Orb motion -- the "floating fish" brain behind the Fairy Orb RGB effect (sixth pass: rewritten
// from scratch; the old retarget-and-ease wander in custom_rgb.c is gone).
//
// This module only decides WHERE the orb is and WHERE ITS TAIL WAS. It knows nothing about LEDs,
// colors or QMK's render loop (rgb_effects.c's fairy_orb_render() draws it), which also makes it
// testable on a plain PC (see ORB_MOTION_HOST_TEST in orb_motion.c).
//
// How it moves (why it cannot jitter, flip or stop):
//   * It is a vehicle with a HEADING and a CURVATURE, not a point chasing targets. Position only ever
//     changes by "move forward along the current heading"; heading only changes by "curvature x
//     distance". There is no target to arrive at, so no sudden retargeting, no overshoot and no
//     stop-and-pick-a-new-point moment.
//   * Curvature itself is never set directly: it is low-pass filtered, so it can only glide from
//     one value to the next. Heading is therefore the integral of a smooth signal (smooth), and
//     position the integral of heading (smoother still).
//   * The wish for curvature comes from smooth procedural noise (smoothstep-interpolated random key
//     values -- continuous with zero slope at every key, new random keys forever, so the path never
//     loops) blended with a soft steering force that bends the orb back toward the board when it
//     gets near the edge (a smooth ramp, never a wall bounce).
//   * Everything above is evaluated in PATH-LENGTH space, not time. The shape of the path is a pure
//     function of distance travelled, so the configured speed changes only how fast that same path
//     is flown through -- never how sharply it turns or how fluid it looks. Frame rate is likewise
//     irrelevant (movement is integrated from real elapsed milliseconds in small fixed-length
//     sub-steps).
// ================================================================================================

// Coordinates are in the same "LED units" as g_led_config.point[] (x 0..224, y 0..64). Public
// positions are Q4 fixed point (value / ORB_Q = units) so the renderer can do integer distance math.
#define ORB_Q 16

typedef struct {
    int16_t x, y; // Q4: units * ORB_Q
} orb_xy_t;

// Seeds the random source and drops the orb somewhere near the middle of the board with a random
// heading. Call when the effect is (re)entered.
void orb_motion_init(uint32_t seed);

// Advances the orb by `dt_ms` of real time. `speed_setting` is the user's 0-255 speed value for
// this effect (0 = slowest, 255 = fastest). Call exactly once per rendered frame.
void orb_motion_advance(uint16_t dt_ms, uint8_t speed_setting);

// Current head position (Q4).
orb_xy_t orb_motion_head(void);

// Tail: a short history of where the orb has been, dropped at fixed distances along its path.
// Index 0 is the newest sample. `age_q4` receives how far (in Q4 units) the orb has travelled since
// that sample was dropped -- the renderer fades samples by age, which keeps the fade perfectly
// smooth at any speed. Returns false when i is past the last stored sample.
bool orb_motion_tail(uint8_t i, orb_xy_t *pos, uint16_t *age_q4);

// Length (units) after which a tail sample has fully faded and is discarded.
#define ORB_TAIL_LENGTH_UNITS 72

// Most tail samples orb_motion_tail() can ever return (renderer-side array size). Tail samples are dropped every
// 5 units, so 72 units need 15; the model keeps a little headroom.
#define ORB_TAIL_MAX_SAMPLES 18
