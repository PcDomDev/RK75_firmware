#include "orb_motion.h"

// ORB_MOTION_HOST_TEST: lets this file be compiled on a PC (together with a tiny shim that provides
// sin16()/cos16()) to simulate hours of motion in seconds and measure smoothness/containment. It is
// never defined in the firmware build.
#ifndef ORB_MOTION_HOST_TEST
#    include <lib/lib8tion/lib8tion.h> // sin16()/cos16(): theta 0..65535 = one full turn, result +-32767
#endif

// ------------------------------------------------------------------------------------------------
// The board is 224 x 64 units (keys span x 0..224, y 0..64). A vehicle that can only turn at a limited
// rate needs about 2 x R_MIN of height to U-turn, so on a board this short the orb has to start turning
// at exactly the right moment: too early and it loops constantly, too late and it leaves the board.
// Section 2 of orb_step() solves that with a "stopping point" prediction (see there).
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// Tuning (keyboard units; 16 = one key column).
// ------------------------------------------------------------------------------------------------

// Tightest turn the orb is ever allowed to make (radius of the circle it would trace at maximum
// curvature). Only the edge steering ever asks for the full curvature; free swimming uses a fraction
// of it (ORB_NOISE_AMPLITUDE).
#ifndef ORB_R_MIN_UNITS
#    define ORB_R_MIN_UNITS 22

// How quickly curvature may change, as the path length over which it relaxes toward what is wanted.
#endif
// This is what turns "sudden direction change" into a gradual swing -- larger = silkier.
#ifndef ORB_KAPPA_TAU_UNITS
#    define ORB_KAPPA_TAU_UNITS 16

// Length of one smooth-noise segment: the orb's natural curvature glides between two random values
#endif
// over a distance picked uniformly from this range, then a new random value is drawn (forever, so
// the route never repeats).
#ifndef ORB_SEG_MIN_UNITS
#    define ORB_SEG_MIN_UNITS 90
#endif
#ifndef ORB_SEG_MAX_UNITS
#    define ORB_SEG_MAX_UNITS 220
#endif
#ifndef ORB_NOISE_AMPLITUDE
#    define ORB_NOISE_AMPLITUDE 8500 // of 32768 (~26%) -- random curvature keys are drawn within +-this, so free
#endif
                                   // swimming makes long, gentle arcs of radius >= R_MIN / 0.26 = 100 units

// Travel speed range in units per second for speed setting 0 .. 255 (never zero: no abrupt
// stops).
#define ORB_V_MIN_UNITS_PER_S 18
#define ORB_V_MAX_UNITS_PER_S 150
// Changes of the speed setting are eased in over this long instead of applied instantly.
#define ORB_SPEED_TAU_MS 250

// The region the orb's center should stay inside. It reaches a little past the keys (the glow is much
// bigger than a key) but must never leave the keyboard dark.
#ifndef ORB_BOX_X_MIN
#    define ORB_BOX_X_MIN (-16)
#endif
#ifndef ORB_BOX_X_MAX
#    define ORB_BOX_X_MAX 240
#endif
#ifndef ORB_BOX_Y_MIN
#    define ORB_BOX_Y_MIN (-8)
#endif
#ifndef ORB_BOX_Y_MAX
#    define ORB_BOX_Y_MAX 72
// The edge steering ramps in over this many units before the box edge (smooth, never a wall bounce).
#endif
#ifndef ORB_SOFT_X
#    define ORB_SOFT_X 18
#endif
#ifndef ORB_SOFT_Y
#    define ORB_SOFT_Y 14
#endif
#ifndef ORB_STEEP_START
#    define ORB_STEEP_START 16000       // |sin(heading)|, Q15 (~0.49): steeper than ~30 degrees from horizontal gets levelled
#endif
#ifndef ORB_LEVEL_STRENGTH_PCT
#    define ORB_LEVEL_STRENGTH_PCT 55   // curvature (percent of maximum) the levelling asks for at full steepness
#endif
#ifndef ORB_DRIFT_MAX_PCT
#    define ORB_DRIFT_MAX_PCT 90
#endif
#ifndef ORB_DRIFT_FULL_DEPTH
#    define ORB_DRIFT_FULL_DEPTH 14
#endif
#define ORB_CENTER_X 112
#define ORB_CENTER_Y 32
// Nothing in the model should ever get here; it only exists so a bug can't send the orb to infinity.
#define ORB_HARD_X 400
#define ORB_HARD_Y 200

// Integration happens in sub-steps no longer than this, so a long frame never turns into a long
// straight lurch and the path is the same at any frame rate.
#define ORB_STEP_UNITS 3
// Frames longer than this are treated as this long (e.g. after the effect was paused).
#define ORB_MAX_DT_MS 50

// Tail sample spacing along the path.
#define ORB_TAIL_SPACING_UNITS 5
#define ORB_TAIL_CAPACITY ((ORB_TAIL_LENGTH_UNITS / ORB_TAIL_SPACING_UNITS) + 3)

// ------------------------------------------------------------------------------------------------
// Fixed-point conventions: positions/distances Q12 (units * 4096), curvature Q15 (-32768..32768 =
// -100%..+100% of the maximum curvature 1/ORB_R_MIN), heading uint32 where 2^32 = one full turn.
// ------------------------------------------------------------------------------------------------
#define Q12 4096
#define Q15 32768

// Heading change per unit of path length at 100% curvature, in heading units (2^32 per turn):
// 2^32 / (2 * pi * R_MIN). Constant-folded by the compiler.
#define ORB_HEADING_PER_UNIT_AT_MAX_CURVATURE ((int64_t)(4294967296.0 / (2.0 * 3.14159265358979 * ORB_R_MIN_UNITS)))

_Static_assert(ORB_TAIL_CAPACITY <= ORB_TAIL_MAX_SAMPLES, "orb_motion.h's ORB_TAIL_MAX_SAMPLES must cover the tail ring buffer");

typedef struct {
    int16_t  x, y;    // position, Q4
    uint32_t path_at; // path length (Q12) at the moment this sample was dropped
} tail_sample_t;

static uint32_t rng_state = 1;

static int32_t  pos_x_q12, pos_y_q12;
static uint32_t heading;              // 2^32 = 360 degrees
static int32_t  kappa;                // Q15, the curvature the orb is actually flying
static int32_t  speed_q8;             // units per second, Q8
static bool     speed_primed;

static int32_t noise_a, noise_b;         // Q15 curvature keys the noise is gliding between
static int32_t seg_pos_q12, seg_len_q12; // progress through the current noise segment
static int8_t  avoid_side;               // +1/-1: which way the last edge-avoidance turn went
static int8_t  level_side;               // +1/-1: which way the last "level out" turn went

static uint32_t      path_total_q12; // distance travelled (wraps harmlessly)
static uint32_t      last_drop_path_q12;
static tail_sample_t tail[ORB_TAIL_CAPACITY]; // ring buffer, oldest first
static uint8_t       tail_first, tail_count;

// ------------------------------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------------------------------

// xorshift32 -- the orb has its own generator so it neither depends on nor disturbs the global
// rand() state the other effects use.
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int32_t rng_range(int32_t lo, int32_t hi) { // inclusive
    return lo + (int32_t)(rng_next() % (uint32_t)(hi - lo + 1));
}

// Smoothstep 3p^2 - 2p^3 on Q15: 0 -> 0, 1 -> 1, slope zero at both ends.
static int32_t smoothstep_q15(int32_t p) {
    if (p <= 0) return 0;
    if (p >= Q15) return Q15;
    int64_t p2 = ((int64_t)p * p) >> 15;
    return (int32_t)((p2 * (3 * Q15 - 2 * (int64_t)p)) >> 15);
}

static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t iabs(int32_t a) { return a < 0 ? -a : a; }

// Integer square root (floor) of a 32-bit value.
static uint32_t isqrt32(uint32_t v) {
    uint32_t root = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static void start_noise_segment(void) {
    noise_a     = noise_b;
    noise_b     = rng_range(-ORB_NOISE_AMPLITUDE, ORB_NOISE_AMPLITUDE);
    seg_len_q12 = rng_range(ORB_SEG_MIN_UNITS, ORB_SEG_MAX_UNITS) * Q12;
}

// ------------------------------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------------------------------

void orb_motion_init(uint32_t seed) {
    rng_state = seed ? seed : 1;
    for (uint8_t i = 0; i < 8; i++) {
        rng_next(); // spin up: nearby seeds (timer values) must not give nearby first draws
    }

    pos_x_q12 = rng_range(60, 164) * Q12;
    pos_y_q12 = rng_range(20, 44) * Q12;
    heading   = rng_next();
    kappa     = 0;

    speed_primed = false;
    avoid_side   = (rng_next() & 1) ? 1 : -1;
    level_side   = (rng_next() & 1) ? 1 : -1;

    noise_b = 0;
    start_noise_segment(); // noise_a = 0, noise_b = first random key
    seg_pos_q12 = 0;

    path_total_q12     = 0;
    last_drop_path_q12 = 0;
    tail_first         = 0;
    tail_count         = 0;
}

static orb_xy_t to_display(int32_t x_q12, int32_t y_q12) {
    return (orb_xy_t){.x = (int16_t)(x_q12 >> 8), .y = (int16_t)(y_q12 >> 8)}; // Q12 -> Q4
}

// How far (units) the orb would stray outside the box if it turned `side` (+1/-1) at maximum curvature
// until its heading points clearly into the board (away from the wall with inward normal (nux, nuy),
// vn <= -0.7), then carried straight on. Walks the circular arc in 15 degree steps.
static int32_t arc_violation(int8_t side, int32_t x0, int32_t y0, uint16_t theta16, int32_t nux, int32_t nuy) {
    const int32_t R  = ORB_R_MIN_UNITS;
    const int32_t s0 = sin16(theta16);
    const int32_t c0 = cos16(theta16);
    // The curvature does not jump to its maximum: it ramps up over ~TAU units of path (less if the orb is
    // already turning that way), so the orb keeps going straight for a while first.
    int32_t ramp = Q15 - imax(0, imin(Q15, side * kappa));
    int32_t lead = (int32_t)(((int64_t)ORB_KAPPA_TAU_UNITS * ramp * 4) / (5 * (int64_t)Q15));
    int32_t x    = x0 + (int32_t)(((int64_t)c0 * lead) >> 15);
    int32_t y    = y0 + (int32_t)(((int64_t)s0 * lead) >> 15);
    // circle center: P + side * R * (-sin t, cos t)
    const int32_t ccx = x + side * (int32_t)(((int64_t)(-s0) * R) >> 15);
    const int32_t ccy = y + side * (int32_t)(((int64_t)c0 * R) >> 15);
    int32_t worst = imax(imax(ORB_BOX_X_MIN - x, x - ORB_BOX_X_MAX), imax(ORB_BOX_Y_MIN - y, y - ORB_BOX_Y_MAX));
    worst         = imax(worst, 0);
    for (uint8_t i = 0; i <= 16; i++) { // 16 * 15 deg = 240 deg, more than any turn needs
        uint16_t th = (uint16_t)(theta16 + side * (int32_t)i * (65536 / 24));
        int32_t  hs = sin16(th);
        int32_t  hc = cos16(th);
        // position on the arc at heading th: center + side * R * (sin th, -cos th)
        int32_t px = ccx + side * (int32_t)(((int64_t)hs * R) >> 15);
        int32_t py = ccy + side * (int32_t)(((int64_t)(-hc) * R) >> 15);
        int32_t v  = imax(imax(ORB_BOX_X_MIN - px, px - ORB_BOX_X_MAX), imax(ORB_BOX_Y_MIN - py, py - ORB_BOX_Y_MAX));
        worst      = imax(worst, v);
        int32_t vn = (int32_t)(-((int64_t)hc * nux + (int64_t)hs * nuy) >> 15);
        if (vn <= -(Q15 * 7) / 10) {
            break; // heading clearly inward now -- the rest of the arc is straight
        }
    }
    return worst;
}

// One piece of travel of `ds` units (Q12).
static void orb_step(int32_t ds) {
    // --- 1. natural curvature from smooth noise -------------------------------------------------
    seg_pos_q12 += ds;
    while (seg_pos_q12 >= seg_len_q12) {
        seg_pos_q12 -= seg_len_q12;
        start_noise_segment();
    }
    int32_t p       = (int32_t)(((int64_t)seg_pos_q12 * Q15) / seg_len_q12);
    int32_t s       = smoothstep_q15(p);
    int32_t k_noise = noise_a + (int32_t)(((int64_t)(noise_b - noise_a) * s) >> 15);

    // --- 2. soft steering back toward the middle before the orb reaches an edge ---------------------
    // Instead of reacting to where the orb IS (too late) or to a fixed distance ahead (fires on every
    // shallow heading), predict where it would COME TO REST relative to each wall if it started its
    // tightest turn now: turning from a heading at angle b to the wall normal until it runs parallel to
    // the wall displaces it R * (1 - sin b) toward the wall (R when heading straight at it, 0 when already
    // parallel), plus the distance covered while the curvature is still ramping up (~TAU along the heading).
    // When that stopping point reaches the edge zone, the steering takes over -- smoothly, by the depth.
    int32_t x  = pos_x_q12 >> 12;
    int32_t y  = pos_y_q12 >> 12;
    int32_t hx = cos16((uint16_t)(heading >> 16)); // Q15 unit heading vector
    int32_t hy = sin16((uint16_t)(heading >> 16));

    int32_t stop_dx = (int32_t)(((int64_t)ORB_R_MIN_UNITS * (Q15 - iabs(hy)) + (int64_t)ORB_KAPPA_TAU_UNITS * iabs(hx)) >> 15);
    int32_t stop_dy = (int32_t)(((int64_t)ORB_R_MIN_UNITS * (Q15 - iabs(hx)) + (int64_t)ORB_KAPPA_TAU_UNITS * iabs(hy)) >> 15);
    int32_t sx      = x + (hx >= 0 ? stop_dx : -stop_dx);
    int32_t sy      = y + (hy >= 0 ? stop_dy : -stop_dy);

    // Penalty per wall, 0..Q15 (0 = comfortably away, Q15 = at/over the edge zone), for the worse of
    // "where the orb is" and "where it would come to rest".
    int32_t wall[4] = {0, 0, 0, 0}; // left, right, top (y min), bottom (y max)
    for (uint8_t k = 0; k < 2; k++) {
        int32_t px = (k == 0) ? x : sx;
        int32_t py = (k == 0) ? y : sy;
        int32_t d[4] = {
            ORB_SOFT_X - (px - ORB_BOX_X_MIN),
            ORB_SOFT_X - (ORB_BOX_X_MAX - px),
            ORB_SOFT_Y - (py - ORB_BOX_Y_MIN),
            ORB_SOFT_Y - (ORB_BOX_Y_MAX - py),
        };
        for (uint8_t i = 0; i < 4; i++) {
            int32_t soft = (i < 2) ? ORB_SOFT_X : ORB_SOFT_Y;
            int32_t pk   = (int32_t)(imin(imax(d[i], 0), soft) * (int64_t)Q15 / soft);
            wall[i]      = imax(wall[i], pk);
        }
    }
    int32_t w = smoothstep_q15(imax(imax(wall[0], wall[1]), imax(wall[2], wall[3]))); // 0 well inside, 1 at/after the edge zone

    // Level out: the board is 3.5x wider than it is tall, so the orb should mostly swim ACROSS it. Headings
    // steeper than ORB_STEEP_START (|sin|, Q15) are eased back toward horizontal with a smoothly ramped extra
    // curvature, which also produces the long diagonal sweeps with rounded bounces at the ends. Faded out
    // while an edge turn is under way (then the orb has to be allowed to go vertical).
    int32_t k_free = k_noise;
    int32_t steep  = iabs(hy) - ORB_STEEP_START;
    if (steep > 0) {
        int32_t vw = smoothstep_q15((int32_t)(((int64_t)steep * Q15) / (32767 - ORB_STEEP_START)));
        if (iabs(hx) > 3000) { // clear which way is "toward horizontal"; otherwise keep the last answer
            level_side = ((hx > 0) == (hy > 0)) ? -1 : 1;
        }
        int32_t k_level = level_side * ((Q15 * ORB_LEVEL_STRENGTH_PCT) / 100);
        k_free          = k_noise + (int32_t)(((int64_t)(k_level - k_noise) * vw) >> 15);
    }

    int32_t k_cmd = k_free;
    if (w > 0) {
        // Inward normal of whatever the orb is pressing against (both walls add up in a corner).
        int32_t nx = wall[0] - wall[1];
        int32_t ny = wall[2] - wall[3];
        uint32_t nl = isqrt32((uint32_t)(((int64_t)nx * nx + (int64_t)ny * ny) >> 0 > 0x7fffffff ? 0x7fffffff : (uint32_t)((int64_t)nx * nx + (int64_t)ny * ny)));
        if (nl > 0) {
            int32_t nux = (int32_t)(((int64_t)nx * Q15) / (int32_t)nl);
            int32_t nuy = (int32_t)(((int64_t)ny * Q15) / (int32_t)nl);
            // vn > 0: heading toward the wall.
            int32_t vn = (int32_t)(-((int64_t)hx * nux + (int64_t)hy * nuy) >> 15);

            // Which way to turn: whichever arc stays on the board. (Picking "the shorter rotation toward the
            // wall's normal" is not enough: a heading that is only slightly off head-on makes one side look
            // marginally shorter even when that side is the one with no room, and the U-turn then bulges out of
            // the board.) Not re-decided once a turn is clearly under way, and only switched when the other
            // side is clearly better, so it cannot flip back and forth mid-turn.
            bool committed = (kappa * avoid_side) > (Q15 / 4);
            if (!committed) {
                int32_t va = arc_violation(1, x, y, (uint16_t)(heading >> 16), nux, nuy);
                int32_t vb = arc_violation(-1, x, y, (uint16_t)(heading >> 16), nux, nuy);
                if (iabs(va - vb) > 4) {
                    avoid_side = (va < vb) ? 1 : -1;
                }
            }

            // How hard to turn: full curvature when heading at the wall, still strong while running
            // along it (vn = 0) so the orb keeps rounding the corner instead of sliding off down the
            // wall, tapering to nothing once it is clearly heading inward (vn <= -0.7, about 45 degrees
            // off the wall) and the free-swimming noise can take over again.
            int32_t mag     = imax(0, imin(Q15, vn + (Q15 * 7) / 10));
            int32_t k_avoid = avoid_side * mag;
            k_cmd           = k_free + (int32_t)(((int64_t)(k_avoid - k_free) * w) >> 15);
        }
    }

    // --- 3. curvature glides toward the wish (low-pass in PATH LENGTH, not time) ---------------
    kappa += (int32_t)(((int64_t)(k_cmd - kappa) * ds) / ((int64_t)ORB_KAPPA_TAU_UNITS * Q12 + ds));
    kappa = imax(-Q15, imin(Q15, kappa));

    // --- 4. turn, then move forward (midpoint heading for an accurate arc) --------------------
    int32_t  dtheta = (int32_t)(((int64_t)kappa * ds * ORB_HEADING_PER_UNIT_AT_MAX_CURVATURE) >> 27); // Q15 * Q12
    uint32_t mid    = heading + (uint32_t)(dtheta / 2);
    pos_x_q12 += (int32_t)(((int64_t)cos16((uint16_t)(mid >> 16)) * ds) >> 15);
    pos_y_q12 += (int32_t)(((int64_t)sin16((uint16_t)(mid >> 16)) * ds) >> 15);
    heading += (uint32_t)dtheta;

    // Last line of defence: a gentle sideways drift back toward the board once the center is PAST the box
    // edge (never reached in normal operation -- the steering above turns the orb well before that). It ramps
    // in smoothly with depth (zero at the edge, ORB_DRIFT_MAX_PCT of the travel speed at ORB_DRIFT_FULL_DEPTH
    // units past it), so it can never show up as a jolt.
    {
        int32_t dxo = imax(imax(ORB_BOX_X_MIN - (pos_x_q12 >> 12), (pos_x_q12 >> 12) - ORB_BOX_X_MAX), 0);
        int32_t dyo = imax(imax(ORB_BOX_Y_MIN - (pos_y_q12 >> 12), (pos_y_q12 >> 12) - ORB_BOX_Y_MAX), 0);
        if (dxo > 0) {
            int32_t f = smoothstep_q15((int32_t)(imin(dxo, ORB_DRIFT_FULL_DEPTH) * (int64_t)Q15 / ORB_DRIFT_FULL_DEPTH));
            int32_t mv = (int32_t)(((int64_t)ds * f * ORB_DRIFT_MAX_PCT) / (100 * (int64_t)Q15));
            pos_x_q12 += ((pos_x_q12 >> 12) < ORB_BOX_X_MIN) ? mv : -mv;
        }
        if (dyo > 0) {
            int32_t f = smoothstep_q15((int32_t)(imin(dyo, ORB_DRIFT_FULL_DEPTH) * (int64_t)Q15 / ORB_DRIFT_FULL_DEPTH));
            int32_t mv = (int32_t)(((int64_t)ds * f * ORB_DRIFT_MAX_PCT) / (100 * (int64_t)Q15));
            pos_y_q12 += ((pos_y_q12 >> 12) < ORB_BOX_Y_MIN) ? mv : -mv;
        }
    }

    pos_x_q12 = imax(-ORB_HARD_X * Q12, imin(ORB_HARD_X * Q12, pos_x_q12));
    pos_y_q12 = imax(-ORB_HARD_Y * Q12, imin(ORB_HARD_Y * Q12, pos_y_q12));

    // --- 5. tail bookkeeping  -------------------------------------------
    path_total_q12 += (uint32_t)ds;

    // Drop the oldest samples that have fully faded.
    while (tail_count > 0 && (uint32_t)(path_total_q12 - tail[tail_first].path_at) >= (uint32_t)ORB_TAIL_LENGTH_UNITS * Q12) {
        tail_first = (uint8_t)((tail_first + 1) % ORB_TAIL_CAPACITY);
        tail_count--;
    }
    // Drop a new sample every ORB_TAIL_SPACING_UNITS of displayed travel.
    if ((uint32_t)(path_total_q12 - last_drop_path_q12) >= (uint32_t)ORB_TAIL_SPACING_UNITS * Q12) {
        last_drop_path_q12 = path_total_q12;
        if (tail_count == ORB_TAIL_CAPACITY) { // can't happen with the capacity above; stay safe
            tail_first = (uint8_t)((tail_first + 1) % ORB_TAIL_CAPACITY);
            tail_count--;
        }
        uint8_t     slot = (uint8_t)((tail_first + tail_count) % ORB_TAIL_CAPACITY);
        orb_xy_t    d    = to_display(pos_x_q12, pos_y_q12);
        tail[slot].x       = d.x;
        tail[slot].y       = d.y;
        tail[slot].path_at = path_total_q12;
        tail_count++;
    }
}

void orb_motion_advance(uint16_t dt_ms, uint8_t speed_setting) {
    if (dt_ms > ORB_MAX_DT_MS) {
        dt_ms = ORB_MAX_DT_MS;
    }

    // Speed setting -> target speed, then ease toward it (a speed change must never be a jolt).
    int32_t target_q8 = ((int32_t)ORB_V_MIN_UNITS_PER_S * 255 + (int32_t)(ORB_V_MAX_UNITS_PER_S - ORB_V_MIN_UNITS_PER_S) * speed_setting) * 256 / 255;
    if (!speed_primed) {
        speed_q8     = target_q8;
        speed_primed = true;
    } else if (dt_ms > 0) {
        speed_q8 += (int32_t)(((int64_t)(target_q8 - speed_q8) * dt_ms) / ((int32_t)dt_ms + ORB_SPEED_TAU_MS));
    }

    // Distance to cover this frame (Q12 units): units/s (Q8) * ms / 1000, Q8 -> Q12.
    int32_t left = (int32_t)(((int64_t)speed_q8 * dt_ms * 16) / 1000);
    while (left > 0) {
        int32_t ds = imin(left, ORB_STEP_UNITS * Q12);
        orb_step(ds);
        left -= ds;
    }
}

orb_xy_t orb_motion_head(void) {
    return to_display(pos_x_q12, pos_y_q12);
}

bool orb_motion_tail(uint8_t i, orb_xy_t *pos, uint16_t *age_q4) {
    if (i >= tail_count) {
        return false;
    }
    // i = 0 is the newest sample, which sits at the end of the ring buffer.
    uint8_t slot = (uint8_t)((tail_first + tail_count - 1 - i) % ORB_TAIL_CAPACITY);
    pos->x       = tail[slot].x;
    pos->y       = tail[slot].y;
    uint32_t age_q12 = (uint32_t)(path_total_q12 - tail[slot].path_at);
    *age_q4          = (uint16_t)(age_q12 >> 8);
    return true;
}
