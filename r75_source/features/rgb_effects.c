#include "rgb_effects.h"
#include "custom_rgb.h"
#include "orb_motion.h" // Fairy Orb (sixth pass): self-contained motion model
#include "rgb_control.h" // for rgbctl_get_effect_color() -- each bespoke effect below (Enter/
                          // Backspace/Delete/Space/Encoder) now renders in its own independently
                          // configurable color (continuation-pass spec sec 17) instead of a
                          // hardcoded constant or the shared base profile hue.
#include <lib/lib8tion/lib8tion.h> // qadd8/scale8 -- not pulled in transitively by quantum.h/
                       // rgb_matrix.h; confirmed by this file's first build attempt (implicit-
                       // declaration error), not assumed. Same include form quantum/rgb_matrix/
                       // rgb_matrix.c itself uses. LIB8TION_ENABLE is already forced on by
                       // RGB_MATRIX_ENABLE (builddefs/common_features.mk), so lib8tion.c is
                       // already being linked in -- only the header was missing.

// --- Speed scaling, per target (fourth pass; fifth pass: now per-target, not just per-mode) ----
// Row Wave (already existing), and the fourth pass's Ripple Pool/Comet Trail/Fairy Orb's own
// arrival ripple, all previously used a fixed total-duration constant with no reference to the
// configured animation speed at all -- reported (about the newest of these) as "the speed of the
// new wave mode doesn't change", and confirmed true of Row Wave too on inspection, not just the
// new modes. reactive_energy's own glow_event_contribution() above (the *only* place speed was
// already being read in this whole file) uses a much wider, division-based range tuned for its own
// falloff curve specifically (65,535/(speed+1) ms) -- reusing that exact formula here would swing
// these wave/comet-style effects from a fraction of a second up to *over a minute* at the lowest
// speed setting, wildly outside the range they were actually designed and tuned around. This is a
// separate, gentler, linear scale purpose-built for a fixed "total_ms" duration: 1.6x the tuned
// baseline at the slowest speed setting, 0.5x at the fastest, ~1.0x at the mid setting these
// constants were originally tuned against -- a clearly visible difference at either extreme
// without the wave's whole character changing beyond recognition the way the wide range above
// would. Only the *timing* scales; how far a wave travels (its own MAX_RADIUS/SPAN/etc constant)
// is left alone, so "speed" reads as exactly that -- faster or slower -- not also bigger/smaller.
//
// Fifth pass: takes a target now (was always the one shared global) -- per the person's own
// explicit request ("individual animation speed... using the number keys"), every one of Custom
// Mode's own special-key effects (Enter/Backspace/Delete/Space/Escape/the encoder) reads its own
// independently-stored speed through this same function now too, alongside Row Wave/Ripple Pool/
// Comet Trail, each through their own already-established RGBCTL_TARGET_*.
static uint16_t target_scaled_ms(uint16_t base_ms, rgbctl_color_target_t target) {
    uint8_t  speed = rgbctl_get_effect_speed(target);
    uint16_t pct    = (uint16_t)(160 - ((uint32_t)speed * 110) / 255); // 160..50
    return (uint16_t)(((uint32_t)base_ms * pct) / 100);
}

// The same speed mapping applied to *elapsed time* instead of to a duration: returns how far into
// its own (unscaled, as-tuned) timeline an animation is, given real elapsed ms. Needed by animations
// built from several fixed internal timings (Escape's per-key cascade: step / pause / per-key fade)
// where scaling only the total would cut the cascade short at high speed -- running the animation's
// *clock* faster or slower keeps every internal phase in proportion automatically.
static uint16_t target_scaled_elapsed(uint16_t real_elapsed, rgbctl_color_target_t target) {
    uint8_t  speed = rgbctl_get_effect_speed(target);
    uint16_t pct    = (uint16_t)(160 - ((uint32_t)speed * 110) / 255); // 160..50
    return (uint16_t)(((uint32_t)real_elapsed * 100) / pct);
}

// Continuation-pass rewrite; R75 spec round (2026-09) redesigned several pieces further -- see
// each section's own comment for what changed and why. The reactive-glow event pool
// (custom_rgb.h) still renders here -- standard/modifier/Tab glow, live-widened radius for
// GLOW_KIND_NORMAL while Shift is held or Caps Lock is on (sec 9). Enter/Backspace/Delete/Space
// are each their own small pool of independent concurrent instances instead of a single-slot
// preempting state machine, and every LED in the protected Esc-F1-F12-Delete strip is
// unconditionally excluded from all of it -- normal glow, all five bespoke effects, and the
// encoder wave alike. F-key groups and the Escape sequence are the only things that still touch
// that strip. Row Wave/Darkening/Fairy Orb (sec 23/24/25), at the bottom of this file, are new,
// standalone modes rather than overlays on this render path -- see their own section comments.
//
// Every bespoke effect below (Enter/Backspace/Delete/Space/Encoder/Row Wave) now scales its
// brightness from its own target's *stored* value (rgbctl_get_effect_color()'s .v, spec sec 26)
// instead of a hardcoded 255 -- in most cases that just means no longer overwriting hsv.v after
// reading it, since the color returned already carries the right ceiling; where a 0-255 falloff
// value needs to be combined with that ceiling, the pattern used throughout is
// `hsv.v = scale8(falloff, hsv.v)` with hsv initialized from rgbctl_get_effect_color() first.

// Falloff radii, x/y units (16 = one column, ~13 = one row on this board -- confirmed from
// keyboard.json's rgb_matrix.layout). R75 spec round (2026-09) sec 3/9: reduced further from the
// previous pass's 24/34 -- at 18, a same-row/same-column neighbor (16 or ~13 units away) still
// picks up a dim touch, but a diagonal neighbor (~20.6 units away) no longer reaches inside the
// radius at all, which reads as a visibly tighter reaction than before while staying clearly
// visible rather than shrinking to invisibility. GLOW_RADIUS_NORMAL_ENLARGED (Shift held/Caps
// Lock on) kept at roughly the same ratio to the new base as the previous pass used. Modifier
// stays deliberately smaller than either; Tab ~1.5x the base normal radius, unchanged.
#define GLOW_RADIUS_NORMAL_BASE 18
#define GLOW_RADIUS_NORMAL_ENLARGED 26
#define GLOW_RADIUS_MODIFIER 20
#define GLOW_RADIUS_TAB 51 // ~1.5 * the previous pass's normal-base radius, kept as-is (sec 8:
                             // "keep the existing visual character" for Tab specifically)

// ~60% peak for modifiers, applied as a plain scale8() fraction.
#define GLOW_PEAK_MODIFIER_SCALE8 153 // 153/255 ~= 60%
// Tab's ~120% peak can't be expressed as a scale8() fraction (>100%), so it's applied as a
// saturating boost instead.
#define GLOW_TAB_BOOST_DIVISOR 5 // brightness += brightness/5 (~+20%), qadd8-clamped at 255

// Brightness (0-255) that glow_pool slot `ev` currently contributes at LED `led`: spatial falloff
// (squared-distance, no sqrt) times temporal decay.
static uint8_t glow_event_contribution(const glow_event_t *ev, uint8_t led) {
    uint16_t radius;
    switch ((glow_kind_t)ev->kind) {
        case GLOW_KIND_MODIFIER:
            radius = GLOW_RADIUS_MODIFIER;
            break;
        case GLOW_KIND_TAB:
            radius = GLOW_RADIUS_TAB;
            break;
        case GLOW_KIND_NORMAL_WIDE:
            // Sec 9: fixed at whichever this event was created with -- see custom_rgb.h's
            // comment on GLOW_KIND_NORMAL_WIDE. No live glow_wide_radius_active() check here.
            radius = GLOW_RADIUS_NORMAL_ENLARGED;
            break;
        default: // GLOW_KIND_NORMAL
            radius = GLOW_RADIUS_NORMAL_BASE;
            break;
    }

    int16_t  dx        = (int16_t)g_led_config.point[ev->led_index].x - (int16_t)g_led_config.point[led].x;
    int16_t  dy        = (int16_t)g_led_config.point[ev->led_index].y - (int16_t)g_led_config.point[led].y;
    uint32_t dist_sq    = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);
    uint32_t radius_sq  = (uint32_t)radius * radius;
    if (dist_sq > radius_sq) {
        return 0;
    }
    uint8_t spatial = (uint8_t)(255 - (dist_sq * 255) / radius_sq);

    uint8_t  speed_factor = qadd8(rgb_matrix_config.speed, 1); // +1 avoids divide-by-zero
    uint16_t max_tick      = 65535 / speed_factor;
    uint16_t tick           = (uint16_t)(g_rgb_timer - ev->start_time);
    if (tick >= max_tick) {
        return 0;
    }
    uint8_t temporal = (uint8_t)(255 - ((uint32_t)tick * 255) / max_tick);

    uint8_t brightness = scale8(spatial, temporal);
    switch ((glow_kind_t)ev->kind) {
        case GLOW_KIND_MODIFIER:
            brightness = scale8(brightness, GLOW_PEAK_MODIFIER_SCALE8);
            break;
        case GLOW_KIND_TAB:
            brightness = qadd8(brightness, brightness / GLOW_TAB_BOOST_DIVISOR);
            break;
        default:
            break;
    }
    return brightness;
}

// --- Enter explosion / Backspace eraser / Delete vacuum --------------------------------------
//
// All three are overlaid on top of the pool render below, every frame, for every currently-
// active instance in their respective pool (custom_rgb.h): normal glow keeps rendering from
// whatever the pool still holds, each active instance additionally (a) paints its own pixels for
// LEDs within its current "active region" this frame, and (b) progressively clears pool events
// it has passed over. None of the three ever touches a protected-strip LED (sec 19/21/3/4), and
// none leaves a persistent plain glow on its own origin key (custom_rgb.c's glow_pool_sync()
// already excludes Enter/Backspace/Space's LEDs from the plain pool entirely; Delete's origin is
// in the protected strip so it was already excluded).

#define ENTER_LED 62            // verified LED index, see custom_rgb.c's classify table
#define ENTER_ORIGIN_X 208
#define ENTER_ORIGIN_Y 38
#define ENTER_FLASH_MS 40       // phase 1 end
#define ENTER_RING_START_MS 70  // phase 2 end / phase 3 start
// Fifth pass: 280 -> 720 (70ms of flash + a 650ms ring, matching Ripple Pool's own RIPPLE_TOTAL_MS)
// -- per the person's request to give Enter "the variant from the second wave mode" (Ripple Pool,
// the drop-like board-wide ring; Enter's own ring was a ~3x faster, constant-brightness sweep that
// read as a fast moving line rather than a ripple). See overlay_enter_instance() phase 3 below.
#define ENTER_TOTAL_MS 720      // phase 3 end
// Fifth pass: ENTER_TOTAL_MS is now this target's own *base* duration (at the mid speed setting it was tuned against) -- enter_total_ms() below scales it live by RGBCTL_TARGET_ENTER's own independently-stored speed (rgb_control.h's rgbctl_get_effect_speed()), same as Row Wave/Ripple Pool/Comet Trail already do, so this effect's speed is now also individually adjustable via the number keys (person's own request) rather than reading a value that never varied.
static uint16_t enter_total_ms(void) {
    return target_scaled_ms(ENTER_TOTAL_MS, RGBCTL_TARGET_ENTER);
}
#define ENTER_RING_THICKNESS 30 // ~2 LED-widths (sec 10: "approximately 2-3 LEDs thick"); was 28,
                                  // then 32 -- 30 now, matching RIPPLE_THICKNESS exactly (fifth
                                  // pass: same ring, same look as Ripple Pool's drop)
#define ENTER_RING_MAX_RADIUS 233 // board-crossing distance from Enter's origin

#define BACKSPACE_START_X 224    // board's max x -- sweep starts from the right edge so it
                                  // covers the entire board
// Fourth pass (2026-09): reported as "clunky" alongside Enter/Delete's own wave-clear -- Backspace
// was noticeably faster than either sibling (200ms vs. Enter's 280/Delete's 260), which reads as
// abrupt next to them even though its own sweep math was already smooth. Harmonized to Delete's
// pacing (also a straight-line sweep, the closer match of the two) rather than inventing a fourth
// number -- BACKSPACE_START_X/BACKSPACE_COLUMN_WIDTH below don't need to change: the sweep still
// covers the same distance, just over more frames, so it simply reads as a slower, steadier wipe.
#define BACKSPACE_TOTAL_MS 260
// Fifth pass: BACKSPACE_TOTAL_MS is now this target's own *base* duration (at the mid speed setting it was tuned against) -- backspace_total_ms() below scales it live by RGBCTL_TARGET_BACKSPACE's own independently-stored speed (rgb_control.h's rgbctl_get_effect_speed()), same as Row Wave/Ripple Pool/Comet Trail already do, so this effect's speed is now also individually adjustable via the number keys (person's own request) rather than reading a value that never varied.
static uint16_t backspace_total_ms(void) {
    return target_scaled_ms(BACKSPACE_TOTAL_MS, RGBCTL_TARGET_BACKSPACE);
}
#define BACKSPACE_COLUMN_WIDTH 16 // matches the board's real column spacing
// Trailing "keep" fractions (of 255) applied to whatever's already rendered there, indexed by
// how many columns behind the erase edge a LED is: 0 = the bright leading glint itself (handled
// separately, not via this table), 1-3 = progressively less suppressed, restoring normal glow
// over a few columns' width rather than a single hard cut. Sized/shaped so the eraser reads as a
// moving dark wake with a visible bright tip, not a repaint (sec 2).
static const uint8_t backspace_keep_table[4] = {0, 40, 120, 200};

#define DELETE_LED 8              // verified LED index -- protected strip, never itself painted
#define DELETE_ORIGIN_X 208
#define DELETE_ORIGIN_Y 0
#define DELETE_TOTAL_MS 260
// Fifth pass: DELETE_TOTAL_MS is now this target's own *base* duration (at the mid speed setting it was tuned against) -- delete_total_ms() below scales it live by RGBCTL_TARGET_DELETE's own independently-stored speed (rgb_control.h's rgbctl_get_effect_speed()), same as Row Wave/Ripple Pool/Comet Trail already do, so this effect's speed is now also individually adjustable via the number keys (person's own request) rather than reading a value that never varied.
// Sixth pass: Delete's own speed curve. The shared curve (target_scaled_ms()) bottoms out at 160% of the
// base duration, i.e. 260 ms * 1.6 = 416 ms for the whole sweep at the slowest speed -- still fast
// enough that the vacuum reads as a flicker rather than a motion. Delete now has its own curve:
//   speed >= mid (127): identical to the shared curve, so the look at the default/mid setting and
//                        everything faster is unchanged;
//   speed <  mid:        keeps stretching linearly, reaching DELETE_SLOW_PCT_AT_MIN percent at speed 0,
//                        i.e. 260 ms * 3.85 ~= 1 s for the full sweep at the minimum setting.
// The two halves meet at speed 127 with the same value, so there is no jump anywhere along the dial.
#define DELETE_SLOW_PCT_AT_MIN 385
static uint16_t delete_speed_pct(uint8_t speed) {
    const uint16_t shared_pct_at_mid = (uint16_t)(160 - ((uint32_t)127 * 110) / 255); // same expression as target_scaled_ms()
    if (speed >= 127) {
        return (uint16_t)(160 - ((uint32_t)speed * 110) / 255);
    }
    return (uint16_t)(DELETE_SLOW_PCT_AT_MIN - ((uint32_t)speed * (DELETE_SLOW_PCT_AT_MIN - shared_pct_at_mid)) / 127);
}
static uint16_t delete_total_ms(void) {
    return (uint16_t)(((uint32_t)DELETE_TOTAL_MS * delete_speed_pct(rgbctl_get_effect_speed(RGBCTL_TARGET_DELETE))) / 100);
}
#define DELETE_RING_THICKNESS 32 // ~2 LED-widths, matches ENTER_RING_THICKNESS's reasoning (sec
                                   // 11 gives Delete the same "opposite of Enter" ring character);
                                   // was 28
#define DELETE_MAX_RADIUS 220     // board-crossing distance from Delete's origin
// Same "keep" concept as Backspace, indexed by how many ring-thicknesses inside the current
// (growing) consumption radius a LED sits: 0 = just consumed (bright glint), further in =
// progressively more suppressed, fully black several steps behind the front.
static const uint8_t delete_keep_table[4] = {0, 40, 120, 200};

#define SPACE_ORIGIN_X 80
#define SPACE_ORIGIN_Y 64
#define SPACE_WAVE_MAX_RADIUS 100 // reaches both Alts, both Ctrl/Win, and both Shifts
#define SPACE_WAVE_TOTAL_MS 220
// Fifth pass: SPACE_WAVE_TOTAL_MS is now this target's own *base* duration (at the mid speed setting it was tuned against) -- space_wave_total_ms() below scales it live by RGBCTL_TARGET_SPACE's own independently-stored speed (rgb_control.h's rgbctl_get_effect_speed()), same as Row Wave/Ripple Pool/Comet Trail already do, so this effect's speed is now also individually adjustable via the number keys (person's own request) rather than reading a value that never varied.
static uint16_t space_wave_total_ms(void) {
    return target_scaled_ms(SPACE_WAVE_TOTAL_MS, RGBCTL_TARGET_SPACE);
}

// Current outer radius of Enter's expanding ring: 0 before the ring starts, linear from 0 to
// ENTER_RING_MAX_RADIUS across [ENTER_RING_START_MS, enter_total_ms()].
static uint16_t enter_ring_radius(uint16_t elapsed) {
    if (elapsed <= ENTER_RING_START_MS) {
        return 0;
    }
    uint16_t t    = elapsed - ENTER_RING_START_MS;
    uint16_t span = enter_total_ms() - ENTER_RING_START_MS;
    return (uint16_t)(((uint32_t)t * ENTER_RING_MAX_RADIUS) / span);
}

// Paints LED `led`'s overlay pixel for one Enter explosion instance, if any, into *rgb.
static void overlay_enter_instance(uint8_t led, uint16_t elapsed, RGB *rgb) {
    HSV base = rgbctl_get_effect_color(RGBCTL_TARGET_ENTER);
    if (elapsed < ENTER_FLASH_MS) {
        // Phase 1: Enter's own LED only, at that target's own configured brightness.
        if (led == ENTER_LED) {
            *rgb = hsv_to_rgb(base);
        }
        return;
    }
    if (elapsed < ENTER_RING_START_MS) {
        // Phase 2: Enter's own LED, linear fade 100% -> 70% of its configured brightness.
        if (led == ENTER_LED) {
            uint16_t t    = elapsed - ENTER_FLASH_MS;
            uint16_t span = ENTER_RING_START_MS - ENTER_FLASH_MS;
            uint8_t  pct  = 100 - (uint8_t)(((uint32_t)t * 30) / span);
            HSV      hsv  = base;
            hsv.v         = (uint8_t)(((uint16_t)base.v * pct) / 100);
            *rgb          = hsv_to_rgb(hsv);
        }
        return;
    }
    if (elapsed < enter_total_ms()) {
        // Phase 3: expanding ring band at that target's own configured color/brightness. Fifth
        // pass: now the same "drop" character as Ripple Pool (ring thickness, pacing, and the
        // ring fading out as it spreads, 255 -> 0 across the ring's own lifetime, exactly as
        // Ripple Pool's ripple_contribution() does) instead of a constant-brightness fast sweep.
        uint16_t radius = enter_ring_radius(elapsed);
        uint16_t inner  = (radius > ENTER_RING_THICKNESS) ? (radius - ENTER_RING_THICKNESS) : 0;

        int16_t  dx      = (int16_t)g_led_config.point[led].x - (int16_t)ENTER_ORIGIN_X;
        int16_t  dy      = (int16_t)g_led_config.point[led].y - (int16_t)ENTER_ORIGIN_Y;
        uint32_t dist_sq = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);

        if (dist_sq <= (uint32_t)radius * radius && dist_sq >= (uint32_t)inner * inner) {
            // Fourth pass: this used to force hsv.s = 255 here ("so the shockwave reads clearly
            // regardless of the configured color/profile") -- reported as the actual bug: with
            // saturation forced, only Enter's own LED (phases 1/2 above, which already correctly
            // use base.s unmodified) responded to the saturation control at all; the ring itself
            // never did. base already carries the target's real saturation -- just use it, the
            // same way every other phase/effect in this file already does.
            uint16_t ring_t    = elapsed - ENTER_RING_START_MS;
            uint16_t ring_span = enter_total_ms() - ENTER_RING_START_MS;
            uint8_t  fade      = (uint8_t)(255 - ((uint32_t)ring_t * 255) / ring_span);
            HSV      ring_hsv  = base;
            ring_hsv.v         = scale8(fade, base.v);
            *rgb               = hsv_to_rgb(ring_hsv);
        }
    }
}

static void overlay_enter_all(uint8_t led, RGB *rgb) {
    if (is_protected_led(led)) {
        return;
    }
    // Fifth pass: composite overlapping instances by brightness instead of letting whichever pool
    // slot comes last simply overwrite the rest. That was harmless while every ring was a constant-
    // brightness band, but Enter's ring now fades as it spreads (see overlay_enter_instance()), so
    // with several alive at once (holding Enter for key-repeat) a dim, nearly-finished ring in a
    // later slot would otherwise paint over a brighter, newer one at the same LED.
    RGB  best     = {0, 0, 0};
    bool any_hit  = false;
    for (uint8_t i = 0; i < EFFECT_POOL_SIZE; i++) {
        if (!enter_pool[i].active) {
            continue;
        }
        uint16_t elapsed = (uint16_t)(g_rgb_timer - enter_pool[i].start_time);
        if (elapsed >= enter_total_ms()) {
            continue; // tick_special_effects() below will retire it this same frame
        }
        RGB layer = {0, 0, 0};
        overlay_enter_instance(led, elapsed, &layer);
        uint8_t layer_max = (layer.r > layer.g) ? ((layer.r > layer.b) ? layer.r : layer.b) : ((layer.g > layer.b) ? layer.g : layer.b);
        uint8_t best_max  = (best.r > best.g) ? ((best.r > best.b) ? best.r : best.b) : ((best.g > best.b) ? best.g : best.b);
        if (layer_max > 0 && (!any_hit || layer_max > best_max)) {
            best    = layer;
            any_hit = true;
        }
    }
    if (any_hit) {
        *rgb = best;
    }
}

// Current x-position of one Backspace instance's leading edge: linear from BACKSPACE_START_X
// (right edge) at elapsed=0 to 0 at elapsed=backspace_total_ms().
static uint8_t backspace_edge_x(uint16_t elapsed) {
    if (elapsed >= backspace_total_ms()) {
        return 0;
    }
    uint32_t moved = ((uint32_t)elapsed * BACKSPACE_START_X) / backspace_total_ms();
    return (uint8_t)(BACKSPACE_START_X - moved);
}

// Erases (suppresses toward black) whatever's already in *rgb from a normal render, with a
// bright leading "tip" in Backspace's configured color right at the edge -- an eraser moving
// through the existing field, not a repaint (sec 2). Leaves *rgb untouched ahead of the edge.
static void overlay_backspace_instance(uint8_t led, uint16_t elapsed, RGB *rgb) {
    if (elapsed >= backspace_total_ms()) {
        return;
    }
    uint8_t edge_x = backspace_edge_x(elapsed);
    uint8_t led_x  = g_led_config.point[led].x;
    if (led_x < edge_x) {
        return; // ahead of the edge -- not reached yet, normal glow shows through
    }
    uint8_t columns_behind = (led_x - edge_x) / BACKSPACE_COLUMN_WIDTH;
    if (columns_behind == 0) {
        *rgb = hsv_to_rgb(rgbctl_get_effect_color(RGBCTL_TARGET_BACKSPACE)); // that target's own
                                                                                // configured
                                                                                // brightness (sec 26)
        return;
    }
    if (columns_behind >= 4) {
        return; // fully restored -- already cleared from the pool a moment ago, so whatever
                 // normal render remains here (typically nothing) shows through untouched
    }
    uint8_t keep = backspace_keep_table[columns_behind];
    rgb->r        = scale8(rgb->r, keep);
    rgb->g        = scale8(rgb->g, keep);
    rgb->b        = scale8(rgb->b, keep);
}

static void overlay_backspace_all(uint8_t led, RGB *rgb) {
    if (is_protected_led(led)) {
        return;
    }
    for (uint8_t i = 0; i < EFFECT_POOL_SIZE; i++) {
        if (!backspace_pool[i].active) {
            continue;
        }
        uint16_t elapsed = (uint16_t)(g_rgb_timer - backspace_pool[i].start_time);
        if (elapsed >= backspace_total_ms()) {
            continue;
        }
        overlay_backspace_instance(led, elapsed, rgb);
    }
}

// Delete vacuum: the conceptual opposite of Enter -- a boundary sweeps INWARD from the board's
// far edge toward Delete (radius shrinks from DELETE_MAX_RADIUS to 0 over the animation's life),
// swallowing/erasing whatever it's already swept past and leaving everything still inside the
// shrinking boundary untouched until its turn comes, with a bright "being pulled in" glint
// (Delete's configured color) right at the boundary as it passes through.
//
// Spec update (2026-09) sec 1.1: previously this radius *grew* from Delete outward, the same
// direction Enter's ring moves -- meaning the bright leading edge visibly travelled away from
// Delete, indistinguishable in direction from an explosion. Fixed by inverting the radius's
// relationship to elapsed time so the boundary genuinely travels toward/into Delete instead.
static void overlay_delete_instance(uint8_t led, uint16_t elapsed, RGB *rgb) {
    if (elapsed >= delete_total_ms()) {
        return;
    }
    uint16_t radius = DELETE_MAX_RADIUS - (uint16_t)(((uint32_t)elapsed * DELETE_MAX_RADIUS) / delete_total_ms());

    int16_t  dx      = (int16_t)g_led_config.point[led].x - (int16_t)DELETE_ORIGIN_X;
    int16_t  dy      = (int16_t)g_led_config.point[led].y - (int16_t)DELETE_ORIGIN_Y;
    uint32_t dist_sq = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);

    if (dist_sq <= (uint32_t)radius * radius) {
        return; // still inside the shrinking boundary -- not reached (consumed) yet
    }
    // Outside the boundary: already swept over. Same 4-band fade shape as before, now measured
    // *outward* from the shrinking radius (band 0, right at the boundary, is the bright glint;
    // bands 1-3 trail behind it, farther from Delete = swept over longer ago).
    for (uint8_t step = 0; step < 4; step++) {
        uint16_t inner_r = radius + (uint16_t)step * DELETE_RING_THICKNESS;
        uint16_t outer_r = radius + (uint16_t)(step + 1) * DELETE_RING_THICKNESS;
        if (dist_sq > (uint32_t)inner_r * inner_r && dist_sq <= (uint32_t)outer_r * outer_r) {
            if (step == 0) {
                *rgb = hsv_to_rgb(rgbctl_get_effect_color(RGBCTL_TARGET_DELETE)); // that target's
                                                                                    // own
                                                                                    // configured
                                                                                    // brightness
                                                                                    // (sec 26)
            } else {
                uint8_t keep = delete_keep_table[step];
                rgb->r        = scale8(rgb->r, keep);
                rgb->g        = scale8(rgb->g, keep);
                rgb->b        = scale8(rgb->b, keep);
            }
            return;
        }
    }
    // Beyond all 4 bands -- swept over long ago, fully consumed.
    rgb->r = 0;
    rgb->g = 0;
    rgb->b = 0;
}

static void overlay_delete_all(uint8_t led, RGB *rgb) {
    if (is_protected_led(led)) {
        return; // includes Delete's own LED -- origin is math only, per sec 4/21
    }
    for (uint8_t i = 0; i < EFFECT_POOL_SIZE; i++) {
        if (!delete_pool[i].active) {
            continue;
        }
        uint16_t elapsed = (uint16_t)(g_rgb_timer - delete_pool[i].start_time);
        if (elapsed >= delete_total_ms()) {
            continue;
        }
        overlay_delete_instance(led, elapsed, rgb);
    }
}

// Space wave: a soft, additive (not erasing/overlaying) pulse expanding from Space, the same
// general falloff shape as a normal key's glow, just from its own pool/origin and in its own
// configurable color rather than piggy-backing on the plain pool (sec 7).
static uint8_t space_wave_brightness(const effect_wave_t *ev, uint8_t led) {
    if (!ev->active) {
        return 0;
    }
    uint16_t elapsed = (uint16_t)(g_rgb_timer - ev->start_time);
    if (elapsed >= space_wave_total_ms()) {
        return 0;
    }
    uint16_t radius = (uint16_t)(((uint32_t)elapsed * SPACE_WAVE_MAX_RADIUS) / space_wave_total_ms());
    int16_t  dx      = (int16_t)g_led_config.point[led].x - (int16_t)SPACE_ORIGIN_X;
    int16_t  dy      = (int16_t)g_led_config.point[led].y - (int16_t)SPACE_ORIGIN_Y;
    uint32_t dist_sq = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);
    if (dist_sq > (uint32_t)radius * radius) {
        return 0; // not reached yet
    }
    // Fades the whole pulse out over its own lifetime so it doesn't end abruptly once it has
    // grown past every key it's going to reach.
    return (uint8_t)(255 - ((uint32_t)elapsed * 255) / space_wave_total_ms());
}

static void overlay_space_all(uint8_t led, RGB *rgb) {
    if (is_protected_led(led)) {
        return;
    }
    uint8_t v = 0;
    for (uint8_t i = 0; i < EFFECT_POOL_SIZE; i++) {
        v = qadd8(v, space_wave_brightness(&space_pool[i], led));
    }
    if (v == 0) {
        return;
    }
    HSV hsv = rgbctl_get_effect_color(RGBCTL_TARGET_SPACE);
    hsv.v   = scale8(v, hsv.v); // v: 0-255 falloff; hsv.v starts as that target's own configured
                                  // brightness ceiling (sec 26) -- scale8 combines the two
    RGB add = hsv_to_rgb(hsv);
    rgb->r  = qadd8(rgb->r, add.r);
    rgb->g  = qadd8(rgb->g, add.g);
    rgb->b  = qadd8(rgb->b, add.b);
}

// --- F-key groups / Escape sequence -----------------------------------------------------------
//
// F-key LEDs are exactly 9-20, and -- unlike every other LED group on this board -- consecutive
// and in reverse order (F1=20 down to F12=9; verified programmatically against keyboard.json/
// keymap.c). That makes "F-key index 0-11" <-> "LED" a plain subtraction, not a table.
#define FKEY_LED_F1 20
#define FKEY_GROUP_SIZE 4
#define FGROUP_STEP_MS 20  // ~20ms/step distance delay
#define FGROUP_FADE_MS 280 // ~250-300ms fade, midpoint

#define ESCSEQ_STEP_MS 90        // between keys within a group
#define ESCSEQ_PAUSE_MS 220      // pause after each group of four
#define ESCSEQ_KEY_FADE_MS 280   // each key's own fade once lit

static bool is_fkey_led(uint8_t led, uint8_t *fkey_index_out) {
    if (led < (FKEY_LED_F1 - 11) || led > FKEY_LED_F1) {
        return false;
    }
    *fkey_index_out = FKEY_LED_F1 - led; // 0=F1 .. 11=F12
    return true;
}

// F-key groups. Only meaningful while no escape sequence runs -- enforced once, in
// glow_trigger_fkey_group() (custom_rgb.c), not repeated here.
static void overlay_fkey_group(uint8_t led, RGB *rgb) {
    uint16_t elapsed;
    uint8_t  pressed_led = glow_fkey_group_pressed_led(&elapsed);
    if (pressed_led == GLOW_LED_NONE) {
        return;
    }
    uint8_t fi, pressed_fi;
    if (!is_fkey_led(led, &fi) || !is_fkey_led(pressed_led, &pressed_fi)) {
        return;
    }
    if (fi / FKEY_GROUP_SIZE != pressed_fi / FKEY_GROUP_SIZE) {
        return; // only the pressed key's own group of 4 lights
    }
    uint8_t  distance = (fi > pressed_fi) ? (fi - pressed_fi) : (pressed_fi - fi);
    uint16_t delay     = (uint16_t)distance * FGROUP_STEP_MS;
    if (elapsed < delay) {
        return; // this LED's turn in the cascade hasn't arrived yet
    }
    uint16_t fade_elapsed = elapsed - delay;
    if (fade_elapsed >= FGROUP_FADE_MS) {
        return;
    }
    uint8_t pct = 255 - (uint8_t)(((uint32_t)fade_elapsed * 255) / FGROUP_FADE_MS);
    HSV     hsv = rgb_matrix_config.hsv;
    hsv.v       = pct;
    *rgb        = hsv_to_rgb(hsv);
}

// ms-offset (from the sequence's start) at which F-key index `fi` (0-11) lights up.
static uint16_t escseq_key_onset(uint8_t fi) {
    uint16_t group_span = (FKEY_GROUP_SIZE - 1) * ESCSEQ_STEP_MS + ESCSEQ_PAUSE_MS;
    return (uint16_t)(fi / FKEY_GROUP_SIZE) * group_span + (uint16_t)(fi % FKEY_GROUP_SIZE) * ESCSEQ_STEP_MS;
}

#define ESCSEQ_GROUP_COUNT 3
#define ESCSEQ_TOTAL_MS (((FKEY_GROUP_SIZE - 1) * ESCSEQ_STEP_MS + ESCSEQ_PAUSE_MS) * (ESCSEQ_GROUP_COUNT - 1) + (FKEY_GROUP_SIZE - 1) * ESCSEQ_STEP_MS + ESCSEQ_KEY_FADE_MS)
// Fifth pass: ESCSEQ_TOTAL_MS is now this target's own *base* duration (at the mid speed setting it was tuned against) -- escseq_total_ms() below scales it live by RGBCTL_TARGET_ESC's own independently-stored speed (rgb_control.h's rgbctl_get_effect_speed()), same as Row Wave/Ripple Pool/Comet Trail already do, so this effect's speed is now also individually adjustable via the number keys (person's own request) rather than reading a value that never varied.
static uint16_t escseq_total_ms(void) {
    return target_scaled_ms(ESCSEQ_TOTAL_MS, RGBCTL_TARGET_ESC);
}

// R75 spec round (2026-09) sec 4: Escape must behave like Enter/Backspace's independent-wave
// pattern -- multiple presses must produce multiple *coexisting* waves, none of them restarting
// or preempting another. Iterates escseq_pool (custom_rgb.h) directly rather than a single
// special_effect_t, and -- since every active instance shares the exact same cascade shape and
// color, just started at different times -- takes whichever instance is currently *brightest* at
// each LED rather than summing them, so two overlapping waves read as "the newer one is winning
// right now" instead of blowing out toward white.
static void overlay_escape_sequence_all(uint8_t led, RGB *rgb) {
    uint8_t fi;
    if (!is_fkey_led(led, &fi)) {
        return;
    }
    uint16_t onset    = escseq_key_onset(fi);
    uint8_t  best_pct = 0;
    for (uint8_t i = 0; i < ESCSEQ_POOL_SIZE; i++) {
        if (!escseq_pool[i].active) {
            continue;
        }
        // Fifth pass: scaled *time* against the as-tuned base timeline (see target_scaled_elapsed()),
        // not a scaled total against fixed step/pause/fade constants -- the latter would truncate
        // the last keys of the cascade whenever speed is above the mid setting.
        uint16_t elapsed = target_scaled_elapsed((uint16_t)(g_rgb_timer - escseq_pool[i].start_time), RGBCTL_TARGET_ESC);
        if (elapsed >= ESCSEQ_TOTAL_MS || elapsed < onset) {
            continue;
        }
        uint16_t fade_elapsed = elapsed - onset;
        if (fade_elapsed >= ESCSEQ_KEY_FADE_MS) {
            continue;
        }
        uint8_t pct = 255 - (uint8_t)(((uint32_t)fade_elapsed * 255) / ESCSEQ_KEY_FADE_MS);
        if (pct > best_pct) {
            best_pct = pct;
        }
    }
    if (best_pct == 0) {
        return;
    }
    // Fourth pass (2026-09): previously always read the live board base color directly here
    // (rgb_matrix_config.hsv) -- unlike every one of its sibling special keys (Enter/Backspace/
    // Delete/Space/Encoder all already had their own configurable target), Escape had no color
    // slot of its own at all. RGBCTL_TARGET_ESC (rgb_control.h) gives it one, following the exact
    // same "stored hue/sat, own .v ceiling scaled by this frame's fade pct" combining pattern the
    // encoder wave above already uses.
    HSV hsv = rgbctl_get_effect_color(RGBCTL_TARGET_ESC);
    hsv.v   = scale8(best_pct, hsv.v);
    *rgb    = hsv_to_rgb(hsv);
}

// Retires expired instances and clears pool events each active instance has passed over, so the
// plain reactive pool ends up empty behind them without a special case. Called at most once per
// frame (guarded the same way glow_pool_sync() guards itself).
static void tick_effect_pools(void) {
    static uint32_t last_ticked_frame = 0;
    if (g_rgb_timer == last_ticked_frame) {
        return;
    }
    last_ticked_frame = g_rgb_timer;

    for (uint8_t i = 0; i < EFFECT_POOL_SIZE; i++) {
        if (enter_pool[i].active) {
            uint16_t elapsed = (uint16_t)(g_rgb_timer - enter_pool[i].start_time);
            if (elapsed >= ENTER_RING_START_MS) {
                glow_pool_clear_within_radius(ENTER_ORIGIN_X, ENTER_ORIGIN_Y, enter_ring_radius(elapsed), enter_pool[i].start_time);
            }
            if (elapsed >= enter_total_ms()) {
                enter_pool[i].active = false;
            }
        }
        if (backspace_pool[i].active) {
            uint16_t elapsed = (uint16_t)(g_rgb_timer - backspace_pool[i].start_time);
            glow_pool_clear_x_at_or_above(backspace_edge_x(elapsed), backspace_pool[i].start_time);
            if (elapsed >= backspace_total_ms()) {
                backspace_pool[i].active = false;
            }
        }
        if (delete_pool[i].active) {
            uint16_t elapsed = (uint16_t)(g_rgb_timer - delete_pool[i].start_time);
            // Shrinking radius (sec 1.1 fix, see overlay_delete_instance) -- erase whatever the
            // inward-sweeping boundary has already passed, i.e. farther out than it currently is.
            uint16_t radius  = DELETE_MAX_RADIUS - (uint16_t)(((uint32_t)elapsed * DELETE_MAX_RADIUS) / delete_total_ms());
            glow_pool_clear_outside_radius(DELETE_ORIGIN_X, DELETE_ORIGIN_Y, radius, delete_pool[i].start_time);
            if (elapsed >= delete_total_ms()) {
                delete_pool[i].active = false;
            }
        }
        if (space_pool[i].active) {
            uint16_t elapsed = (uint16_t)(g_rgb_timer - space_pool[i].start_time);
            if (elapsed >= space_wave_total_ms()) {
                space_pool[i].active = false;
            }
        }
    }

    // R75 spec round (2026-09) sec 4: escseq_pool is now a ring-buffer of independent instances
    // (custom_rgb.h), same shape as enter_pool/etc above -- retire whichever have finished their
    // full cascade-and-fade.
    for (uint8_t i = 0; i < ESCSEQ_POOL_SIZE; i++) {
        if (escseq_pool[i].active) {
            uint16_t elapsed = (uint16_t)(g_rgb_timer - escseq_pool[i].start_time);
            if (elapsed >= escseq_total_ms()) {
                escseq_pool[i].active = false;
            }
        }
    }
}

// --- Shift held-state overlay (spec update sec 4) -----------------------------------------------
// Shift's glow must stay lit for exactly as long as it's physically held, not fade on the normal
// ~2s timer every other key's glow uses. Rendered as a live query first; glow_pool's own entries
// (inserted on press, and fresh again on release via glow_trigger_shift_release(), keymap.c)
// still provide the brief fade-out tail once Shift is no longer held, unmodified.
#define LED_LSHIFT 75
#define LED_RSHIFT 64

static void overlay_shift_held(uint8_t led, RGB *rgb) {
    if ((led != LED_LSHIFT && led != LED_RSHIFT) || !glow_shift_is_held()) {
        return;
    }
    *rgb = hsv_to_rgb(rgb_matrix_config.hsv); // same base color/full brightness normal modifier
                                                 // glow would show at its peak -- just held-
                                                 // duration-aware instead of timer-based
}

bool reactive_energy_render(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    // params->init is true on this effect's first render after boot and after every switch back
    // onto it (set in rgb_task_render() -- same convention the stock rgb_matrix_none() uses).
    if (params->init) {
        glow_pool_init();
        glow_effect_pools_clear_all();
    }

    glow_pool_sync();
    tick_effect_pools();

    bool escseq_active = glow_escseq_any_active();

    HSV hsv = rgb_matrix_config.hsv;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();

        RGB rgb;
        if (is_protected_led(i)) {
            // Never touched by normal reactive glow or any of the five bespoke effects below
            // (sec 19/21) -- starts black; only the F-key-group / Escape-sequence overlays (both
            // already self-scoped to this exact strip) may light it.
            rgb = (RGB){0, 0, 0};
        } else {
            uint8_t v = 0;
            for (uint8_t s = 0; s < GLOW_POOL_SIZE; s++) {
                const glow_event_t *ev = &glow_pool[s];
                if (ev->led_index == GLOW_LED_NONE) {
                    continue;
                }
                v = qadd8(v, glow_event_contribution(ev, i));
            }
            HSV pixel_hsv = hsv;
            pixel_hsv.v   = scale8(v, hsv.v);
            rgb            = hsv_to_rgb(pixel_hsv);

            overlay_space_all(i, &rgb);
            overlay_shift_held(i, &rgb);
        }

        // Enter/Backspace/Delete are hard takeovers (each already self-excludes protected LEDs),
        // layered after the plain glow + space so they read clearly on top of it.
        overlay_enter_all(i, &rgb);
        overlay_backspace_all(i, &rgb);
        overlay_delete_all(i, &rgb);
        overlay_shift_held(i, &rgb); // must win even if an Enter/Backspace/Delete wave happens to
                                       // sweep through this exact LED -- "remain illuminated for
                                       // as long as held" (spec update sec 4) has no exception for
                                       // that, so re-assert it after those three hard takeovers.

        if (escseq_active) {
            overlay_escape_sequence_all(i, &rgb);
        } else {
            // The standalone F-group effect only applies when no escape sequence is running.
            // glow_trigger_fkey_group() already refuses to *start* one in that case; this
            // additionally covers an escape sequence starting while a group flash from just
            // before it is still fading.
            overlay_fkey_group(i, &rgb);
        }

        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

// --- Row Wave (spec sec 23) --------------------------------------------------------------------
// A new standalone mode (its own rgb_matrix_mode -- see rgb_matrix_user.inc), not an overlay on
// reactive_energy above: pressing any key sends a band rippling outward, in both directions,
// along that key's own physical LED row. "Row" is derived from the real g_led_config y-coordinate
// (every LED sharing the origin's exact y), not a matrix-row assumption -- this board's rows
// don't all have the same LED count or x-span (see keyboard.json's rgb_matrix.layout: 6 distinct
// y values, 9-15 LEDs each), so a matrix-row-index approach would be wrong on the physical
// ANSI-gap/stepped-bottom-row keys specifically, exactly what sec 23/46 warn against.
#define ROWWAVE_TOTAL_MS 420 // fifth pass: was 260 -- reported as wanting a slower, smoother
                               // sweep; matches roughly Ripple Pool's own pacing character now
                               // rather than reading as noticeably quicker/sharper than it.
#define ROWWAVE_THICKNESS 40  // x-units -- wide enough to read as a moving band, not one pixel
#define ROWWAVE_MAX_SPAN_X 224 // board's own max x -- the farthest a band ever needs to reach

static uint8_t rowwave_contribution(const row_wave_t *ev, uint8_t led) {
    if (!ev->active) {
        return 0;
    }
    uint16_t total_ms = target_scaled_ms(ROWWAVE_TOTAL_MS, RGBCTL_TARGET_ROWWAVE); // fourth pass:
                                                                                      // was a fixed
                                                                                      // constant
    uint16_t elapsed   = (uint16_t)(g_rgb_timer - ev->start_time);
    if (elapsed >= total_ms) {
        return 0;
    }
    if (g_led_config.point[led].y != g_led_config.point[ev->origin_led].y) {
        return 0; // different physical row entirely
    }
    int16_t  origin_x = (int16_t)g_led_config.point[ev->origin_led].x;
    int16_t  led_x     = (int16_t)g_led_config.point[led].x;
    uint16_t dist       = (uint16_t)((led_x > origin_x) ? (led_x - origin_x) : (origin_x - led_x));

    // Two-sided expanding band, same shape as this file's other waves: outer edge grows linearly
    // with elapsed time, ROWWAVE_THICKNESS wide, brightness fading out over the instance's own
    // lifetime so it doesn't cut off abruptly once it's swept past the whole row.
    uint16_t outer = (uint16_t)(((uint32_t)elapsed * ROWWAVE_MAX_SPAN_X) / total_ms);
    uint16_t inner = (outer > ROWWAVE_THICKNESS) ? (outer - ROWWAVE_THICKNESS) : 0;
    if (dist > outer || dist < inner) {
        return 0;
    }
    return (uint8_t)(255 - ((uint32_t)elapsed * 255) / total_ms);
}

bool row_wave_render(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (params->init) {
        row_wave_pool_init();
    }
    row_wave_pool_sync();

    HSV base = rgbctl_get_effect_color(RGBCTL_TARGET_ROWWAVE);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint8_t v = 0;
        for (uint8_t w = 0; w < ROWWAVE_POOL_SIZE; w++) {
            v = qadd8(v, rowwave_contribution(&row_wave_pool[w], i));
        }
        HSV hsv = base;
        hsv.v    = scale8(v, base.v); // v: 0-255 falloff; base.v: that target's own configured
                                        // brightness ceiling (sec 26)
        RGB rgb  = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// --- Darkening (spec sec 24) ------------------------------------------------------------------
// The conceptual inverse of the normal reactive glow: the board sits at its configured
// brightness/color at rest, and each keypress carves out a temporary *darker* area around itself
// that heals back to full brightness over the same falloff shape normal glow uses. Reuses
// glow_pool/glow_event_contribution directly (same pool, same spatial/temporal math, same
// GLOW_RADIUS_* constants) rather than a second, parallel implementation -- keeps this reading as
// one coherent system rather than a collection of unrelated effects. Has no color slot of its own
// (rgb_control.c's target_for_slot() maps its one slot to RGBCTL_TARGET_BASE) -- it darkens
// whatever the board's own live color already is, there's nothing else for it to have its own
// color separate from that. One accepted limitation, inherited directly from reusing glow_pool
// unmodified: glow_pool_sync() never inserts events for the protected F-row/Esc/Delete strip or
// for Caps/Backspace/Enter/Space's own LEDs (see custom_rgb.c's is_glow_pool_excluded_led()), so
// those specific keys don't darken when pressed here -- they still show at full resting
// brightness. Rather than build a second, parallel key-hit tracker just for this one mode, that
// trade-off was accepted; see HANDOFF.md.
bool darkening_glow_render(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (params->init) {
        glow_pool_init();
    }
    glow_pool_sync();

    HSV base = rgb_matrix_config.hsv;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint8_t reduction = 0;
        for (uint8_t s = 0; s < GLOW_POOL_SIZE; s++) {
            const glow_event_t *ev = &glow_pool[s];
            if (ev->led_index == GLOW_LED_NONE) {
                continue;
            }
            reduction = qadd8(reduction, glow_event_contribution(ev, i));
        }
        HSV hsv = base;
        hsv.v    = qsub8(base.v, reduction); // saturating -- floors at black under the press,
                                                // never wraps
        RGB rgb  = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// --- Fairy Orb (sixth pass: rewritten from scratch) ----------------------------------------------
// A glowing orb that glides over the keyboard like a fish in a lake, trailing a short fading tail.
//
// All of the "where is it" logic lives in features/orb_motion.{h,c} (smooth heading/curvature steering
// driven by procedural noise -- see that file's header for why it cannot jitter, flip or stop, and why
// speed only changes how fast the same path is flown). This function only (1) advances that motion once
// per frame and (2) paints it.
//
// Fully autonomous: no keypress, layer, encoder or idle-timer input is read anywhere in this effect, so it
// keeps moving whether or not anyone is typing -- it is the ambient mode.
// Color / brightness / speed come from the mode's own settings target (RGBCTL_TARGET_ORB).
#define ORB_RADIUS 34         // units -- head glow radius (about 2 key widths)
#define ORB_TAIL_RADIUS_MIN_PCT 40 // the oldest tail sample's radius, as a percent of ORB_RADIUS
#define ORB_TAIL_PEAK_SCALE8 190   // brightest tail sample vs the head (255 = equal), so the head always leads

// Glow profile: 255 at the center falling smoothly to 0 at `radius` (squared falloff, soft edge).
static uint8_t orb_glow(uint32_t dist_sq, uint32_t radius) {
    uint32_t radius_sq = radius * radius;
    if (dist_sq >= radius_sq) {
        return 0;
    }
    uint8_t t = (uint8_t)(255 - (dist_sq * 255) / radius_sq);
    return scale8(t, t);
}

bool fairy_orb_render(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    // Advance the motion exactly once per frame. The effect is called once per LED chunk (params->iter
    // counts them), but g_rgb_timer is the same for every chunk of a frame, so chunk 0 does the update.
    static uint32_t last_timer = 0;
    if (params->iter == 0) {
        uint32_t now = g_rgb_timer;
        if (params->init) {
            orb_motion_init(timer_read32() ^ ((uint32_t)now << 11) ^ 0x9E3779B9u);
        } else {
            uint32_t dt = now - last_timer; // unsigned: safe across timer wrap
            orb_motion_advance((uint16_t)(dt > 0xFFFF ? 0xFFFF : dt), rgbctl_get_effect_speed(RGBCTL_TARGET_ORB));
        }
        last_timer = now;
    }

    const orb_xy_t head = orb_motion_head();
    const int16_t  hx   = (int16_t)(head.x / ORB_Q);
    const int16_t  hy   = (int16_t)(head.y / ORB_Q);

    // Tail samples for this frame, unpacked once (not once per LED).
    int16_t  tx[ORB_TAIL_MAX_SAMPLES], ty[ORB_TAIL_MAX_SAMPLES];
    uint8_t  tbright[ORB_TAIL_MAX_SAMPLES], trad[ORB_TAIL_MAX_SAMPLES];
    uint8_t  tail_n = 0;
    for (uint8_t i = 0; i < ORB_TAIL_MAX_SAMPLES; i++) {
        orb_xy_t p;
        uint16_t age_q4;
        if (!orb_motion_tail(i, &p, &age_q4)) {
            break;
        }
        // Age as a 0..255 fraction of the tail length: 0 = just dropped, 255 = fully faded.
        uint32_t age_units = age_q4 / ORB_Q;
        uint8_t  age       = (age_units >= ORB_TAIL_LENGTH_UNITS) ? 255 : (uint8_t)((age_units * 255) / ORB_TAIL_LENGTH_UNITS);
        uint8_t  life      = 255 - age;
        tx[tail_n]      = (int16_t)(p.x / ORB_Q);
        ty[tail_n]      = (int16_t)(p.y / ORB_Q);
        tbright[tail_n] = scale8(scale8(life, life), ORB_TAIL_PEAK_SCALE8); // fades quadratically: soft, no hard end
        trad[tail_n]    = (uint8_t)(ORB_TAIL_RADIUS_MIN_PCT + ((uint16_t)(100 - ORB_TAIL_RADIUS_MIN_PCT) * life) / 255);
        tail_n++;
    }

    const HSV base = rgbctl_get_effect_color(RGBCTL_TARGET_ORB);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        const int16_t lx = (int16_t)g_led_config.point[i].x;
        const int16_t ly = (int16_t)g_led_config.point[i].y;

        int32_t  dx = lx - hx;
        int32_t  dy = ly - hy;
        uint8_t  v  = orb_glow((uint32_t)(dx * dx + dy * dy), ORB_RADIUS);

        // The tail is the brightest of its samples at this LED (max, not sum, so the overlapping blobs
        // blend into one smooth streak instead of stacking up bright spots), never brighter than the head.
        for (uint8_t j = 0; j < tail_n; j++) {
            int32_t ddx = lx - tx[j];
            int32_t ddy = ly - ty[j];
            uint32_t d2 = (uint32_t)(ddx * ddx + ddy * ddy);
            if (d2 >= (uint32_t)ORB_RADIUS * ORB_RADIUS) {
                continue; // cheap reject: the widest any sample can be
            }
            uint8_t tv = scale8(orb_glow(d2, ((uint32_t)ORB_RADIUS * trad[j]) / 100), tbright[j]);
            if (tv > v) {
                v = tv;
            }
        }

        HSV hsv = base;
        hsv.v    = scale8(v, base.v);
        RGB rgb  = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// --- Ripple Pool (fourth pass: new mode) ----------------------------------------------------
// Board-wide analogue of Row Wave above: pressing any key sends a ring expanding outward from
// that key's exact position in *every* direction (not restricted to its row) across the whole
// board. Same squared-distance ring-band test overlay_enter_instance()/orb_ripple_contribution()
// above already use, just centered on each instance's own origin LED instead of a fixed point.
#define RIPPLE_THICKNESS 30
#define RIPPLE_MAX_RADIUS 240 // board-crossing distance -- slightly more than the board's own
                                // diagonal (224x64) so a ripple from any corner still fully clears
#define RIPPLE_TOTAL_MS 650

static uint8_t ripple_contribution(const ripple_wave_t *ev, uint8_t led) {
    if (!ev->active) {
        return 0;
    }
    uint16_t total_ms = target_scaled_ms(RIPPLE_TOTAL_MS, RGBCTL_TARGET_RIPPLE); // fourth pass: was a fixed constant
    uint16_t elapsed   = (uint16_t)(g_rgb_timer - ev->start_time);
    if (elapsed >= total_ms) {
        return 0;
    }
    int16_t  origin_x = (int16_t)g_led_config.point[ev->origin_led].x;
    int16_t  origin_y = (int16_t)g_led_config.point[ev->origin_led].y;
    int16_t  dx         = (int16_t)g_led_config.point[led].x - origin_x;
    int16_t  dy         = (int16_t)g_led_config.point[led].y - origin_y;
    uint32_t dist_sq    = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);

    uint16_t outer = (uint16_t)(((uint32_t)elapsed * RIPPLE_MAX_RADIUS) / total_ms);
    uint16_t inner = (outer > RIPPLE_THICKNESS) ? (outer - RIPPLE_THICKNESS) : 0;
    if (dist_sq > (uint32_t)outer * outer || dist_sq < (uint32_t)inner * inner) {
        return 0;
    }
    return (uint8_t)(255 - ((uint32_t)elapsed * 255) / total_ms);
}

bool ripple_pool_render(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (params->init) {
        ripple_pool_init();
    }
    ripple_pool_sync();

    HSV base = rgbctl_get_effect_color(RGBCTL_TARGET_RIPPLE);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint8_t v = 0;
        for (uint8_t w = 0; w < RIPPLE_POOL_SIZE; w++) {
            v = qadd8(v, ripple_contribution(&ripple_wave_pool[w], i));
        }
        HSV hsv = base;
        hsv.v    = scale8(v, base.v);
        RGB rgb  = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// --- Comet Trail (fourth pass: new mode) ----------------------------------------------------
// Pressing a key launches a small bright comet from that key, travelling outward along a fixed
// diagonal (away from the board's own center, by simple quadrant -- see custom_rgb.h's own
// comment on comet_t for why this is 4 fixed directions rather than a true normalized vector) and
// fading as it goes, with two dimmer echoes of itself trailing a short distance back along the
// same line -- three moving blobs (head + 2 trailing echoes), each the same squared-distance blob
// shape fairy_orb_render's own orb uses, rather than genuine line-segment trail geometry.
#define COMET_TOTAL_MS 550
#define COMET_SPEED_PER_MS 1 // real units/ms per axis -- both axes move at this same rate along
                               // the fixed 45-degree diagonal (see comet_head() below)
#define COMET_HEAD_RADIUS 26
#define COMET_TRAIL_GAP 22    // real units between the head and each trailing echo
#define COMET_TRAIL_COUNT 2   // number of dimmer echoes behind the head
#define COMET_CENTER_X 112     // board's rough horizontal/vertical center (same center the Orb used to start from
#define COMET_CENTER_Y 32      // own starting position above)

// Head position at a given elapsed time, plus the fixed diagonal direction (+-1 on each axis) the
// instance was launched in -- both derived from the same quadrant test, done once here rather
// than stored per-instance, since it's cheap and keeps comet_t the same small shape as the other
// wave pools (see custom_rgb.h).
static void comet_head(const comet_t *c, uint16_t elapsed, int16_t *head_x, int16_t *head_y, int8_t *dir_x, int8_t *dir_y) {
    *dir_x   = (c->origin_x >= COMET_CENTER_X) ? 1 : -1;
    *dir_y   = (c->origin_y >= COMET_CENTER_Y) ? 1 : -1;
    int16_t travelled = (int16_t)(elapsed * COMET_SPEED_PER_MS);
    *head_x            = (int16_t)c->origin_x + (*dir_x) * travelled;
    *head_y             = (int16_t)c->origin_y + (*dir_y) * travelled;
}

static uint8_t comet_blob(int16_t led_x, int16_t led_y, int16_t blob_x, int16_t blob_y, uint8_t radius, uint8_t peak_v) {
    int16_t  dx      = led_x - blob_x;
    int16_t  dy      = led_y - blob_y;
    uint32_t dist_sq = (uint32_t)((int32_t)dx * dx + (int32_t)dy * dy);
    uint32_t r_sq     = (uint32_t)radius * radius;
    if (dist_sq > r_sq) {
        return 0;
    }
    return scale8((uint8_t)(255 - (dist_sq * 255) / r_sq), peak_v);
}

static uint8_t comet_contribution(const comet_t *c, uint8_t led) {
    if (!c->active) {
        return 0;
    }
    uint16_t total_ms = target_scaled_ms(COMET_TOTAL_MS, RGBCTL_TARGET_COMET); // fourth pass
    uint16_t elapsed   = (uint16_t)(g_rgb_timer - c->start_time);
    if (elapsed >= total_ms) {
        return 0;
    }
    int16_t head_x, head_y;
    int8_t   dir_x, dir_y;
    comet_head(c, elapsed, &head_x, &head_y, &dir_x, &dir_y);

    uint8_t fade = (uint8_t)(255 - ((uint32_t)elapsed * 255) / total_ms);
    int16_t led_x = (int16_t)g_led_config.point[led].x;
    int16_t led_y = (int16_t)g_led_config.point[led].y;

    uint8_t v = comet_blob(led_x, led_y, head_x, head_y, COMET_HEAD_RADIUS, fade);
    for (uint8_t t = 1; t <= COMET_TRAIL_COUNT; t++) {
        int16_t trail_x = head_x - dir_x * (int16_t)(COMET_TRAIL_GAP * t);
        int16_t trail_y = head_y - dir_y * (int16_t)(COMET_TRAIL_GAP * t);
        // Each successive echo both dimmer (halved per step) and slightly smaller, so the chain
        // reads as fading away behind the head rather than three equal blobs in a row.
        uint8_t echo_v = comet_blob(led_x, led_y, trail_x, trail_y, COMET_HEAD_RADIUS - t * 6, fade >> t);
        v                = qadd8(v, echo_v);
    }
    return v;
}

bool comet_trail_render(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (params->init) {
        comet_pool_init();
    }
    comet_pool_sync();

    HSV base = rgbctl_get_effect_color(RGBCTL_TARGET_COMET);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint8_t v = 0;
        for (uint8_t w = 0; w < COMET_POOL_SIZE; w++) {
            v = qadd8(v, comet_contribution(&comet_pool[w], i));
        }
        // Fourth pass: this used to force hsv.s = 255 here, the same "reads more clearly at full
        // saturation" reasoning Enter's ring had -- and the same bug report applies (see rgb_
        // control's RGBCTL_TARGET_COMET slot: the person's saturation control should actually
        // reach the thing it's editing). base already carries the target's real saturation.
        HSV hsv = base;
        hsv.v    = scale8(v, base.v);
        RGB rgb  = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// --- Rainbow Wave, right-to-left (fourth pass follow-up: new mode) -----------------------------
// See rgb_effects.h's own comment on rainbow_wave_rtl_render() for the full reasoning -- the
// genuine mirror of the stock CYCLE_LEFT_RIGHT effect, only reachable as a small custom effect
// since modifying CYCLE_LEFT_RIGHT's own file would be a core patch. hsv.s/.v come from the live
// board color/brightness exactly like the stock effect does (rgb_matrix_config.hsv) -- only hue
// is overridden by position+time, same division of responsibility as the effect it mirrors.
// Shared body of the two rainbow waves. `to_the_left` flips the one sign that distinguishes them.
static bool rainbow_wave_render(effect_params_t *params, bool to_the_left) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    // Identical formula to quantum/rgb_matrix/animations/runners/effect_runner_i.h's own time
    // derivation (copied, not reinvented, so this responds to the speed control exactly the way
    // every stock effect built on that same runner already does).
    uint8_t time = scale16by8(g_rgb_timer, qadd8(rgb_matrix_config.speed / 4, 1));
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        HSV hsv = rgb_matrix_config.hsv;
        // Right: x - time (exactly stock CYCLE_LEFT_RIGHT). Left: x + time (the mirror image).
        hsv.h   = to_the_left ? (uint8_t)(g_led_config.point[i].x + time) : (uint8_t)(g_led_config.point[i].x - time);
        RGB rgb = hsv_to_rgb(hsv);
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

bool rainbow_wave_rtl_render(effect_params_t *params) {
    return rainbow_wave_render(params, true);
}

// Sixth pass: stock CYCLE_LEFT_RIGHT, re-registered as a custom effect (see config.h and
// rgb_matrix_user.inc) purely so it can sit directly next to Rainbow Left in the effect list --
// the order of stock effects is fixed inside QMK core, custom effects are the only ones this project
// can place. Same math as the stock effect, so it looks and behaves identically.
bool rainbow_wave_ltr_render(effect_params_t *params) {
    return rainbow_wave_render(params, false);
}


// Both fill from led_high toward lower indices.

void draw_level_bar(uint8_t led_min, uint8_t led_max, uint8_t led_high, uint8_t count, uint8_t percent, RGB color, uint8_t brightness_pct) {
    // How many of the `count` LEDs are fully lit, and the boundary LED's fractional brightness
    // (0-254), derived from the same scaled value so the two can't disagree with each other.
    uint16_t scaled    = (uint16_t)percent * count; // 0 .. 255*count
    uint8_t  full_leds = (uint8_t)(scaled / 255);
    uint8_t  remainder = (uint8_t)(scaled % 255);

    RGB scaled_color = {
        .r = scale8(color.r, brightness_pct),
        .g = scale8(color.g, brightness_pct),
        .b = scale8(color.b, brightness_pct),
    };

    for (uint8_t i = 0; i < count; i++) {
        uint8_t led = led_high - i;
        if (led < led_min || led >= led_max) {
            continue; // outside this frame's RGB_MATRIX_LED_PROCESS_LIMIT chunk
        }
        uint8_t v;
        if (i < full_leds) {
            v = 255;
        } else if (i == full_leds) {
            v = remainder;
        } else {
            v = 0;
        }
        rgb_matrix_set_color(led, scale8(scaled_color.r, v), scale8(scaled_color.g, v), scale8(scaled_color.b, v));
    }
}

#define HUE_BAR_POINTER_V 255
#define HUE_BAR_BASE_V 70 // dim baseline so the pointer LED visibly stands out

void draw_hue_bar(uint8_t led_min, uint8_t led_max, uint8_t led_high, uint8_t count, uint8_t current_hue, uint8_t brightness_pct) {
    uint8_t closest_i    = 0;
    uint8_t closest_dist = 255;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t spectrum_hue = (uint8_t)(((uint16_t)i * 255) / (count - 1));
        uint8_t d            = (spectrum_hue > current_hue) ? (spectrum_hue - current_hue) : (current_hue - spectrum_hue);
        if (d > 128) {
            d = 255 - d; // circular distance -- hue wraps at 255 -> 0
        }
        if (d < closest_dist) {
            closest_dist = d;
            closest_i    = i;
        }
    }
    for (uint8_t i = 0; i < count; i++) {
        uint8_t led = led_high - i;
        if (led < led_min || led >= led_max) {
            continue; // outside this frame's RGB_MATRIX_LED_PROCESS_LIMIT chunk
        }
        uint8_t spectrum_hue = (uint8_t)(((uint16_t)i * 255) / (count - 1));
        uint8_t base_v       = (i == closest_i) ? HUE_BAR_POINTER_V : HUE_BAR_BASE_V;
        HSV     hsv          = {.h = spectrum_hue, .s = 255, .v = scale8(base_v, brightness_pct)};
        RGB     rgb          = hsv_to_rgb(hsv);
        rgb_matrix_set_color(led, rgb.r, rgb.g, rgb.b);
    }
}
