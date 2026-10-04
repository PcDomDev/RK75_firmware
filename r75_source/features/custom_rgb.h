#pragma once

#include QMK_KEYBOARD_H

// Shared reactive-glow event pool and the plumbing that feeds it from the core RGB Matrix
// key-hit tracker (g_last_hit_tracker) without touching keymap.c or any quantum/-core file.
// features/rgb_effects.c reads this pool every frame to render.
//
// Verified against this fork's actual quantum/rgb_matrix/rgb_matrix.c before use (not assumed):
// g_last_hit_tracker is `extern`-visible (rgb_matrix.h) and is a per-frame snapshot, refreshed
// exactly once per frame in rgb_task_start() -- stable for the whole RENDERING phase of a frame
// even if that phase spans multiple led_min/led_max chunk calls. g_led_config.point[i].x/.y is
// the same field the core itself uses internally to populate that tracker. Also verified: the
// reactive tracker is fed from switch_events()/rgb_matrix_handle_key_event() at the raw matrix-
// scan level, entirely independent of process_record_user()'s return value -- relevant to
// Cleaning Mode (features/rgb_control.h), which relies on reactive glow continuing to work even
// while process_record_user blocks everything else.

// --- Protected upper control strip ----------------------------------------------------------
// Esc, F1-F12, Delete -- LED indices 8-21 inclusive (verified against keyboard.json's
// rgb_matrix.layout: matrix row 0 occupies exactly this contiguous LED range). Normal reactive
// glow, and every special effect below, must never illuminate or erase these -- they're driven
// only by features/indicators.c (Fn-help / RGB-control bars / Caps / Cleaning Mode) and the
// F-key-group / Escape-sequence overlays in rgb_effects.c, which are the "explicitly specified"
// exceptions. Encoder has no LED of its own (matrix [0,14] has none in rgb_matrix.layout).
#define LED_PROTECTED_MIN 8
#define LED_PROTECTED_MAX 21
static inline bool is_protected_led(uint8_t led) {
    return led >= LED_PROTECTED_MIN && led <= LED_PROTECTED_MAX;
}

// RGB R75 spec round (2026-09): sec 6/47 -- effects were being evicted from a 24-slot pool
// during fast typing before they finished fading. Bumped to 32 (above the spec's suggested
// 20-30) since the cost is trivial: 4 B/slot, +32 B RAM total for this one pool. Same reasoning
// applies to EFFECT_POOL_SIZE/ENCWAVE_POOL_SIZE below -- all three bumped together so no single
// pool becomes the new bottleneck after the others are widened.
//
// Fourth pass (2026-09): reported as "only remembers about 20-30 keys" during fast/varied
// multi-key typing in Custom Mode (reactive_energy) before the oldest ones visibly disappear
// early. 32 was already above the old spec's own suggested ceiling, so this wasn't a case of the
// previous number being chosen too low against its own target -- it just needed to be bigger than
// any realistic burst of distinct keys a person can physically produce inside one ~2s decay
// window. Bumped to 128 (4x): cost is still trivial (4 B/slot => 512 B total, +384 B over the
// previous 32-slot size) against this MCU's 28,672 B RAM, and de-dup/render cost per slot is one
// cheap comparison (glow_pool_has_event()) or one falloff calc (glow_event_contribution()) -- see
// HANDOFF.md's own §14 reasoning for why widening this pool doesn't meaningfully cost anything at
// this MCU's frame rate.
#define GLOW_POOL_SIZE 128
#define GLOW_LED_NONE 0xFF  // sentinel: slot unused. Real LED indices are 0-79 (ANSI) / 0-80
                             // (ISO), so neither can be the empty value -- must skip
                             // GLOW_LED_NONE slots when rendering.

typedef enum {
    GLOW_KIND_NORMAL = 0,  // letters, numbers, ~/-/=, and every key without a dedicated bespoke
                            // animation of its own. Also used for the synthetic idle events
                            // below -- they are meant to look exactly like a normal keypress.
    GLOW_KIND_MODIFIER,     // Alt/Win/Ctrl/Shift/Fn/RAlt
    GLOW_KIND_TAB,
    // R75 spec round (2026-09) sec 9: a *separate* kind, rather than branching on live Shift/Caps
    // state at render time, deliberately -- radius has to be decided once, at the moment an event
    // is inserted (glow_pool_sync(), custom_rgb.c), and then stay fixed for that event's whole
    // life. The bug this replaces: glow_event_contribution() used to call glow_wide_radius_active()
    // itself, live, on every single render call -- so an event that started under the normal
    // radius would visibly *grow* the instant Shift was pressed while it was still fading, and an
    // event that started enlarged would shrink the instant Shift was released, neither of which
    // sec 9's walkthrough allows ("the already-existing animation must NOT suddenly become
    // larger"). Now glow_wide_radius_active() is only ever read once, at insertion.
    GLOW_KIND_NORMAL_WIDE,
} glow_kind_t;

// 5 bytes/slot (uint16_t then three uint8_t -- naturally aligned, no padding) * 128 = 640 B.
typedef struct {
    uint16_t start_time;  // g_rgb_timer-scale ms timestamp, truncated to 16 bits. Age is computed
                           // as (uint16_t)(g_rgb_timer - start_time); unsigned wraparound makes
                           // this correct as long as an entry is never queried >65s after it
                           // started, which holds here (every decay curve fully fades in ~2s).
    uint8_t led_index;    // GLOW_LED_NONE (0xFF) = slot unused/free
    uint8_t kind;          // glow_kind_t
    bool    simulated;     // fourth pass: true if this came from idle simulation rather than a
                            // real keypress -- lets glow_track_real_input() force-clear any of
                            // these still active the instant real typing resumes (see its own
                            // comment), rather than letting them visibly run out their full,
                            // normal fade on their own. Real keystroke entries are never touched.
} glow_event_t;

extern glow_event_t glow_pool[GLOW_POOL_SIZE];

void glow_pool_init(void);

// Pulls new key-hit events out of the core's g_last_hit_tracker and inserts them into glow_pool,
// tagging each with its kind. Caps Lock (LED 50), and every LED with its own bespoke animation
// origin (Enter/Backspace/Space) or in the protected strip (LED_PROTECTED_MIN..MAX), are never
// inserted here -- each either has its own dedicated pool below or its own indicator elsewhere,
// and must not also pick up a plain persistent reactive glow on top of/instead of that. Safe and
// intended to be called once per render pass; internally de-duplicates so repeated calls within
// the same g_rgb_timer frame value are a cheap no-op. Also the single place that tracks "when did
// a real key last go down" (see glow_ms_since_real_input()/glow_last_real_input_led() below) and
// spawns the idle animation once that's been long enough ago (spec sec 2).
void glow_pool_sync(void);

// Live "should reactive glow be wider right now" state (spec sec 3/9): either physical Shift
// held, or Caps Lock currently on. Pure live queries (get_mods()/host_keyboard_led_state()) --
// nothing to store, so this is the whole implementation, kept here so both rgb_effects.c and any
// future caller share one definition instead of duplicating the condition.
static inline bool glow_wide_radius_active(void) {
    return (get_mods() & MOD_MASK_SHIFT) != 0 || host_keyboard_led_state().caps_lock;
}

// Spec update (2026-09) sec 4: Shift's own glow must stay lit for exactly as long as it's
// physically held, not for a fixed ~2s decay the way every other key's glow works -- a plain
// glow_pool entry (a fire-and-forget timed event) can't represent "for as long as this is held"
// on its own, so LSHIFT/RSHIFT's glow is rendered as a live query of this function first
// (rgb_effects.c), falling back to a normal pool-driven fade only for the brief moment right
// after release (see glow_trigger_shift_release() below).
static inline bool glow_shift_is_held(void) {
    return (get_mods() & MOD_MASK_SHIFT) != 0;
}

// --- Real-input activity tracking (spec sec 2, 19/45, 25.1) --------------------------------
// One shared "when/where was the last genuine key hit" fact, updated once per frame inside
// glow_track_real_input() (called every scan from matrix_scan_user(), mode-independent -- see
// below) and glow_pool_sync() both feed it. Three independent consumers read it:
//   - the idle animation (this file): how long since real input, to decide whether to spawn a
//     synthetic idle event: "normal user input must always have priority over the idle
//     animation" is automatic here, since any real hit resets the clock before the next idle
//     roll is even considered.
//   - nothing else currently (Fairy Orb used to be a second consumer, for its own now-removed
//     chase-the-last-key behavior -- fifth pass, see that section below), but kept general rather
//     than baked into the one remaining caller.
// Only reflects *real* hits (g_last_hit_tracker) and encoder input (see
// glow_note_encoder_input() below), never the synthetic idle events this same file injects into
// glow_pool -- an idle event must not look like "real input" and reset its own timer, or idle
// mode would never stop retriggering itself.
uint16_t glow_ms_since_real_input(void);
uint8_t  glow_last_real_input_led(void); // GLOW_LED_NONE if there has been no real input yet
                                          // this session (e.g. the first few ms after boot).

// Spec update (2026-09) sec 5: encoder press/rotation counts as real interaction for idle-timer
// purposes, the same as a normal keystroke. Encoder events are triggered directly from
// process_record_user (glow_trigger_encoder_wave() below) rather than flowing through
// g_last_hit_tracker the way real matrix keys do, so they need their own explicit pulse here
// instead of being picked up incidentally by the same scan real keys are.
void glow_note_encoder_input(void);

// Real hardware issue reported (2026-09, third pass): filters mechanical contact bounce/chatter
// on the encoder's push-button specifically -- see custom_rgb.c for the full explanation. Call
// once per genuine press event, at every one of the encoder-press call sites (keymap.c's normal
// handling and its Cleaning Mode passthrough); true means accept it, false means treat it as the
// same physical press bouncing and suppress it (both the visual ring and, in keymap.c, the real
// Play/Pause HID send).
bool glow_encoder_press_debounce_ok(void);

// Mode-independent "did a real key go down this frame" tracking -- called from matrix_scan_user()
// (keymap.c) unconditionally, every scan, regardless of which RGB mode is currently active.
//
// Bug fixed (spec update 2026-09 sec 5): previously this tracking lived entirely inside
// glow_pool_sync(), which is only ever called from reactive_energy_render()/darkening_glow_
// render() -- i.e. only while one of those two specific modes is the one actually rendering.
// While any *other* mode was active (Row Wave, Fairy Orb, Solid Color, any stock effect),
// last_real_input_time simply never updated at all, no matter how much real typing happened.
// Switching back to reactive_energy afterward would then see a stale, long-ago timestamp and
// could start idle events almost immediately -- looking exactly like "idle events appearing
// while the user is actively typing", just one mode-switch removed from the actual typing.
// Fixed by tracking this independently of which mode is rendering; glow_pool_sync() now calls
// this too instead of duplicating the same scan inline.
void glow_track_real_input(void);

// --- Idle simulation (spec sec 2; fourth pass (2026-09): generalized across every mode) -------
// No idle-wave or idle-event code existed anywhere in the source this pass originally started
// from (checked by grepping the whole tree, not assumed) -- implemented fresh to the behavior the
// spec describes: after a period with no real key press, occasionally simulate one, at a random
// non-protected LED, indistinguishable from a real press to whichever rendering this MCU actually
// owns. Visual only -- this never calls register_code/tap_code or touches HID in any way, so it
// cannot generate real keyboard input, regardless of what's focused on the host.
//
// Fourth pass: previously this only ever *fired* while reactive_energy/darkening_glow were the
// one actually rendering (nested inside glow_pool_sync(), which only those two call) -- every
// other mode (Row Wave, Fairy Orb, any stock effect) saw no idle events at all, which is what the
// person's own request (sec 2 of their follow-up) called out directly: "trigger animations across
// *all* backlight modes, not just basic ones." The *threshold timer* was already mode-independent
// since the previous pass's own fix (glow_track_real_input(), below) -- what wasn't was the act of
// spawning something once that timer elapsed. Split cleanly along that same line:
// glow_idle_simulation_tick() (custom_rgb.c) is the new mode-independent scheduler -- call once
// per scan from matrix_scan_user(), unconditionally, like glow_track_real_input() already is --
// and it looks at rgb_matrix_get_mode() itself to decide which pool a simulated press should
// actually land in (glow_pool for reactive_energy/Darkening as before, row_wave_pool for Row Wave,
// the ripple/comet pools for those two modes) -- see rgb_effects.c's render functions for the
// consuming side of each. A mode this can't safely reach into (a stock QMK effect, or any future
// custom mode that doesn't opt in) simply gets no simulated events, the same as today, rather than
// this reaching into state it doesn't own. Fifth pass: Fairy Orb is now one of those opted-out
// modes too, on purpose -- it no longer reacts to anything, real or simulated (see its own section
// below), so the idle dispatcher no longer has a Fairy Orb branch at all.
//
// Also widened 10s -> 20s (see GLOW_IDLE_THRESHOLD_MS below) -- reported as lighting up "even
// while typing", and 10s is well within an ordinary mid-sentence pause (rereading a line, thinking
// about the next word) during a session someone would still call "actively typing", even though
// the mechanism firing at exactly that gap was working as specified, not misfiring. 20s is closer
// to "actually stepped away for a moment" while still showing the effect reasonably promptly once
// the board really is idle.
#define GLOW_IDLE_THRESHOLD_MS 20000 // how long the board must sit untouched before idle events
                                       // are eligible to start at all (fourth pass: was 10000)
#define GLOW_IDLE_MIN_GAP_MS 650    // fourth pass follow-up: reported as "about 1 second between
#define GLOW_IDLE_MAX_GAP_MS 1900   // presses... could be just a tiny bit faster" -- was 900-2600
                                     // (averaging ~1.75s, consistent with "about 1 second" as a
                                     // rough felt impression). Tightened to 650-1900 (~1.28s
                                     // average) -- a real, noticeable-but-modest speedup matching
                                     // "just a tiny bit", not a wholesale redesign of the pacing.
                                     // Still randomized in between, deliberately irregular so it
                                     // reads as "occasional", not a metronome.
// Fourth pass: roughly 1 in this many simulated *reactive_energy* events fires one of Enter/
// Backspace/Delete/Space/Escape/the encoder's own bespoke animation instead of a plain glow --
// "simulate presses for all types of keys ... react exactly as if a real user physically pressed
// them" (the person's own wording) reads as wanting the full character of the mode shown off, not
// only its plainest reactive glow. Kept rare (most idle events still land as a plain glow) so it
// reads as an occasional flourish, not a strobe of shockwaves.
#define GLOW_IDLE_SPECIAL_EVENT_ODDS 6

// Mode-independent scheduler: call once per scan from matrix_scan_user(), unconditionally,
// regardless of which RGB mode is currently active -- the same convention glow_track_real_input()
// already uses, and for the same reason (see that function's own comment). Internally decides
// *when* the next simulated press happens (identical threshold/gap timing to before) and, once due,
// dispatches it into whichever state the currently active mode actually consumes.
void glow_idle_simulation_tick(void);

// --- Enter / Backspace / Delete / Space: independent small wave pools ----------------------
//
// Each of these is a short (<300ms) takeover animation layered on top of the always-running
// normal render (rgb_effects.c renders the pool exactly as it already does every frame, then
// overlays each active wave's own pixels on top). All four must be able to run several
// concurrent instances of themselves *and* alongside each other -- a repeat trigger starts an
// additional independent wave rather than resetting or preempting whatever's already travelling.
// Fourth pass: this is very plausibly the actual root cause behind "keys stay lit if Del,
// Backspace, or Enter is pressed during a custom animation... they should start fading out or
// simply disappear" -- each of these has an EFFECT_POOL_SIZE-slot ring buffer of its *own*
// independent wave instances (below), and it's each instance's *own* growing/shrinking radius
// that erases glow_pool as it travels (rgb_effects.c's tick_effect_pools()) -- an instance that
// gets evicted (ring-buffer-overwritten by a newer press) before finishing never reaches its own
// full board-crossing radius, so whatever glow_pool entries it would have gone on to erase simply
// never get erased by anything. 8 was already sized "for e.g. Backspace under fast OS key-repeat"
// per the comment below (a prior pass's own reasoning, not new information this pass), but a
// sustained OS key-repeat rate (often 20-33 presses/second while a key like Backspace or Enter is
// held down to delete/add several lines at once) against each instance's own ~260-280ms lifetime
// needs on the order of 6-9 *just to keep up*, with zero margin for typing elsewhere at the same
// time or for more than one of these being held in sequence -- exactly the kind of real,
// reproducible-under-normal-use gap "very plausible, not just theoretical" describes. Widened 4x,
// the same proportional bump GLOW_POOL_SIZE got for the same class of reasoning (sec 35): 32 slots
// against a 260-280ms lifetime comfortably covers well over 100 presses/second, far beyond any
// real key-repeat rate, while costing only a few dozen bytes of RAM per pool (effect_wave_t is 3-4
// bytes/slot; four of these pools -- Enter/Backspace/Delete/Space -- exist).
#define EFFECT_POOL_SIZE 32

typedef struct {
    uint16_t start_time;
    bool     active;  // false = slot free. Explicit flag (rather than reusing a start_time
                        // sentinel) because 0 is a legitimate real start_time.
} effect_wave_t;

extern effect_wave_t enter_pool[EFFECT_POOL_SIZE];
extern effect_wave_t backspace_pool[EFFECT_POOL_SIZE];
extern effect_wave_t delete_pool[EFFECT_POOL_SIZE];
extern effect_wave_t space_pool[EFFECT_POOL_SIZE];

// Called from keymap.c's process_record_user on the relevant key's press (the narrow, documented
// exception to keeping RGB-effect logic out of keymap.c).
void glow_trigger_enter_explosion(void);
void glow_trigger_backspace_eraser(void);
void glow_trigger_delete_vacuum(void);
void glow_trigger_space_wave(void);

// Pool-clearing helpers used by the Enter/Backspace/Delete overlays' "erase what's been passed
// over" behavior. Squared-distance / plain-threshold, no sqrt -- same convention as the falloff
// math elsewhere. Safe to call every frame per active instance; clearing an already-cleared
// region is a no-op.
void glow_pool_clear_within_radius(uint8_t origin_x, uint8_t origin_y, uint16_t radius, uint16_t wave_start);
// Delete's inward-sweeping boundary (spec update sec 1.1) needs the opposite test: erase
// whatever is *farther* than the current (shrinking) radius, since that's the region the
// boundary has already swept past on its way in toward Delete.
void glow_pool_clear_outside_radius(uint8_t origin_x, uint8_t origin_y, uint16_t radius, uint16_t wave_start);
void glow_pool_clear_x_at_or_above(uint8_t x_threshold, uint16_t wave_start);
void glow_pool_clear_all(void);

// Spec update (2026-09) sec 4: Shift's own glow must stay lit for exactly as long as it's
// physically held (glow_shift_is_held(), above), not fade on a fixed timer the way a normal
// keypress does -- a plain glow_pool entry can't represent "for as long as this is held" on its
// own. Call this on Shift's *release* (keymap.c) to start a fresh, ordinary decay for both Shift
// LEDs from that exact moment -- see rgb_effects.c's overlay_shift_held() for the held-state
// rendering this hands off from.
void glow_trigger_shift_release(void);

// --- Escape wave(s) --------------------------------------------------------------------------
// R75 spec round (2026-09) sec 4: Escape must behave like Enter/Backspace's independent-wave
// pattern -- a second Esc press while one F-row wave is still running must spawn an *additional*
// wave, never restart/preempt the one already playing. Previously a single re-triggerable
// instance; now the same small ring-buffer-of-instances pattern as enter_pool/etc above (only
// ever touches the protected F-row strip, so it still can't collide with the normal reactive
// field those three erase/absorb/displace).
// Fourth pass: same reasoning as EFFECT_POOL_SIZE above -- Escape can also be held for OS repeat
// in plenty of software (closing several dialogs/menus in a row). 8 -> 24 (a smaller multiplier
// than Enter/Backspace/Delete/Space got: Escape's own cascade already runs longer per instance --
// ESCSEQ_TOTAL_MS -- and it's a single pool rather than four, so the same slot count buys more
// combined headroom relative to how often it's realistically triggered).
#define ESCSEQ_POOL_SIZE 24
extern effect_wave_t escseq_pool[ESCSEQ_POOL_SIZE];
void glow_trigger_escape_sequence(void);

// F-key groups: only meaningful (per spec) while no escape sequence is running --
// glow_trigger_fkey_group() itself no-ops in that case, so callers don't need to check first.
// `led` must be one of the 12 F-key LEDs (9-20).
void glow_trigger_fkey_group(uint8_t led);
// GLOW_LED_NONE if no group flash is active (or its ~250-300ms+delay window has elapsed --
// checked and self-cleared here, not by a separate tick call). Otherwise the LED that was
// pressed to start it, plus how long ago.
uint8_t glow_fkey_group_pressed_led(uint16_t *elapsed_ms_out);
void glow_fkey_group_clear(void); // called on effect re-entry (params->init), like glow_pool_init()

// True while any escape wave is currently active -- glow_trigger_fkey_group() reads this instead
// of the old single-instance special_effect_t check.
bool glow_escseq_any_active(void);

// (Sixth pass: the encoder wave animation was removed entirely -- see rgb_control.h. The encoder
// still sends volume/mute to the host exactly as before; it just no longer draws a ring.)

// --- Row Wave (spec sec 23) -------------------------------------------------------------------
// A new standalone RGB mode (its own rgb_matrix_mode, registered in rgb_matrix_user.inc) --
// pressing any key sends a wave rippling outward along that key's own *physical* LED row (real
// g_led_config y, not a matrix-row assumption: see rgb_effects.c). Same ring-buffer-of-instances
// shape as the pools above so fast typing can have several rows waving at once.
#define ROWWAVE_POOL_SIZE 8
typedef struct {
    uint16_t start_time;
    uint8_t  origin_led; // which LED (and therefore which physical row/x) the wave began at
    bool     active;
    bool     simulated; // fourth pass: true if from idle simulation -- see glow_event_t's own
                          // field (glow_pool, above) for the full reasoning; same treatment here.
} row_wave_t;
extern row_wave_t row_wave_pool[ROWWAVE_POOL_SIZE];
void row_wave_pool_init(void);
void row_wave_pool_sync(void); // same "drain new hits from g_last_hit_tracker" shape as
                                 // glow_pool_sync(), independent pool/guard -- safe since only one
                                 // custom effect is ever the active render target in a given frame.
// Inserts one synthetic instance directly -- used by glow_idle_simulation_tick() (fourth pass) to
// simulate a press while Row Wave is the active mode. row_wave_pool_sync() itself now calls this
// too for real hits, rather than duplicating the insert logic in two places.
void row_wave_pool_add(uint8_t led);

// --- Ripple Pool (fourth pass: new mode) --------------------------------------------------------
// A new standalone mode: pressing any key sends a circular ring rippling outward across the
// *whole* board from that key's exact position -- the board-wide analogue of Row Wave's
// row-restricted band. Same ring-buffer-of-instances shape as Row Wave/the bespoke pools above so
// fast typing can have several ripples expanding at once.
#define RIPPLE_POOL_SIZE 8
typedef struct {
    uint16_t start_time;
    uint8_t  origin_led;
    bool     active;
    bool     simulated; // fourth pass: see row_wave_t's own field above -- same treatment.
} ripple_wave_t;
extern ripple_wave_t ripple_wave_pool[RIPPLE_POOL_SIZE];
void ripple_pool_init(void);
void ripple_pool_sync(void); // same "drain new hits from g_last_hit_tracker" shape as
                               // row_wave_pool_sync() -- see that function's comment.
// Inserts one synthetic instance directly (no g_last_hit_tracker entry needed) -- used by
// glow_idle_simulation_tick() above to simulate a press while Ripple Pool is the active mode.
void ripple_pool_add(uint8_t led);

// --- Comet Trail (fourth pass: new mode) --------------------------------------------------------
// A new standalone mode: pressing a key launches a small bright comet from that key, travelling
// outward along a fixed 45-degree-ish diagonal (away from the board's own center, by simple
// quadrant -- see rgb_effects.c) and fading as it goes, trailed by two dimmer echoes of itself a
// short distance back along the same line. Deliberately not true per-instance vector math (which
// would need a sqrt to normalize direction, unavailable/expensive without an FPU on this MCU) --
// four fixed diagonal directions, chosen by which quadrant the origin key sits in relative to
// board center, reads as "shoots away from wherever you pressed" without needing one.
#define COMET_POOL_SIZE 6
typedef struct {
    uint16_t start_time;
    uint8_t  origin_x, origin_y; // real x/y units, not fixed-point -- direction is derived once
                                   // at trigger time and re-derived from these at render time
                                   // rather than also stored, since it's cheap and keeps this
                                   // struct the same shape as the other small wave pools.
    bool     active;
    bool     simulated; // fourth pass: see row_wave_t's own field for the full reasoning.
} comet_t;
extern comet_t comet_pool[COMET_POOL_SIZE];
void comet_pool_init(void);
void comet_pool_sync(void); // same shape as row_wave_pool_sync()/ripple_pool_sync().
void comet_pool_add(uint8_t led); // synthetic-press entry point, see ripple_pool_add() above.

// Equalizer (fourth pass) was here and has been removed again, per the person's own follow-up
// request -- both "very strange and buggy" on its own, and a separate explicit confirmation not
// to pursue real audio reactivity (which was the entire point of the concept) made it not worth
// keeping as a keystroke-only mode. RGBCTL_TARGET_EQUALIZER/EQ_NUM_BANDS/eq_band_hit_time[]/etc
// are gone from every file that had them (rgb_control.h/.c, rgb_profiles.h/.c, rgb_effects.c/.h,
// rgb_matrix_user.inc) -- if a future pass wants column-bar visuals again, treat this as a fresh
// design rather than resurrecting this exact one.

// (Sixth pass: Fairy Orb's state/motion declarations were removed from this header -- the effect is now
// self-contained in features/orb_motion.{h,c} + fairy_orb_render() in rgb_effects.c.)

// Called once from the custom effect's params->init branch (rgb_effects.c) to clear every
// reactive/typing-driven pool above at once -- effect re-entry (mode switch away and back) should
// start from a clean slate. Row Wave and Fairy Orb have their own init functions above since they
// are separate rgb_matrix modes with their own params->init moment, not part of reactive_energy.
void glow_effect_pools_clear_all(void);

// R75 spec round sec 19/45: "an old key press from another layer/context must not suddenly
// appear as new reactive input" once that context's visual suppression ends. glow_pool is the
// only pool this applies to -- Enter/Backspace/Delete/Space/Escape/encoder waves are all
// triggered explicitly from specific keycodes that simply aren't bound on Layer 1/2/3/5 (they
// physically cannot fire from there), so only the plain g_last_hit_tracker-fed pool can go stale
// this way. Called once/frame from glow_pool_sync(); tracks whether *any* visually-suppressing
// context (Fn-held, RGB Control layer, Options layer, Num layer, Cleaning Mode) was active last
// frame vs this one, and clears glow_pool on the falling edge -- any events that accumulated
// while suppressed (real key hits at raw matrix level still populate g_last_hit_tracker no matter
// what layer/mode is active) are discarded rather than suddenly "lighting up" the moment
// suppression ends. This is deliberately *not* applied to the persistent-animation pools (Enter/
// Backspace/Delete/Space/Escape/encoder) -- those are the "an animation that's intentionally
// still running must not restart" half of sec 45's distinction, and clearing them here would be
// the wrong kind of fix for a problem they don't have.
