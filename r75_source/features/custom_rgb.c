#include "custom_rgb.h"
#include "rgb_control.h" // rgbctl_get_effect_speed()/RGBCTL_TARGET_ORB -- Fairy Orb's own
                           // independent speed (fifth pass); this file otherwise owns state, not
                           // color/speed access, but Orb's cruise speed lives there now.
#include <stdlib.h> // rand()/srand() -- already linked in by this toolchain regardless (QMK's
                     // own stock digital-rain effect uses rand() the same way), just needed a
                     // header for the idle animation and Fairy Orb's random-target picking below.

glow_event_t glow_pool[GLOW_POOL_SIZE];
static uint8_t glow_next_slot;

// LED indices that get non-NORMAL treatment, or are excluded from the pool entirely because
// they have their own bespoke animation/indicator. Derived programmatically from keyboard.json's
// rgb_matrix.layout (matrix -> LED index) cross-referenced against keymap.c's layer-0 LAYOUT()
// call (matrix -> keycode).
#define LED_TAB       49
#define LED_CAPS      50
#define LED_LSHIFT    75
#define LED_RSHIFT    64
#define LED_BACKSPACE 35
#define LED_ENTER     62
#define LED_SPACE     79

static bool is_modifier_led(uint8_t led) {
    switch (led) {
        case 76: // LCtrl
        case 77: // LWin / LGUI
        case 78: // LAlt
        case 0:  // RAlt
        case 1:  // Fn, MO(1)
        case 75: // LShift
        case 64: // RShift
            return true;
        default:
            return false;
    }
}

static glow_kind_t classify_led(uint8_t led) {
    if (led == LED_TAB) {
        return GLOW_KIND_TAB;
    }
    if (is_modifier_led(led)) {
        return GLOW_KIND_MODIFIER;
    }
    // R75 spec round (2026-09) sec 9: decided once, here, at the moment this event is created --
    // see custom_rgb.h's comment on GLOW_KIND_NORMAL_WIDE for why this can't be a live check at
    // render time any more.
    return glow_wide_radius_active() ? GLOW_KIND_NORMAL_WIDE : GLOW_KIND_NORMAL;
}

// A LED excluded from the plain reactive pool for any reason -- own bespoke animation, own
// indicator, or the protected strip. Reused by the idle animation below so a synthetic event can
// never land somewhere a real one never would.
static bool is_glow_pool_excluded_led(uint8_t led) {
    return led == LED_CAPS || led == LED_BACKSPACE || led == LED_ENTER || led == LED_SPACE || is_protected_led(led);
}

void glow_pool_init(void) {
    for (uint8_t i = 0; i < GLOW_POOL_SIZE; i++) {
        glow_pool[i].led_index  = GLOW_LED_NONE;
        glow_pool[i].start_time = 0;
        glow_pool[i].kind       = GLOW_KIND_NORMAL;
        glow_pool[i].simulated  = false;
    }
    glow_next_slot = 0;
    srand(timer_read32()); // reseed on every (re-)entry to the reactive effect -- only consumed
                             // by the idle animation and Fairy Orb, neither of which need
                             // cryptographic quality, just "not the same sequence every boot".
}

// Fourth pass: `simulated` added as a real parameter (was implicitly always false) -- every
// existing call site below passes false explicitly; only the idle-simulation dispatcher passes
// true. Keeping it an explicit parameter here, rather than a separate glow_pool_add_simulated()
// wrapper, means adding a future caller can't silently forget to think about which one it means.
static void glow_pool_add(uint8_t led, uint16_t start_time, glow_kind_t kind, bool simulated) {
    glow_pool[glow_next_slot].led_index  = led;
    glow_pool[glow_next_slot].start_time = start_time;
    glow_pool[glow_next_slot].kind       = (uint8_t)kind;
    glow_pool[glow_next_slot].simulated  = simulated;
    glow_next_slot                       = (glow_next_slot + 1) % GLOW_POOL_SIZE;
}

// True if glow_pool already holds this exact (led, start_time) event.
static bool glow_pool_has_event(uint8_t led, uint16_t start_time) {
    for (uint8_t i = 0; i < GLOW_POOL_SIZE; i++) {
        if (glow_pool[i].led_index == led && glow_pool[i].start_time == start_time) {
            return true;
        }
    }
    return false;
}

// --- Real-input activity tracking --------------------------------------------------------------
// Fifth pass: was uint16_t, with glow_ms_since_real_input() below doing 16-bit modular
// subtraction -- which wraps every 65.5s, so after ~65.5s of *genuine* inactivity it started
// reporting a small number again (as if input had just happened) and idle simulation quietly
// switched itself off for another ~20s before resuming, then repeated -- an idle cycle of ~45s on,
// ~20s off, forever, instead of "on from 20s onward". Also opened a 12ms window every 65.5s where
// glow_encoder_rotation_ok() below would see "input just now" out of nowhere. g_rgb_timer itself
// is 32-bit (extern uint32_t in quantum/rgb_matrix/rgb_matrix.h), so keep the timestamp 32-bit too
// and saturate the reported age at 65,535ms instead of wrapping -- "idle for at least a minute
// or so" is all any caller here needs to know, and it must stay true from then on.
static uint32_t last_real_input_time;
static uint8_t  last_real_input_led = GLOW_LED_NONE;
static uint32_t last_input_tracked_frame = 0; // separate guard from glow_pool_sync()'s own --
                                                // this has to run even on frames where that
                                                // function is never called at all (see below)
static uint16_t idle_next_event_at; // g_rgb_timer-scale; only meaningful once idle has started --
                                       // declared here (rather than down in the "Idle animation"
                                       // section below, where it conceptually belongs) because
                                       // glow_note_encoder_input()/glow_track_real_input() need to
                                       // clear it and are used from outside this file before the
                                       // idle section's own functions are defined.

uint16_t glow_ms_since_real_input(void) {
    uint32_t elapsed = g_rgb_timer - last_real_input_time;
    return (elapsed > 0xFFFF) ? 0xFFFF : (uint16_t)elapsed; // saturate, never wrap -- see above
}

uint8_t glow_last_real_input_led(void) {
    return last_real_input_led;
}

void glow_note_encoder_input(void) {
    last_real_input_time = g_rgb_timer;
    idle_next_event_at    = 0;
}

// Real hardware issue reported (2026-09, third pass): the encoder's push-button (a normal matrix
// key, row 0 col 14 -- see keymap.c) firing repeatedly in quick bursts while otherwise idle,
// including while typing elsewhere on the board -- consistent with mechanical contact bounce/
// chatter on that specific switch rather than anything in this codebase's own trigger logic
// (every call site already only fires on a genuine `record->event.pressed`, verified, not
// assumed). A person cannot deliberately repeat a single button press faster than roughly
// 100-150ms even trying to double-tap quickly, so anything faster than that is almost certainly
// the same physical press bouncing, not a second one -- this filters those out at the point where
// the encoder-press call site funnels through, gating the real Play/Pause HID send (keymap.c) so a
// bounce doesn't toggle media playback twice. (Sixth pass: the encoder's visual ring was removed; this
// debounce is kept because it protects the HID action, not the animation.)
// Deliberately does *not* apply anything like this to CW/CCW rotation -- legitimate fast
// scrolling needs to generate many events in quick succession, so the same "ignore rapid repeats"
// approach would break real use rather than filter noise; if spurious *rotation* (not just press)
// keeps happening after this, that's more likely a genuine hardware issue this can't safely mask.
#define ENCODER_PRESS_DEBOUNCE_MS 150
// Fifth pass: 32-bit timer (was timer_read()/timer_elapsed(), both 16-bit and wrapping every
// 65.5s): a genuine press landing at almost exactly a whole multiple of 65.5s (plus under 150ms)
// after the previous accepted one would alias into "a bounce" and have its Play/Pause silently
// swallowed. Rare per press (~0.2%), but it is a real, deterministic hole in something that is
// supposed to be reliable.
static uint32_t last_encoder_press_accepted_at;
static bool     encoder_press_debounce_primed = false;

bool glow_encoder_press_debounce_ok(void) {
    if (encoder_press_debounce_primed && timer_elapsed32(last_encoder_press_accepted_at) < ENCODER_PRESS_DEBOUNCE_MS) {
        return false;
    }
    encoder_press_debounce_primed  = true;
    last_encoder_press_accepted_at = timer_read32();
    return true;
}

// Sixth pass: glow_encoder_rotation_ok() (a 12 ms "typing proximity" guard) was removed together with the
// encoder wave animation it existed to protect -- it only ever decided whether to *draw the ring*, never
// whether a rotation tick was sent to the host, so rotation itself (volume up/down) is untouched by this.

// Fourth pass: reported as "if you press a key while auto-pressing is active, keys continue to
// light up randomly... the animation has to finish its cycle before stopping" -- accurate: an
// idle-simulated pool entry used to be visually indistinguishable from a real one (by design, so
// it looks natural while idle -- see glow_idle_simulation_tick()'s own comment), which also meant
// it kept fading out on its own normal schedule even after real typing resumed, same as any real
// key's glow would. Each `simulated` field (glow_event_t/row_wave_t/ripple_wave_t/comet_t) exists
// so this one function -- called from glow_track_real_input() below, which already knows, this
// exact frame, that real input has just arrived -- can force every still-active simulated entry,
// across every pool at once (regardless of which mode is currently rendering, same reasoning as
// glow_track_real_input() itself being mode-independent), to stop immediately instead of running
// out its normal cycle.
static void force_clear_all_simulated(void) {
    for (uint8_t i = 0; i < GLOW_POOL_SIZE; i++) {
        if (glow_pool[i].simulated) {
            glow_pool[i].led_index = GLOW_LED_NONE;
        }
    }
    for (uint8_t i = 0; i < ROWWAVE_POOL_SIZE; i++) {
        if (row_wave_pool[i].simulated) {
            row_wave_pool[i].active = false;
        }
    }
    for (uint8_t i = 0; i < RIPPLE_POOL_SIZE; i++) {
        if (ripple_wave_pool[i].simulated) {
            ripple_wave_pool[i].active = false;
        }
    }
    for (uint8_t i = 0; i < COMET_POOL_SIZE; i++) {
        if (comet_pool[i].simulated) {
            comet_pool[i].active = false;
        }
    }
}

void glow_track_real_input(void) {
    if (g_rgb_timer == last_input_tracked_frame) {
        return;
    }
    last_input_tracked_frame = g_rgb_timer;

    uint8_t count = g_last_hit_tracker.count;
    for (uint8_t j = 0; j < count; j++) {
        last_real_input_led = g_last_hit_tracker.index[j];
    }
    if (count > 0) {
        last_real_input_time = g_rgb_timer;
        idle_next_event_at    = 0; // cancel any pending idle event -- real input always wins
        force_clear_all_simulated();
    }
}

// Fifth pass: the simulated-input tracking that used to live here (last_simulated_led/_tick_at,
// note_simulated_press(), glow_last_simulated_led()/glow_last_simulated_tick()) is removed --
// Fairy Orb's chase behavior was its only consumer, and Fairy Orb no longer reacts to anything
// (see its own section below), so this is genuinely dead state now rather than something to keep
// around unused.

// --- Layer/mode-transition staleness (spec sec 19/45) -----------------------------------------
static bool overlay_was_suppressing_last_frame = false;

static bool any_overlay_suppressing_now(void) {
    return IS_LAYER_ON(1) || IS_LAYER_ON(2) || IS_LAYER_ON(3) || IS_LAYER_ON(5);
    // Cleaning Mode deliberately not included here: while it's active, reactive_energy_render()
    // (the only caller of glow_pool_sync()) never actually reaches this LED range with anything
    // visible -- indicators.c's render_cleaning_mode() takes over unconditionally and returns
    // early -- and, more importantly, Cleaning Mode blocks process_record_user for everything
    // except Fn itself and the five whitelisted glow_trigger_*() calls, none of which touch
    // glow_pool. There is nothing for this function to protect against there.
}

static void glow_pool_check_staleness(void) {
    bool suppressing_now = any_overlay_suppressing_now();
    if (overlay_was_suppressing_last_frame && !suppressing_now) {
        glow_pool_clear_all(); // falling edge: whatever accumulated while suppressed is stale,
                                 // not "just happened" -- see custom_rgb.h's comment on this.
    }
    overlay_was_suppressing_last_frame = suppressing_now;
}

// Spec update (2026-09) sec 18: row_wave_pool needs the identical falling-edge staleness clear
// glow_pool already had -- it's fed from the same raw g_last_hit_tracker regardless of layer/
// mode, so it was just as capable of accumulating stale events while Layer 2/3/5 suppressed its
// visuals and then suddenly animating them once that suppression ended, and previously had no
// such clear at all. Own static tracking flag rather than sharing glow_pool's, since Row Wave and
// reactive_energy/Darkening are never the active mode at the same time (only one custom effect
// ever renders in a given frame) but each still needs the transition edge detected independently
// against whichever frame it actually resumes rendering on.
static bool row_wave_was_suppressing_last_frame = false;

static void row_wave_pool_check_staleness(void) {
    bool suppressing_now = any_overlay_suppressing_now();
    if (row_wave_was_suppressing_last_frame && !suppressing_now) {
        for (uint8_t i = 0; i < ROWWAVE_POOL_SIZE; i++) {
            row_wave_pool[i].active = false;
        }
    }
    row_wave_was_suppressing_last_frame = suppressing_now;
}

void glow_pool_sync(void) {
    static uint32_t last_synced_frame = 0;
    // g_last_hit_tracker is a stable per-frame snapshot (see custom_rgb.h) -- re-scanning it
    // again for the same g_rgb_timer value can't find anything new.
    if (g_rgb_timer == last_synced_frame) {
        return;
    }
    last_synced_frame = g_rgb_timer;

    glow_pool_check_staleness();
    glow_track_real_input(); // idle-timer bookkeeping, factored out so it also runs from
                               // matrix_scan_user() regardless of which mode is active -- see
                               // that function's own comment (custom_rgb.h) for why this can't
                               // just live inline here the way it used to.

    uint8_t count = g_last_hit_tracker.count;
    for (uint8_t j = 0; j < count; j++) {
        uint8_t led = g_last_hit_tracker.index[j];
        // Never part of the plain reactive-glow pool, at any stage:
        //  - Caps Lock and the protected F-row/Esc/Delete strip have their own indicators and
        //    must never pick up (or bleed) ordinary reactive glow -- this is also what stops
        //    e.g. pressing "5" from bleeding a glow up into the F-row above it, since that row's
        //    LEDs simply never enter the pool to begin with.
        //  - Backspace/Enter/Space each get their own bespoke animation from their own LED and
        //    must not *also* carry a plain persistent glow there.
        if (is_glow_pool_excluded_led(led)) {
            continue;
        }
        // Absolute moment this hit occurred, on the same truncated ms scale as g_rgb_timer.
        // Invariant across frames for the same underlying key-hit (g_rgb_timer and tick[]
        // advance together in real time) -- that invariance is what makes exact-match
        // de-duplication below correct rather than approximate.
        uint16_t start_time = (uint16_t)(g_rgb_timer - g_last_hit_tracker.tick[j]);
        if (glow_pool_has_event(led, start_time)) {
            continue; // already tracked
        }
        glow_pool_add(led, start_time, classify_led(led), false); // real key
        // Shift synchronization: either Shift also lights the other Shift's LED, at the same
        // start_time (so both decay in lockstep) -- this is what provides Shift's brief
        // release-fade tail if pressed and released quickly, before glow_trigger_shift_release()
        // even matters. Purely an event-pool entry, same as every other key here -- never
        // touches the actual keycode/registration. See rgb_effects.c's overlay_shift_held() for
        // the live, hold-duration-aware rendering that takes over for as long as Shift is
        // actually held (spec update sec 4).
        if (led == LED_LSHIFT && !glow_pool_has_event(LED_RSHIFT, start_time)) {
            glow_pool_add(LED_RSHIFT, start_time, GLOW_KIND_MODIFIER, false); // real (Shift pair)
        } else if (led == LED_RSHIFT && !glow_pool_has_event(LED_LSHIFT, start_time)) {
            glow_pool_add(LED_LSHIFT, start_time, GLOW_KIND_MODIFIER, false); // real (Shift pair)
        }
    }
}

// --- Idle simulation (fourth pass: mode-independent scheduler) --------------------------------
// See custom_rgb.h's own comment on glow_idle_simulation_tick() for the full "why" -- this used to
// be nested inside glow_pool_sync() (so it only ever fired while reactive_energy/Darkening were
// the one rendering); it's now called every scan from matrix_scan_user(), unconditionally, the
// same way glow_track_real_input() already is, and decides for itself which mode is active and
// what a simulated press should feed there.

static uint16_t idle_random_gap(void) {
    return GLOW_IDLE_MIN_GAP_MS + (uint16_t)(rand() % (GLOW_IDLE_MAX_GAP_MS - GLOW_IDLE_MIN_GAP_MS));
}

// Picks a random non-excluded LED the same way a real reactive-glow press could land on (retry a
// few times if the draw lands on an excluded one -- ~18/80 LEDs are excluded, so this converges in
// 1-2 tries almost always; a hard cap keeps it bounded). Shared by every per-mode branch below
// that wants "a random ordinary key", so they can't disagree with each other about which LEDs are
// fair game for a simulated press.
static uint8_t idle_pick_random_led(void) {
    for (uint8_t tries = 0; tries < 10; tries++) {
        uint8_t candidate = (uint8_t)(rand() % RGB_MATRIX_LED_COUNT);
        if (!is_glow_pool_excluded_led(candidate)) {
            return candidate;
        }
    }
    return GLOW_LED_NONE;
}

// Occasionally (see GLOW_IDLE_SPECIAL_EVENT_ODDS) a simulated press in reactive_energy/Darkening
// fires one of Enter/Backspace/Delete/Space/Escape's own bespoke animation instead of
// a plain glow -- reuses the exact same trigger functions a real press of each already calls, so
// the result is genuinely indistinguishable from a real one, not a separate lookalike path.
static void idle_fire_special_reactive_event(void) {
    switch (rand() % 5) {
        case 0: glow_trigger_enter_explosion(); break;
        case 1: glow_trigger_backspace_eraser(); break;
        case 2: glow_trigger_delete_vacuum(); break;
        case 3: glow_trigger_space_wave(); break;
        default: glow_trigger_escape_sequence(); break;
    }
}

// Dispatches one simulated press into whichever state the *currently active* custom RGB mode
// actually consumes. A mode this can't safely reach into (a stock QMK effect, or any future custom
// mode that doesn't opt in here) simply gets nothing, same as before this pass -- there's no core
// state this could reach into for those even if it tried (see HANDOFF.md on why core patches are
// avoided throughout this project).
static void idle_fire_simulated_event(void) {
    uint8_t mode = rgb_matrix_get_mode();

    if (mode == RGB_MATRIX_CUSTOM_reactive_energy || mode == RGB_MATRIX_CUSTOM_darkening_glow) {
        if ((rand() % GLOW_IDLE_SPECIAL_EVENT_ODDS) == 0) {
            idle_fire_special_reactive_event();
            return;
        }
        uint8_t led = idle_pick_random_led();
        if (led != GLOW_LED_NONE) {
            glow_pool_add(led, g_rgb_timer, classify_led(led), true); // idle-simulated
        }
    } else if (mode == RGB_MATRIX_CUSTOM_row_wave) {
        uint8_t led = idle_pick_random_led();
        if (led != GLOW_LED_NONE) {
            row_wave_pool_add(led);
        }
    } else if (mode == RGB_MATRIX_CUSTOM_ripple_pool) {
        uint8_t led = idle_pick_random_led();
        if (led != GLOW_LED_NONE) {
            ripple_pool_add(led);
        }
    } else if (mode == RGB_MATRIX_CUSTOM_comet_trail) {
        uint8_t led = idle_pick_random_led();
        if (led != GLOW_LED_NONE) {
            comet_pool_add(led);
        }
    }
    // Darkening is handled by the reactive_energy branch above (shares glow_pool -- see
    // rgb_effects.c's darkening_glow_render()). Fairy Orb (fifth pass: now fully non-reactive, on
    // purpose -- see its own section below) and any other active mode (a stock QMK effect,
    // Rainbow Wave (R->L)) simply get no simulated event.
}

void glow_idle_simulation_tick(void) {
    if (glow_ms_since_real_input() < GLOW_IDLE_THRESHOLD_MS) {
        return; // real input in the last few seconds always wins -- nothing to do
    }
    if (any_overlay_suppressing_now()) {
        return; // Fn-held/RGB Settings/Options/Num layer: a simulated press here would land on
                 // state those layers' own indicators would immediately blank anyway, and -- for
                 // the bespoke Enter/Backspace/Delete/Space/Escape/encoder effects specifically --
                 // is state that (unlike real input) *could* reach into their pools from a layer a
                 // real press of those keycodes physically cannot fire from, which is exactly the
                 // staleness class of bug sec 19/45 already exists to prevent. Simplest correct
                 // fix: don't generate one while any of those is active, the same as a real press
                 // of Enter/Backspace/etc. couldn't either.
    }
    if (idle_next_event_at == 0) {
        // First frame that crosses the threshold: schedule the first event a random short gap
        // from now rather than firing immediately, so it doesn't flash the instant the threshold
        // ticks over. (idle_next_event_at is reset to 0 on every real hit -- see
        // glow_track_real_input()/glow_note_encoder_input() -- so this branch is also what re-arms
        // idle mode the next time the board goes quiet.)
        idle_next_event_at = g_rgb_timer + idle_random_gap();
        return;
    }
    if ((int16_t)(g_rgb_timer - idle_next_event_at) < 0) {
        return; // scheduled moment hasn't arrived yet
    }

    idle_fire_simulated_event();
    idle_next_event_at = g_rgb_timer + idle_random_gap();
    if (idle_next_event_at == 0) {
        idle_next_event_at = 1; // extremely rare wraparound landing exactly on the "unscheduled"
                                  // sentinel -- nudge by 1ms so this frame doesn't misread itself
                                  // as "just re-armed" and silently swallow one gap.
    }
}

void glow_trigger_shift_release(void) {
    glow_pool_add(LED_LSHIFT, g_rgb_timer, GLOW_KIND_MODIFIER, false); // real
    glow_pool_add(LED_RSHIFT, g_rgb_timer, GLOW_KIND_MODIFIER, false); // real
}

// Fifth pass: every erase sweep (Enter/Backspace/Delete) used to clear every glow_pool entry inside
// its reach regardless of *when that entry started* -- i.e. it wiped not just what was lit when the
// key was pressed, but also anything typed *during* the sweep, the instant the sweep's reach got to
// it. That was tolerable when the sweeps were ~200-280ms; Enter's is now a slower, drop-like ripple
// (~720ms, see rgb_effects.c) and typing the first letters of the next line while it's still
// expanding would have lit each key for a single frame at most before the ring erased it. An erase
// should only remove glow that was already there when it began -- newer entries are left alone.
// Wrap-safe (16-bit modular compare, same convention as every other start_time in this file);
// equal timestamps count as "not older" and are kept.
static inline bool entry_newer_than_wave(const glow_event_t *e, uint16_t wave_start) {
    return (int16_t)(e->start_time - wave_start) >= 0;
}

void glow_pool_clear_within_radius(uint8_t origin_x, uint8_t origin_y, uint16_t radius, uint16_t wave_start) {
    uint32_t radius_sq = (uint32_t)radius * radius;
    for (uint8_t i = 0; i < GLOW_POOL_SIZE; i++) {
        if (glow_pool[i].led_index == GLOW_LED_NONE || entry_newer_than_wave(&glow_pool[i], wave_start)) {
            continue;
        }
        int16_t  dx      = (int16_t)g_led_config.point[glow_pool[i].led_index].x - (int16_t)origin_x;
        int16_t  dy      = (int16_t)g_led_config.point[glow_pool[i].led_index].y - (int16_t)origin_y;
        uint32_t dist_sq = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);
        if (dist_sq <= radius_sq) {
            glow_pool[i].led_index = GLOW_LED_NONE;
        }
    }
}

void glow_pool_clear_x_at_or_above(uint8_t x_threshold, uint16_t wave_start) {
    for (uint8_t i = 0; i < GLOW_POOL_SIZE; i++) {
        if (glow_pool[i].led_index == GLOW_LED_NONE || entry_newer_than_wave(&glow_pool[i], wave_start)) {
            continue;
        }
        if (g_led_config.point[glow_pool[i].led_index].x >= x_threshold) {
            glow_pool[i].led_index = GLOW_LED_NONE;
        }
    }
}

void glow_pool_clear_outside_radius(uint8_t origin_x, uint8_t origin_y, uint16_t radius, uint16_t wave_start) {
    uint32_t radius_sq = (uint32_t)radius * radius;
    for (uint8_t i = 0; i < GLOW_POOL_SIZE; i++) {
        if (glow_pool[i].led_index == GLOW_LED_NONE || entry_newer_than_wave(&glow_pool[i], wave_start)) {
            continue;
        }
        int16_t  dx      = (int16_t)g_led_config.point[glow_pool[i].led_index].x - (int16_t)origin_x;
        int16_t  dy      = (int16_t)g_led_config.point[glow_pool[i].led_index].y - (int16_t)origin_y;
        uint32_t dist_sq = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);
        if (dist_sq > radius_sq) {
            glow_pool[i].led_index = GLOW_LED_NONE;
        }
    }
}

void glow_pool_clear_all(void) {
    for (uint8_t i = 0; i < GLOW_POOL_SIZE; i++) {
        glow_pool[i].led_index = GLOW_LED_NONE;
    }
}

// --- Enter / Backspace / Delete / Space: independent small wave pools -----------------------

effect_wave_t enter_pool[EFFECT_POOL_SIZE];
effect_wave_t backspace_pool[EFFECT_POOL_SIZE];
effect_wave_t delete_pool[EFFECT_POOL_SIZE];
effect_wave_t space_pool[EFFECT_POOL_SIZE];

static uint8_t enter_next_slot, backspace_next_slot, delete_next_slot, space_next_slot;

static void effect_wave_add(effect_wave_t *pool, uint8_t *next_slot, uint8_t pool_size) {
    pool[*next_slot].start_time = g_rgb_timer;
    pool[*next_slot].active     = true;
    *next_slot                  = (*next_slot + 1) % pool_size;
}

void glow_trigger_enter_explosion(void) {
    effect_wave_add(enter_pool, &enter_next_slot, EFFECT_POOL_SIZE);
}

void glow_trigger_backspace_eraser(void) {
    effect_wave_add(backspace_pool, &backspace_next_slot, EFFECT_POOL_SIZE);
}

void glow_trigger_delete_vacuum(void) {
    effect_wave_add(delete_pool, &delete_next_slot, EFFECT_POOL_SIZE);
}

void glow_trigger_space_wave(void) {
    effect_wave_add(space_pool, &space_next_slot, EFFECT_POOL_SIZE);
}

// --- Escape wave(s) ------------------------------------------------------------------------
// Same independent-instances pattern as Enter/Backspace/Delete/Space above -- see custom_rgb.h.

effect_wave_t escseq_pool[ESCSEQ_POOL_SIZE];
static uint8_t escseq_next_slot;

void glow_trigger_escape_sequence(void) {
    effect_wave_add(escseq_pool, &escseq_next_slot, ESCSEQ_POOL_SIZE);
}

bool glow_escseq_any_active(void) {
    for (uint8_t i = 0; i < ESCSEQ_POOL_SIZE; i++) {
        if (escseq_pool[i].active) {
            return true;
        }
    }
    return false;
}

// --- F-key groups ------------------------------------------------------------------------

#define FGROUP_MAX_TRACK_MS 400 // generous upper bound (max ~60ms delay + ~300ms fade) past
                                 // which state is treated as expired without a separate tick
                                 // call -- rgb_effects.c computes the precise per-LED timing.

static uint8_t  fgroup_pressed_led = GLOW_LED_NONE;
static uint16_t fgroup_start_time  = 0;

void glow_trigger_fkey_group(uint8_t led) {
    if (glow_escseq_any_active()) {
        return; // only meaningful while no escape wave is running
    }
    fgroup_pressed_led = led;
    fgroup_start_time  = g_rgb_timer;
}

uint8_t glow_fkey_group_pressed_led(uint16_t *elapsed_ms_out) {
    if (fgroup_pressed_led == GLOW_LED_NONE) {
        return GLOW_LED_NONE;
    }
    uint16_t elapsed = (uint16_t)(g_rgb_timer - fgroup_start_time);
    if (elapsed >= FGROUP_MAX_TRACK_MS) {
        fgroup_pressed_led = GLOW_LED_NONE;
        return GLOW_LED_NONE;
    }
    if (elapsed_ms_out) {
        *elapsed_ms_out = elapsed;
    }
    return fgroup_pressed_led;
}

void glow_fkey_group_clear(void) {
    fgroup_pressed_led = GLOW_LED_NONE;
}

void glow_effect_pools_clear_all(void) {
    for (uint8_t i = 0; i < EFFECT_POOL_SIZE; i++) {
        enter_pool[i].active     = false;
        backspace_pool[i].active = false;
        delete_pool[i].active    = false;
        space_pool[i].active     = false;
    }
    for (uint8_t i = 0; i < ESCSEQ_POOL_SIZE; i++) {
        escseq_pool[i].active = false;
    }
    glow_fkey_group_clear();
    idle_next_event_at = 0;
}

// --- Row Wave (spec sec 23) ------------------------------------------------------------------

row_wave_t     row_wave_pool[ROWWAVE_POOL_SIZE];
static uint8_t row_wave_next_slot;

void row_wave_pool_init(void) {
    for (uint8_t i = 0; i < ROWWAVE_POOL_SIZE; i++) {
        row_wave_pool[i].active = false;
    }
    row_wave_next_slot = 0;
}

// De-duplicates against row_wave_pool itself, exactly the same shape glow_pool_sync() uses
// against glow_pool -- see that function's comment. Independent per-frame guard, safe alongside
// glow_pool_sync() even though only one of the two is ever actually called in a given frame
// (only one custom effect renders per frame).
void row_wave_pool_add(uint8_t led) {
    row_wave_pool[row_wave_next_slot].start_time = g_rgb_timer;
    row_wave_pool[row_wave_next_slot].origin_led = led;
    row_wave_pool[row_wave_next_slot].active     = true;
    row_wave_pool[row_wave_next_slot].simulated  = true; // this function is only ever called by
                                                            // the idle-simulation dispatcher
    row_wave_next_slot                            = (row_wave_next_slot + 1) % ROWWAVE_POOL_SIZE;
}

void row_wave_pool_sync(void) {
    static uint32_t last_synced_frame = 0;
    if (g_rgb_timer == last_synced_frame) {
        return;
    }
    last_synced_frame = g_rgb_timer;

    row_wave_pool_check_staleness();
    glow_track_real_input(); // Row Wave is a full standalone mode -- it needs the same
                               // mode-independent idle-timer bookkeeping glow_pool_sync() does,
                               // in case the user switches to reactive_energy right after typing
                               // here (see that function's comment for the bug this avoids).

    uint8_t count = g_last_hit_tracker.count;
    for (uint8_t j = 0; j < count; j++) {
        uint8_t  led        = g_last_hit_tracker.index[j];
        uint16_t start_time = (uint16_t)(g_rgb_timer - g_last_hit_tracker.tick[j]);
        bool     already    = false;
        for (uint8_t i = 0; i < ROWWAVE_POOL_SIZE; i++) {
            if (row_wave_pool[i].active && row_wave_pool[i].origin_led == led && row_wave_pool[i].start_time == start_time) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        row_wave_pool[row_wave_next_slot].start_time  = start_time;
        row_wave_pool[row_wave_next_slot].origin_led  = led;
        row_wave_pool[row_wave_next_slot].active      = true;
        row_wave_pool[row_wave_next_slot].simulated   = false; // real key
        row_wave_next_slot                             = (row_wave_next_slot + 1) % ROWWAVE_POOL_SIZE;
    }
}

// --- Ripple Pool (fourth pass: new mode) ---------------------------------------------------
// Board-wide analogue of Row Wave above -- same shape throughout, just without the row
// restriction (that's entirely in rgb_effects.c's render, not here: this file only tracks
// *when/where* each ripple started).

ripple_wave_t  ripple_wave_pool[RIPPLE_POOL_SIZE];
static uint8_t ripple_next_slot;
static bool     ripple_was_suppressing_last_frame = false;

void ripple_pool_init(void) {
    for (uint8_t i = 0; i < RIPPLE_POOL_SIZE; i++) {
        ripple_wave_pool[i].active = false;
    }
    ripple_next_slot = 0;
}

void ripple_pool_add(uint8_t led) {
    ripple_wave_pool[ripple_next_slot].start_time = g_rgb_timer;
    ripple_wave_pool[ripple_next_slot].origin_led = led;
    ripple_wave_pool[ripple_next_slot].active     = true;
    ripple_wave_pool[ripple_next_slot].simulated  = true; // idle-simulation dispatcher only
    ripple_next_slot                          = (ripple_next_slot + 1) % RIPPLE_POOL_SIZE;
}

void ripple_pool_sync(void) {
    static uint32_t last_synced_frame = 0;
    if (g_rgb_timer == last_synced_frame) {
        return;
    }
    last_synced_frame = g_rgb_timer;

    bool suppressing_now = any_overlay_suppressing_now(); // same falling-edge staleness pattern
    if (ripple_was_suppressing_last_frame && !suppressing_now) { // as row_wave_pool_check_
        for (uint8_t i = 0; i < RIPPLE_POOL_SIZE; i++) {          // staleness() -- see sec 19/45.
            ripple_wave_pool[i].active = false;
        }
    }
    ripple_was_suppressing_last_frame = suppressing_now;

    glow_track_real_input();

    uint8_t count = g_last_hit_tracker.count;
    for (uint8_t j = 0; j < count; j++) {
        uint8_t  led        = g_last_hit_tracker.index[j];
        uint16_t start_time = (uint16_t)(g_rgb_timer - g_last_hit_tracker.tick[j]);
        bool     already    = false;
        for (uint8_t i = 0; i < RIPPLE_POOL_SIZE; i++) {
            if (ripple_wave_pool[i].active && ripple_wave_pool[i].origin_led == led && ripple_wave_pool[i].start_time == start_time) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        ripple_wave_pool[ripple_next_slot].start_time = start_time;
        ripple_wave_pool[ripple_next_slot].origin_led = led;
        ripple_wave_pool[ripple_next_slot].active     = true;
        ripple_wave_pool[ripple_next_slot].simulated  = false; // real key
        ripple_next_slot                          = (ripple_next_slot + 1) % RIPPLE_POOL_SIZE;
    }
}

// --- Comet Trail (fourth pass: new mode) ----------------------------------------------------

comet_t        comet_pool[COMET_POOL_SIZE];
static uint8_t comet_next_slot;
static bool     comet_was_suppressing_last_frame = false;

void comet_pool_init(void) {
    for (uint8_t i = 0; i < COMET_POOL_SIZE; i++) {
        comet_pool[i].active = false;
    }
    comet_next_slot = 0;
}

static void comet_pool_add_at(uint8_t led, uint16_t start_time, bool simulated) {
    // g_led_config.point[].x/.y are the same board-relative units used everywhere else in this
    // file (see custom_rgb.h's top-of-file comment) -- no unit conversion needed.
    comet_pool[comet_next_slot].start_time = start_time;
    comet_pool[comet_next_slot].origin_x   = g_led_config.point[led].x;
    comet_pool[comet_next_slot].origin_y   = g_led_config.point[led].y;
    comet_pool[comet_next_slot].active     = true;
    comet_pool[comet_next_slot].simulated  = simulated;
    comet_next_slot                         = (comet_next_slot + 1) % COMET_POOL_SIZE;
}

void comet_pool_add(uint8_t led) {
    comet_pool_add_at(led, g_rgb_timer, true); // idle-simulation dispatcher only
}

void comet_pool_sync(void) {
    static uint32_t last_synced_frame = 0;
    if (g_rgb_timer == last_synced_frame) {
        return;
    }
    last_synced_frame = g_rgb_timer;

    bool suppressing_now = any_overlay_suppressing_now();
    if (comet_was_suppressing_last_frame && !suppressing_now) {
        for (uint8_t i = 0; i < COMET_POOL_SIZE; i++) {
            comet_pool[i].active = false;
        }
    }
    comet_was_suppressing_last_frame = suppressing_now;

    glow_track_real_input();

    uint8_t count = g_last_hit_tracker.count;
    for (uint8_t j = 0; j < count; j++) {
        uint8_t  led        = g_last_hit_tracker.index[j];
        uint16_t start_time = (uint16_t)(g_rgb_timer - g_last_hit_tracker.tick[j]);
        int16_t  ox         = g_led_config.point[led].x;
        int16_t  oy         = g_led_config.point[led].y;
        bool     already    = false;
        for (uint8_t i = 0; i < COMET_POOL_SIZE; i++) {
            if (comet_pool[i].active && comet_pool[i].start_time == start_time && comet_pool[i].origin_x == ox && comet_pool[i].origin_y == oy) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        comet_pool_add_at(led, start_time, false); // real key
    }
}

// (Sixth pass: the Fairy Orb's motion code that used to live here was rewritten from scratch and moved to
// features/orb_motion.c -- it has no input hooks and nothing in this file depends on it any more.)
