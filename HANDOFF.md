## SIXTH PASS (2026-10) -- backlight-layer fixes, Rainbow order, Clear-Mode hint, encoder, Orb rewrite

Not hardware-tested (no access to the board). Verified by clean compile of both variants against the pinned QMK
commit (ANSI 60,716 B / 131,072 B flash, ISO 56,544 B, 0 warnings; an unmodified ANSI build in the same sandbox
reproduced the 58,676 B of the previous pass) plus a host-side simulation of the new Orb (below). Everything
behavioural on the keyboard itself -- Caps latency, the encoder fix, the fades -- still needs a real flash to confirm.

**FLASHING NOTE.** Stored profile layout changed (RGBCTL_TARGET_ENCODER removed): RGB_PROFILES_LAYOUT_VERSION 5 -> 6,
EECONFIG_KB_DATA_SIZE 218 -> 202 (12 targets x 4 B + effect_id + caps_enabled = 50 B per profile; 4 profiles; +2). The
firmware resets stale profiles itself on first boot, so saved lighting settings are lost once (as on earlier passes).

**1. Backlight config layer (Layer 2).**
- FN color: only Tilde selects RGBCTL_TARGET_FNVIZ now. There was never an "Alt + Tilde" chord: Tilde, Alt and Enter
  were three separate redundant bindings (see the old sec 25/27/32 note). Alt and Enter are now KC_NO on Layer 2 and
  indicators.c lights only Tilde (FNVIZ_KEY_LED).
- Brightness: val_step_for_hold() and the adjust_hold_ms plumbing are gone; VAL_STEP (rgb_control.c) is one constant (4).
  Hold = 4 per 70 ms repeat, ~4.5 s for the full range; tap = exactly 4. Change the one number to taste.
- Caps Lock config: first press selects Caps and enables it; second press now disables the indicator AND deselects it
  (slot back to 1). Before, it stayed selected while disabled, so the layer kept showing the Caps color and the second
  press looked like a no-op. Disabled always shows dim gray; a third press re-selects and re-enables.
- Caps Lock LED latency -- two causes found by reading the code (NOT measured on hardware): (a) the "turned on" edge
  queued a 200 ms indicator_queue entry, and that queue draws the COMPLEMENT color first (black for a white Caps color),
  so the real color was 200 ms late; removed. (b) the LED only reacted to the host's lock-state echo. indicators_caps_key_
  pressed() (called from both keymaps on a real KC_CAPS press) now predicts the new state immediately; the prediction is
  dropped when the host agrees or after CAPS_PREDICT_TIMEOUT_MS (250), so the host stays the source of truth. On Layer 2
  the live Caps state no longer paints over that layer's own Caps-key LED.

**2. Rainbow order.** Stock effect order is fixed inside QMK core and custom effects always sort after it, so the only way
to put Rainbow Left next to Rainbow Right was to make both custom: stock ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT is off
(config.h) and rainbow_wave_ltr (rgb_effects.c, registered in both rgb_matrix_user.inc files) reproduces it exactly
(shared rainbow_wave_render(), sign flipped for left). Same math as the stock effect. Effect NUMBERS shift (hence the
profile reset above); if a VIA JSON lists effect names by index it needs the same order.

**3. Clear Mode (Fn+Esc) hint.** Esc is red as before. After Fn has been RELEASED once since entering the mode (Fn is
still held from the Fn+Esc that started it, so the first hold must not count) and Fn is pressed again, Esc fades
red -> the FN color (RGBCTL_TARGET_FNVIZ, the color set with Tilde) after the same 150 ms FN_HELP_DELAY_MS Fn-help uses,
over 200 ms (the RGB bar's fade length); 120 ms back to red on release. Only Esc changes. The fade level is derived from
timestamps, not per-call accumulation, because the indicator hook runs once per LED chunk.
The request said "the color assigned to FN keys (not the custom color set by Tilde)" but Tilde is what sets the FN
color -- implemented as the FN color; say so if something else was meant.

**4. Encoder direction-reversal.** Nothing in this repo drops ticks (rotation always passes through to the host). This
is QMK's documented quadrature-driver symptom ("skips pulses when it changes direction"): the 4x driver carries a
pulse count between detents and a non-zero leftover at rest swallows the first opposite tick. Fix: config.h
ENCODER_DEFAULT_POS 0x3 (resting pin state, both high -- the pins are input-with-pull-up). ASSUMES the encoder's detent
position is both-contacts-open; if the symptom persists or tick direction becomes odd, try 0x0 or remove the define.

**5. Cleanup.** Encoder wave removed completely (custom_rgb.c/.h, rgb_effects.c, rgb_control.h/.c, rgb_profiles.c,
keymap.c, config.h). Slots on Layer 2 in reactive_energy are now 1 Base, 2 Enter, 3 Backspace, 4 Delete, 5 Space, 6 Esc;
key 7 is unbound. Kept on purpose: the encoder-PRESS debounce (guards the real Play/Pause) and glow_note_encoder_input()
for rotation/press (still counts as real input for the idle timer). Delete speed: delete_speed_pct() in rgb_effects.c is
a Delete-only curve, identical to the shared target_scaled_ms() curve from mid-speed up, continuing to stretch below it
so the slowest setting takes ~1 s (was 416 ms = 260 ms x 160%). No jump at the join.

**6. Fairy Orb rewrite** (features/orb_motion.{h,c} + fairy_orb_render() in rgb_effects.c; old code in custom_rgb.c/.h
deleted). The orb is a vehicle with a heading and a low-pass-filtered curvature, never a point chasing a target. Curvature
wish = smooth noise (smoothstep between random keys, new keys forever) + edge steering. Everything is integrated in
PATH-LENGTH space, so the speed setting scales velocity only -- the path shape and smoothness are unchanged -- and speed
changes are eased in over 250 ms. Edge steering predicts where the orb would come to rest if it started its tightest turn
now, picks the turn side by checking each arc against the board box, keeps turning until it heads clearly inward, and has a
smooth inward drift as a last resort past the box. The tail is path samples dropped every 5 units, faded by distance
travelled (so it looks the same at any speed). No input hooks anywhere: it runs the same typing or idle.
Host simulation (orb_motion.c compiled on a PC with a sin/cos shim; 30 seeds, up to 1 h per run, 3 speeds): the center
never strays more than ~25 units past the keys (x -25..250, y -17..83); curvature never exceeds 1/22 per unit; no stalls;
per-frame travel after a 0 -> 255 speed jump ramps smoothly (0.41 -> 1.5 units/frame). It does patrol the board edges
somewhat more than the middle (center cells ~18% of the time vs 25% if uniform) -- tunable via ORB_SOFT_X/Y and the
noise amplitude. Tunables in orb_motion.c are #ifndef-guarded so a host test can override them with -D.
Rejected on the way: a "squashed pond" model (steer in a 3x taller arena, squash back) -- contained well but turned into
hairpins (~148 deg per 10 units) at the flattened ends.

**Not verified / open.** Caps latency and the encoder fix are diagnosed from the code and QMK docs, not observed. Orb
look (radius 34, tail 72 units) is from an ASCII render of the real LED positions, not seen on the board.

---

## FIFTH PASS (2026-09) — per-profile/per-target settings, Fairy Orb redesign, encoder fixes

Not hardware-tested (no access to the board); verified by clean compile of both variants (ANSI 58,676 B flash /
~9.5 KB RAM, ISO 54,512 B) plus host-side simulation of the new Fairy Orb motion (no stalls over 400k ticks at any
speed). READMEs were intentionally NOT updated this pass (owner is rewriting them) -- the old ones are stale.

**Settings storage (rgb_profiles.h/.c, rgb_control.c).** One struct per profile: effect_id + targets[] of
{hue,sat,val,speed} for EVERY target (Base included) + caps_enabled; 4 independent copies (218 B EEPROM, was 59).
Nothing is shared between profiles anymore. Speed is now per target: for BASE it mirrors the real global
(stock effects/Darkening/Rainbow Wave can only read that); every other target has its own stored speed.
New target RGBCTL_TARGET_ORB (Fairy Orb gets its own color/brightness/speed). A layout-version byte makes the
firmware reset stale old-layout EEPROM by itself on first boot (still fine to Clear EEPROM manually).
Number keys 1-7 in Custom Mode select Base/Enter/Backspace/Delete/Space/Encoder/Esc; Left/Right now change THAT
slot's own speed (each animation reads it via target_scaled_ms()/target_scaled_elapsed() in rgb_effects.c).

**Mode order.** Custom modes: rainbow_wave_rtl, fairy_orb, darkening_glow, row_wave, ripple_pool, comet_trail,
reactive_energy (Custom Mode last). Stock effects always sort first (core order, not patched); ISO had Custom Mode
FIRST until now -- fixed. Mode numbers changed, so old stored effect ids are meaningless (hence the version byte).

**Fn layer.** Esc added to fn_help_leds (Fn+Esc = Cleaning Mode); checked it can't collide with Cleaning Mode's
red Esc, the reset-hold Esc, or other layers. Brightness: could not find the reported coarseness in the step math
(Fn used 8, stock Base used 16 -- Fn was already finer on paper), so brightness now uses ONE rule for all targets
including Base: tap = step 2, accelerating with hold time (val_step_for_hold()). Speed indicator/bar now show the
selected target's speed.

**Animations.** Row Wave 260->420 ms. Enter's ring is now Ripple-Pool style (same thickness, ~650 ms, fades);
INTERPRETATION of "second wave mode" = Ripple Pool. Erase sweeps (Enter/Backspace/Delete) now only clear glow that
pre-dates them (wave_start), so keys typed during a sweep are not wiped. Overlapping Enter rings composite by max.

**Fairy Orb.** No reaction to real or simulated keypresses; no ripple; short fading trail (orb_trail[]); own
speed. (Earlier freeze bugs -- easing step rounding to 0, boundary clamp -- stay fixed.)

**Encoder Play/Pause during idle simulation** -- no single smoking gun; four real defects fixed: (1) a SIMULATED
encoder wave called glow_note_encoder_input(), faking real input (reset idle clock + 12 ms proximity window that could
swallow a real press) -> idle sim now uses encwave_add(); (2) the typing-proximity guard no longer suppresses the HID
Play/Pause, only the ring; (3) press debounce moved to 32-bit timer (16-bit aliased every 65.5 s); (4) glow_ms_since_
real_input() was a wrapping 16-bit age (idle sim switched itself off 20 s out of every 65 s) -> now 32-bit, saturating.
If Play/Pause still fails during idle simulation on hardware, the cause is outside these paths -- report exact steps.

**Other.** Idle gap tightened earlier (650-1900 ms). ISO builds need `-Wno-error=lto-type-mismatch` (kept).

---

## FOURTH PASS FOLLOW-UP (2026-09) — real hardware testing, a dozen concrete reports

Everything below came from actually flashing and using the fourth pass's build. Ordered here by
what mattered most to fix correctly, not necessarily the order in the person's own message.
Verified: clean compile, 0 warnings, both variants; ANSI flash 58,248/131,072 B (44.4%), RAM
9,132/28,672 B (31.8%); ISO flash 54,264/131,072 B (41.4%).

### Encoder firing while typing -- explicitly asked to be solved first

Traced every call site of glow_trigger_encoder_wave() from scratch rather than trusting this
document's own prior description of them. Rotation (KC_VOLU/KC_VOLD via ENCODER_MAP_ENABLE) had
*zero* validation of any kind -- a previous pass's own comment (still present, features/
custom_rgb.c) explains why a rate-limiting debounce like the press's own 150ms one was
deliberately never applied to rotation: it would break genuine fast scrolling, which needs many
real ticks close together. That reasoning is still correct and this pass doesn't revisit it.
Added a different, narrower check instead -- `glow_encoder_rotation_ok()`: reject a rotation tick
(or, alongside the existing 150ms press debounce, a press) that lands within ~12ms of an ordinary
real keystroke elsewhere on the board. This doesn't look at spacing *between* rotation ticks at
all, so it cannot affect fast scrolling regardless of how many genuine ticks arrive in a row; it
only asks whether a real key was *also* just hit in a near-instantaneous window, on the theory
that genuinely independent human actions (typing, and separately deciding to touch the encoder)
essentially never land within single-digit milliseconds of each other, while electrical crosstalk
coupled onto the encoder's lines from a nearby key's own matrix activity would. Documented clearly
in both the code and both READMEs as a reasoned best effort given no access to the physical board
to confirm against, not a guaranteed fix -- if it persists, that's meaningful evidence the cause
is something this check can't reach (e.g. genuine incidental contact with the knob while typing
nearby, which isn't a firmware bug at all).

### Enter's saturation, and the same bug copied into Comet Trail

`overlay_enter_instance()`'s ring phase force-set `hsv.s = 255` ("so the shockwave reads clearly
regardless of the configured profile") -- explicitly documented as deliberate by a prior pass, but
exactly the reported bug: Enter's own LED (phases 1/2, unaffected) responded to the saturation
control; the ring itself never did. Removed the override -- it now uses the target's real stored
saturation like every other phase/effect already does. The fourth pass's own Comet Trail had
copied this exact "full saturation" pattern from Enter (comet_trail_render(), rgb_effects.c) --
fixed there too rather than leaving a newly-introduced copy of a bug just fixed elsewhere. Row
Wave/Ripple Pool/Delete/Backspace/Space/Escape/the encoder ring were all individually checked
against the same pattern and are clean (they already read a target's full stored HSV unmodified).

### Fairy Orb: two distinct, now-fixed bugs behind "freezes... only comes back to life after some
time" / "doesn't always ripple when it reaches a key"

1. **Primary cause**: `orb_tick()`'s easing step is `dx >> ORB_EASE_SHIFT` (dx/16). Integer
   right-shift of a *positive* value from 0-15 rounds to exactly 0 -- so once the orb eased down
   to, say, dx=10 on an axis (short of ORB_ARRIVE_TOLERANCE=4, so not yet "arrived", but inside
   this shift's dead zone), that axis's step became 0 and stayed 0 *forever*: dx never shrinks, so
   the tolerance is never reached either, and the axis simply stops. Recovers only when an
   unrelated event (a new real or simulated keypress) retargets the orb with a fresh, larger dx --
   exactly "comes back to life after some time". Asymmetric by sign (GCC's arithmetic right shift
   on a *negative* dx in this same range still yields -1), so this affected roughly half of all
   arrivals per axis, on the negative-coordinate approach side. Fixed directly: easing is never
   allowed to return exactly zero while real distance remains (steps to +-1 instead in that case).
2. **Secondary, contributing cause**: `orb_pick_random_target()` (the "smoother wander" fix from
   the fourth pass itself) clamped a random step to the board's margin -- which, whenever the orb
   was already at/near that margin and the step pointed further outward, collapsed the *actual
   distance moved* to near zero (clamping onto the same spot it started from). A bounded random
   walk visits its own boundary often, so this could compound the freeze above. Fixed by
   reflecting off the boundary instead of clamping onto it, which preserves the step's full
   magnitude regardless of which direction it ends up pointing.

Both fixes together should also resolve "doesn't always generate a wave when it reaches a key" and
"should just start floating on its own after creating a wave" -- both were very plausibly downstream
of the same stalls (a stalled orb can neither arrive-and-ripple nor resume wandering afterward).
Genuinely never rippling on *every single keystroke* during continuous fast typing is still
expected -- the orb chases the *most recent* key and only ripples on actual arrival, so several
keystrokes in a row before it catches up means only the one it actually reaches gets a ripple; this
is the intended "chase" character (spec sec 25.1), not a bug, and is called out as such in the
README rather than "fixed" into firing on every keystroke.

### "Keys stay lit if Del/Backspace/Enter is pressed during a custom animation... should fade out"

`EFFECT_POOL_SIZE` (Enter/Backspace/Delete/Space's own ring-buffer of wave instances, separate
from GLOW_POOL_SIZE) was still 8 -- untouched by this pass's own earlier GLOW_POOL_SIZE 32->128
bump. Each instance's *own* growing/shrinking radius is what erases glow_pool as it travels
(tick_effect_pools()); an instance evicted (ring-buffer-overwritten by a newer press) before
finishing never reaches its own full board-crossing radius, so whatever it would have gone on to
erase simply never gets erased by anything else. 8 was already sized "for e.g. Backspace under
fast OS key-repeat" per a prior pass's own comment -- but a sustained OS repeat rate (often
20-33/s while a key is held to delete/add several lines at once) against each instance's own
~260-280ms lifetime needs on the order of 6-9 slots *just to keep up*, with zero margin for
anything else happening at the same time. Widened 4x to 32 (same proportional bump GLOW_POOL_SIZE
got, same class of reasoning). ESCSEQ_POOL_SIZE and ENCWAVE_POOL_SIZE, which could plausibly hit a
milder version of the same issue (Escape held for repeat; fast encoder scrolling), widened 8->24
for the same reason, smaller margin since they're individually less likely to sustain that rate.
Checked all three individual clearing functions (glow_pool_clear_within_radius/outside_radius/
x_at_or_above) and each instance's own radius formula for whether they reach full board coverage
by their own natural completion (not cut short) -- confirmed all three do, so this pool-eviction
gap, not a math error in the clearing itself, was the real mechanism.

Separately, but related: "if you press a key while auto-pressing [idle simulation] is active, keys
continue to light up randomly... has to finish its cycle" was also accurate and is now fixed --
see "Idle simulation now stops immediately" below.

### Idle simulation now stops immediately, and runs a little faster

Added a `simulated` field to glow_event_t/row_wave_t/ripple_wave_t/comet_t (every pool idle
simulation can populate). glow_track_real_input() -- which already runs every scan and already
knows, this exact frame, the moment real input resumes -- now also force-clears any still-active
`simulated`-tagged entry across all of them at that instant, instead of letting it run out its
normal fade/cycle the way a real keystroke's own glow correctly still does. GLOW_IDLE_MIN_GAP_MS/
MAX_GAP_MS tightened 900-2600 -> 650-1900 ("a tiny bit faster" than the previous ~1.75s average,
not a wholesale pacing redesign).

### Equalizer removed

Both "very strange and buggy" on its own and an explicit separate "let's skip" on ever pursuing
real audio reactivity (the entire point of the concept) made it not worth keeping as a keystroke-
only mode with neither its original purpose nor a following of its own. Removed end to end:
RGBCTL_TARGET_EQUALIZER/EQ_NUM_BANDS/eq_band_hit_time[]/equalizer_render() and its registration in
both keymaps' rgb_matrix_user.inc, plus the EEPROM slot it used (RGB_EFFECT_COLOR_COUNT 12 -> 11,
EECONFIG_KB_DATA_SIZE 62 -> 59 -- another Clear-EEPROM-after-flashing round, same as every prior
layout change). Six standalone modes remain, same static-ish -> complex ordering as before with
Equalizer's slot simply removed from the middle.

### Two stock-effect "duplicates", and a new custom mode to genuinely fix one of them

Checked both against the real, pinned-commit QMK source (fetched fresh via the same sparse-clone
approach as the fourth pass's own stock-ordering check, not assumed) rather than guessing:
- **Two identical solid colors**: ENABLE_RGB_MATRIX_ALPHAS_MODS's own source
  (animations/alpha_mods_anim.h) derives its second ("mod") color as
  `hsv.h += rgb_matrix_config.speed` -- at whatever speed this board is actually set to, that hue
  shift can be small enough to read as practically the same color as plain Solid Color right next
  to it. Disabled via config.h (a plain ENABLE_RGB_MATRIX_* toggle, not a core change).
- **Two identical left-to-right rainbow waves**: ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON's own
  source (animations/rainbow_moving_chevron_anim.h) has `abs8(y - center_y) + (x - time)` at its
  core -- the exact same `x - time` sweep Cycle Left/Right uses, plus a chevron offset from
  vertical center that only varies across this board's own short row count (6 distinct row
  y-values, keyboard.json) and reads as minor here. Disabled the more redundant-looking of the two
  (kept Cycle Left/Right, the more clearly-named one).

The person's own preference was explicit -- "these should be 2 different modes: one right to left
and the other left to right" -- and no stock ENABLE_RGB_MATRIX_* effect runs `x + time` (the actual
mirror), so a config toggle alone can't deliver that; making CYCLE_LEFT_RIGHT itself run backward
would mean editing its own file, a core patch. Added a new, small custom effect instead --
`rainbow_wave_rtl` (rgb_effects.c/.h) -- that replicates CYCLE_LEFT_RIGHT's own math exactly
(including its precise time/speed formula, copied from quantum/rgb_matrix/animations/runners/
effect_runner_i.h rather than approximated, so it responds to the speed control identically) with
the one sign flipped. Registered first in both keymaps' rgb_matrix_user.inc (ahead of
darkening_glow) since, unlike every other mode this project owns, it has no keypress reactivity at
all -- a continuous, time-only ambient pattern, same character as the stock rainbow/cycle effects
it sits next to. No RGBCTL_TARGET_* of its own (falls through target_for_slot()'s default to
RGBCTL_TARGET_BASE) -- like the stock effects it complements, its whole hue range is the point,
not one configurable accent color.

### Wave/comet modes didn't respond to the speed control at all

Checked: *no* custom effect in this whole file referenced rgb_matrix_get_speed()/
rgb_matrix_config.speed except reactive_energy's own glow falloff and Fairy Orb's own cruise speed
-- Row Wave (pre-existing, not just this pass's new modes) plus Ripple Pool/Comet Trail/the orb's
own arrival ripple (all added this pass, inheriting Row Wave's pattern faithfully, bug included)
all used a fixed total-duration constant. Reusing reactive_energy's own division-based formula
directly (65,535/(speed+1) ms) would swing these effects from a fraction of a second up to over a
*minute* at the lowest speed setting -- wildly outside the range they were tuned around, since that
formula was built for glow's own falloff curve specifically. Added a separate, gentler linear scale
purpose-built for a fixed total-duration effect instead (`speed_scaled_ms()`, rgb_effects.c): 1.6x
the tuned baseline at the slowest speed setting, 0.5x at the fastest, ~1.0x at the mid setting these
constants were originally tuned against. Only timing scales -- how far a wave/comet travels is
unchanged, so "speed" reads as exactly that, not also "bigger/smaller".

### Brightness/speed being per-effect-adjustable: verified, not a bug

Checked `rgbctl_adjust_val()` (rgb_control.c): brightness was already correctly writing to
whichever target `rgbctl_current_target()` resolves to, so Row Wave/Ripple Pool/Comet Trail's own
brightness was already independently adjustable and stored -- no bug found there. Speed
(`rgbctl_adjust_speed()`) is, confirmed, a single board-wide value with no per-target storage at
all, by original design (rgb_control.h's own longstanding comment says so). Given the speed-wiring
fix above, both brightness and speed now visibly, correctly affect every color-selectable mode --
judged this as fully addressing the practical complaint without taking on a much larger, riskier
EEPROM-format change (a 4th stored byte per target) the person didn't explicitly ask for.

### Notes for a fifth pass

- The `simulated` field pattern (glow_event_t and friends) is now established -- if a future new
  pool needs idle-simulation support, follow the same shape (field on the struct, set at every
  `_add()` call site, cleared in force_clear_all_simulated()).
- rainbow_wave_rtl's placement (first, no accent color) is a reasoned choice, not a spec
  requirement -- revisit freely if it doesn't feel right in practice.
- EFFECT_POOL_SIZE/ESCSEQ_POOL_SIZE/ENCWAVE_POOL_SIZE's new sizes are reasoned against realistic
  OS key-repeat rates, not measured against this exact board's actual repeat rate (no hardware
  access this session either) -- if pool exhaustion is somehow still observed, that's the first
  place to look, and the numbers can go higher still very cheaply.
- Every fix in this round came from a specific, reproducible-sounding report -- none were spawned
  from a broader unprompted audit. There may well be more; the person's own closing instruction to
  "look for other bugs" was honored within the areas the specific reports already pointed at
  (encoder, saturation, Fairy Orb, erase completeness, idle behavior, stock-effect redundancy,
  speed responsiveness), not by re-auditing files this round left untouched (indicators.c, rgb_
  profiles.c's storage logic, the keymap's non-RGB behavior).

---

## FOURTH PASS (2026-09) — person's own follow-up bug reports + a large feature request

Six specific bugs reported from actually using the board, plus: idle simulation generalized
across every mode, a "Moving Ball" (Fairy Orb) revamp, three brand-new standalone RGB modes, mode
reordering, and both README files brought fully up to date (including finishing the Russian
translation, which this repo had previously left partly in English -- see git history/previous
archive if that partial version is ever needed again). Verified: clean compile, 0 warnings, both
variants, from a from-scratch build environment (see "Build environment used this pass" below,
worth reading before starting a fifth pass); ANSI flash 58,468/131,072 B (44.6%), RAM 8,552/28,672
B (29.8%); ISO flash 54,612/131,072 B (41.7%), RAM 8,520/28,672 B (29.7%).

### Real bugs found and fixed this round

- **Alt lighting up under a plain Fn hold.** Root cause: `indicators.c`'s `fn_help_leds[]] (the
  Fn-help highlight list) included LED 0, which is RAlt -- verified against the list's own
  same-line comment and `custom_rgb.c`'s `is_modifier_led()`, not assumed from the name alone.
  RAlt genuinely does do something under Fn (opens the Options layer, sec 27), which is presumably
  why a past pass added it, but the person specifically asked for the highlight gone for this key.
  Removed LED 0 from `fn_help_leds[]`, `FN_HELP_LED_COUNT` 12 -> 11. Deliberately left Layer 2's
  own separate use of *Left* Alt (one of the three keys that select the Fn-indicator color while
  the RGB Settings layer is open, sec 13/25) untouched -- confirmed via the ANSI keymap's Layer 2
  LAYOUT table that this is a different physical key (LAlt, LED 78) in a different context, not
  something the request was about.
- **"Phantom" lighting after ~10s idle, including apparently while typing.** This is the existing
  Idle feature (originally sec 2/5, README §15) firing exactly as spec'd, not an uncontrolled bug
  -- the person's own "~10 seconds" nearly exactly matches `GLOW_IDLE_THRESHOLD_MS`'s prior value.
  Traced the full mode-independent real-input-tracking path this pass started from (the second
  pass's own fix, `glow_track_real_input()` called unconditionally from `matrix_scan_user()`) and
  found it correctly wired for both keymaps -- no residual cross-mode staleness bug of that
  specific class remains. What's genuinely true: a 10s mid-session pause (rereading a line,
  thinking) is well within what a person doing active work would still call "typing", even though
  the mechanism firing exactly then was correct per its own spec. Widened
  `GLOW_IDLE_THRESHOLD_MS` 10000 -> 20000 as a direct, minimal response to that specific
  complaint -- see custom_rgb.h's own comment on it for the full reasoning. Also hardened a
  real-but-extremely-rare (~1/65536 idle cycles) `idle_next_event_at` sentinel-collision edge case
  (a wraparound landing exactly on 0, the "unscheduled" sentinel value) while in this code, and
  folded the previously-Custom-Mode-only idle spawn logic into the same mode-independent-scheduler
  generalization the person asked for in their "Idle Simulation Overhaul" request below, since the
  two are the same code path. The encoder half of the original combined report ("keys AND the
  encoder randomly light up") is a *different*, already-addressed mechanism (third pass's 150ms
  press debounce, sec above) -- idle events have never touched the encoder (no LED of its own to
  land on) and still don't; README §23 now says so explicitly so the two don't get re-conflated.
- **Custom Mode's Enter/Backspace/Delete wave-erase "clunky".** Read overlay_enter_instance()/
  overlay_backspace_instance()/overlay_delete_instance() and their pool-clearing call sites in
  full: the smooth-wave-erase-in-its-path behavior the person described is already exactly what's
  implemented (confirmed, not assumed) -- found no actual logic defect to fix here. Two concrete,
  scoped improvements instead, both plausible contributors to a "clunky" impression for a fast
  typist: (1) the Key Memory Buffer bug just below, since a wave sweeping through a pool that's
  mid-overwrite from a full ring buffer would visibly pop/skip entries out from under it; (2)
  Backspace's own total animation time (200ms) was noticeably faster than its two siblings (Enter
  280ms, Delete 260ms) for no documented reason -- harmonized to 260ms (matching Delete, the
  closer of the two) so all three read as one consistent family of motion rather than Backspace
  feeling abrupt next to the other two.
- **Settings-layer color inconsistency: `\` and Left/Right.** Read `render_rgbctl_layer()`
  (indicators.c) end to end against every LED it touches. `\` (LED 36) was never assigned a color
  at all -- it's the layer's own hold-to-stay-here key, and (unlike every other functional key on
  this layer) simply sat OFF the whole time, next to over a dozen lit controls; given it's
  "you're here", not a real editable control, lit it plain informational white (same treatment
  Home already gets). Left/Right (speed, LEDs 2/4) used a hardcoded decorative "sky blue"
  (`0, scale8(v,160), v`) -- the only one of the four adjustable-parameter controls
  (saturation/brightness/speed) not using the shared white-family "intensity readout" style
  Up/Down and `,`/`.` already share; switched to that same style for consistency. Left
  deliberately untouched: `[`/`]` (mode select, violet) and Space (RGB toggle, amber) also use
  fixed decorative colors, but the person's report named `\` and Left/Right specifically, not
  these -- treated as intentional signature colors for those two rather than something to also
  convert, consistent with "Strict Scope."
- **Custom Mode's Escape had no color of its own.** `overlay_escape_sequence_all()` read
  `rgb_matrix_config.hsv` (the live board base color) directly -- the only one of Custom Mode's six
  special-key animations with no independently-configurable target, confirmed against
  `rgb_control.h`'s enum (Base/Enter/Backspace/Delete/Space/Encoder, no Esc). Added
  `RGBCTL_TARGET_ESC` (inserted right after ENCODER so the numbered-slot run stays contiguous,
  1-7), a 7th numbered slot (physical key `7`, LED 29 -- verified against `keyboard.json`, not
  assumed from the existing 1-6 pattern's spacing), and switched the Escape cascade's color source
  to it, combined with its own fade the same `scale8(pct, hsv.v)` way the encoder wave already
  combines its own ring fade with `RGBCTL_TARGET_ENCODER`. EEPROM grew accordingly (see below).
- **Key Memory Buffer ("only remembers 20-30 keys").** `GLOW_POOL_SIZE` was 32 (already above the
  original spec's own suggested 20-30 ceiling from an earlier pass) -- the person's own count
  ("20-30") suggests they were seeing eviction closer to that ceiling than to 32, plausibly from
  idle-simulated events (see above) sharing the same pool as real keystrokes and eating into the
  same 32 slots during a long typing session with the board otherwise idle between bursts. Bumped
  to 128 (4x): 4 B/slot, so +384 B RAM for this one pool against a 28,672 B budget -- trivial, and
  per-slot render/insert cost is one cheap comparison either way, so no framerate concern at this
  MCU's scan/render rate (same reasoning the original spec's own sec 6/47 already used to justify
  24 -> 32).

### Features implemented this round

- **Idle Simulation Overhaul.** Idle-press spawning used to be nested entirely inside
  `glow_pool_sync()`, which only reactive_energy/darkening_glow ever call -- every other mode got
  no idle events at all. Split cleanly: `glow_idle_simulation_tick()` (custom_rgb.c) is now the
  mode-independent scheduler, called every scan from `matrix_scan_user()` in both keymaps (the
  same "unconditional, regardless of active mode" convention `glow_track_real_input()` already
  established), and it looks at `rgb_matrix_get_mode()` itself to dispatch a simulated press into
  whichever pool the *currently active* mode actually reads (glow_pool for reactive_energy/
  Darkening -- occasionally one of Enter/Backspace/Delete/Space/Escape/the encoder's own trigger
  function instead of a plain glow, about 1 in 6, so the mode's full character shows over time,
  not just its plainest glow; row_wave_pool for Row Wave; the new ripple/comet pools for Ripple
  Pool/Comet Trail; `eq_band_hit()` for Equalizer; a small shared "last simulated press"
  led/tick pair, mirroring the existing real-input tracking shape, for Fairy Orb to chase). A mode
  this can't safely reach into (a stock QMK effect) simply gets nothing, consistent with this
  project's own "Core patches: NONE" throughout. Suppressed while any Fn-held/Layer-2/Options/Num
  overlay is active (reuses the existing `any_overlay_suppressing_now()`) -- unlike real input,
  which physically cannot fire Enter/Backspace/etc from those layers, a simulated press could have,
  which is exactly the staleness class of bug sec 19/45 already exists to prevent; simplest correct
  fix is not generating one there at all.
- **"Moving Ball" (Fairy Orb) revamp**, all three parts of the request:
  - *Ripple on arrival*: previously arrival at a chased key picked the next wander target with no
    visual event at all. Added a small dedicated pool (`orb_ripple_pool`, 3 slots) and a squared-
    distance ring-band render (same shape `overlay_enter_instance()`'s own ring already uses),
    triggered only when `orb_chasing` was true at arrival -- not on every ordinary wander-point
    arrival, matching the request's own wording ("the target key").
  - *Smoother wander*: `orb_pick_random_target()` previously picked a fully independent random
    point anywhere on the whole board on every arrival -- each individual transition was already
    eased (a prior pass's own fix), but a long idle wander could still jump corner-to-corner leg
    after leg, which reads as erratic even with each leg smooth. Now picks a bounded step
    (40-100 units) from the *current* position instead, so consecutive legs stay roughly
    continuous.
  - *Reacts to idle-simulated presses*: `orb_tick()` now also checks the shared "last simulated
    press" state (see Idle Simulation above) alongside real `g_last_hit_tracker` hits, real input
    taking priority if both land the same tick.
- **Three new standalone RGB modes** (`ripple_pool`, `comet_trail`, `equalizer` -- registered in
  both keymaps' `rgb_matrix_user.inc`, render bodies in `rgb_effects.c`, state/pools in
  `custom_rgb.c`, each with its own `RGBCTL_TARGET_*` accent color via the same one-slot pattern
  Row Wave already established):
  - *Ripple Pool*: board-wide analogue of Row Wave -- a ring expands in every direction from the
    pressed key, not just along its row.
  - *Comet Trail*: a small comet launches from the pressed key along a fixed diagonal (one of 4,
    picked by which quadrant of the board the key is in, away from center) and fades, with two
    dimmer trailing echoes. Deliberately not true normalized-vector motion -- would need a sqrt
    this MCU doesn't have an FPU for -- see custom_rgb.h's own comment on `comet_t`.
  - *Equalizer*: the board splits into `EQ_NUM_BANDS` (10) vertical column bars, each a
    `sin8()`-driven idle bounce (phase-offset per band) plus a decaying boost on a nearby keypress.
    **Not audio-reactive** -- checked directly (grepped the whole tree for audio/mic/ADC/analog,
    nothing; confirmed no ADC/microphone anywhere in this board's own config, matching the fact
    that this is a keyboard controller with no such peripheral) rather than assumed, and disclosed
    as such in both READMEs so nobody mistakes it for real audio reactivity it structurally cannot
    have without different hardware.
- **Mode reordering.** Requested: static modes first, transitioning to more complex/animated,
  Custom Mode (reactive_energy) last. Stock QMK effects are appended to the mode enum before any
  of this project's own custom effects regardless of registration order (verified directly against
  this pinned commit's own `quantum/rgb_matrix/animations/rgb_matrix_effects.inc` -- see "Build
  environment" below for how; not patched, consistent with this project's own "Core patches: NONE"
  throughout, so their *relative* order among themselves is inherited from QMK's own upstream list
  rather than something this fork controls). The actionable lever is this project's own custom
  effects' relative order, which is exactly their declaration order in `rgb_matrix_user.inc` --
  reordered to: Darkening, Row Wave, Ripple Pool, Comet Trail, Equalizer, Fairy Orb,
  reactive_energy (Custom Mode) last, mirrored in both ANSI and ISO's copies of that file. Default
  boot mode (`RGB_MATRIX_DEFAULT_MODE`, config.h) is unaffected -- it references
  `RGB_MATRIX_CUSTOM_reactive_energy` by name, not by position, so it still correctly points at
  Custom Mode regardless of where that lands in the cycle order.

### EEPROM layout changed again

`RGBCTL_TARGET_ESC`/`_RIPPLE`/`_COMET`/`_EQUALIZER` added to `rgb_control.h`'s enum ->
`RGB_EFFECT_COLOR_COUNT` (rgb_profiles.h) 8 -> 12 -> `EECONFIG_KB_DATA_SIZE` (config.h) 50 -> 62
(+12 B). The existing `_Static_assert`s in `rgb_profiles.c` (both the struct-size one and the
`RGB_EFFECT_COLOR_COUNT == RGBCTL_TARGET_COUNT - 1` one) caught this automatically during the
build -- worth keeping for a fifth pass, they're cheap insurance. As with every previous EEPROM
layout change in this project: **Clear EEPROM once after flashing this build** (Options layer,
`Fn`+`RAlt` held, triple-tap `Z`) -- old profile bytes at the previous, narrower offsets will not
automatically migrate. Both READMEs now say so in their own Troubleshooting sections.

### A genuine toolchain false-positive, not a code bug (ISO build only)

After all the above, ISO's link step failed: `-Werror=lto-type-mismatch` on `g_led_config`,
reported between `quantum/rgb_matrix/rgb_matrix.h`'s own `extern` declaration and
`default_keyboard.c`'s generated definition -- **both files this project doesn't touch**, and the
ANSI variant (identical shared `features/*.c`) linked cleanly. Confirmed a GCC 13.2/LTO false
positive rather than an actual ODR/type violation before treating it as one: the exact same source
links and runs fine with `LTO_ENABLE=no` for this one variant, proving the C-level semantics are
correct and this is specifically an LTO type-debug-info merging fragility (a known GCC LTO
category, particularly with `weak` symbols across projects with very many translation units --
this one's ChibiOS HAL alone is dozens of `.c` files), most likely tipped over by this pass's added
code size/shape rather than caused by any one specific line. Fixed by downgrading just that one
diagnostic back to a warning for the ISO `via` keymap specifically (`CFLAGS += 
-Wno-error=lto-type-mismatch` in `iso/keymaps/via/rules.mk`, with the reasoning above written
inline there too) rather than disabling LTO outright, which would have cost real code size/
performance on both variants to work around a diagnostic already proven spurious.  If a future
pass hits this again (or ANSI starts hitting it too), the same narrow fix applies; if it ever
stops reproducing, this override can presumably be dropped, though there's no strong reason to
chase that.

### Build environment used this pass

Not present in earlier passes' own notes, so worth recording: this pass built and ran a real,
complete build of both variants (not just read-and-reason) using this project's own documented
recipe (see "Continuing this project" below) --
`apt-get install gcc-arm-none-eabi` (13.2.1, matches), `pip install qmk --break-system-packages`
(1.2.0, matches), a shallow `qmk_firmware` checkout at the exact pinned commit plus the three
pinned submodules (chibios/chibios-contrib/printf) at their own exact pinned commits, all fetched
straightforwardly over plain `git fetch --depth 1` -- no proxy or auth issues, ~730 MB total,
a few minutes end to end. Confirmed the *unmodified* source this pass started from already matched
this document's previously-recorded sizes almost exactly (ANSI within 116 B, ISO exact; RAM within
4 B) before making any changes, which is what gave every size delta reported above real meaning
rather than being compared against an unverified baseline. Also used this environment to check the
real upstream stock-effect ordering directly (`quantum/rgb_matrix/animations/rgb_matrix_effects.inc`
at the pinned commit, via a sparse partial clone) rather than relying on trained-in memory of QMK's
effect list, which is exactly the kind of thing worth verifying fresh each time rather than
assuming carries over correctly. Recommend a fifth pass do the same rather than reasoning about
compilability from source-reading alone -- it caught one real, non-obvious bug this pass (the
`ripple_pool` naming collision below) that pure code review very plausibly would have missed.

### Notes for a fifth pass

- **Naming collision caught only by actually compiling**: the new Ripple Pool mode's registered
  effect name (`ripple_pool`, in `rgb_matrix_user.inc` -- this exact string is what
  `RGB_MATRIX_CUSTOM_ripple_pool` derives from, used throughout `custom_rgb.c`'s idle dispatcher
  and `rgb_control.c`'s `target_for_slot()`) collided with the pool's own state array, originally
  also named `ripple_pool` in `custom_rgb.c`/`.h`. Renamed the array to `ripple_wave_pool`; the
  effect name (and therefore the enum constant) stayed `ripple_pool`/`RGB_MATRIX_CUSTOM_ripple_pool`
  throughout. Worth double-checking any *new* mode/pool naming against this same trap before
  assuming a name is free.
- **Equalizer's band count/hue-spread constants** (`EQ_NUM_BANDS`, `EQ_BAND_HUE_SPREAD`,
  `EQ_IDLE_*`, `EQ_HIT_*` -- rgb_effects.c) are, like several tuning constants elsewhere in this
  project, reasoned starting points rather than hardware-verified-correct final values (this
  session has no way to see the actual board). Comet Trail's speed/gap/radius constants likewise.
  Worth a closer look if either reads too fast/slow/cramped on real hardware.
- **ISO parity remains best-effort, not independently re-verified**, same caveat every earlier pass
  already carries for Row Wave/Darkening/Fairy Orb -- this pass's three new modes and the Fairy Orb
  changes are written the same geometry-generic way (against `g_led_config`/`RGB_MATRIX_LED_COUNT`,
  no hardcoded ANSI LED indices), so the same reasoning for why they should carry over correctly
  applies, but "should" is doing the same work it always has in this document. The person's own
  stated hardware is the ANSI board.
- Full per-file/per-symbol detail for everything up through the third pass is unchanged below --
  this section only covers what's new this round.

---

## THIRD PASS (2026-09) — two more real bugs, found from hardware reports

- **Fn+M could strand the keyboard on Mac mode with no way back.** Root cause: the previous
  pass's Fn+RAlt relocation (Options-layer entry) gated its *press* side on
  `keycode == FN_OPTIONS_LYR`. Layer 4 (Mac)'s own row5 remaps RAlt to `KC_RGUI` (part of its
  Alt/Cmd swap) -- and since layer resolution checks the highest-numbered active layer first,
  holding Fn+RAlt while Mac mode was active resolved to `KC_RGUI`, never `FN_OPTIONS_LYR`. The
  Options layer -- and therefore its F1 "back to Base" key, the only documented way back --
  became completely unreachable the moment Mac mode was entered. `layer_state`/`TO()` don't
  persist across a power cycle, so unplug-and-replug was the immediate recovery. Fixed properly
  in `keymap.c`: the press-side check now tests `IS_LAYER_ON(1)` directly instead of the resolved
  keycode, the same way the release-side check already had to for the identical reason. Added a
  second, independent safety net alongside it: Fn+M (`RGBCTL_MAC_TOGGLE`) now toggles Mac mode
  on/off from the same key instead of only ever turning it on, so there's a direct way back that
  doesn't depend on Fn+RAlt/the Options layer chain at all.
- **Encoder push-button firing in spurious bursts, including while typing.** Reported as
  intermittent, bursty repeated presses/wave-animation triggers unrelated to any of this
  project's own trigger logic (every call site already only ever fires on a genuine
  `record->event.pressed`, re-verified). Consistent with mechanical contact bounce/chatter on that
  specific switch (it's a normal matrix key, row 0 col 14) rather than a software cause. Added
  `glow_encoder_press_debounce_ok()` (custom_rgb.c): rejects a press within 150ms of the last
  accepted one (no person can deliberately repeat a single press faster than that), gating both
  the visual ring and the real Play/Pause HID send at every encoder-press call site. Deliberately
  *not* applied to CW/CCW rotation, which needs to generate many events in quick succession for
  legitimate fast scrolling -- if spurious *rotation* (not just presses) persists after this, it's
  more likely a genuine hardware issue (wear, a marginal connection) that firmware can't safely
  mask without breaking real fast scrolling.

---

# R75 QMK Firmware — Technical Handoff

## UPDATE ROUND (2026-09, second pass) — read this section first

This second pass fixed bugs found on real hardware after the first 54-section pass, and
implemented a further 46-section spec update (Delete direction, individual wave colors, Shift
hold-duration, idle-timer accuracy, a from-scratch Layer 2 color-selection redesign, holdable
adjustment keys, Fn+Shift removal, Fn+M, Cleaning Mode's F-row, Fairy Orb smoothness, and more).
Verified: clean compile, 0 warnings, both variants; ANSI flash 55,476/131,072 B (42.3%), RAM
8,008/28,672 B (27.9%); ISO flash 51,572/131,072 B (39.3%), RAM 7,972/28,672 B (27.8%).

### Real bugs found and fixed this round

- **Delete's direction was backwards.** Its "consumption radius" grew outward from Delete over
  time -- the same direction Enter's ring moves -- so the bright leading edge visibly travelled
  *away* from Delete, indistinguishable in direction from an explosion, despite the surrounding
  code correctly darkening what was left behind. Fixed by inverting the radius/time relationship
  so it shrinks from the board's edge down to zero, making the boundary genuinely sweep inward
  toward Delete (`rgb_effects.c`'s `overlay_delete_instance()`; the matching erase call in
  `tick_effect_pools()` needed a new `glow_pool_clear_outside_radius()`, custom_rgb.c/.h, since the
  "already swept" region is now outside the shrinking radius instead of inside a growing one).
- **The idle-animation timer only updated while reactive_energy/Darkening were actively
  rendering.** Since that tracking lived entirely inside `glow_pool_sync()` -- only ever called by
  those two effects -- switching to Row Wave, Fairy Orb, a stock effect, or anything else and
  typing normally there left the "last real input" timestamp frozen. Switching back to
  reactive_energy afterward could then see a long-stale timestamp and fire an idle event almost
  immediately, looking exactly like "idle events appearing while the user is actively typing" one
  mode-switch removed from the actual typing. Fixed with a new `glow_track_real_input()` called
  unconditionally from `matrix_scan_user()` every scan (both ANSI and ISO), independent of which
  RGB mode is active; `glow_pool_sync()`/`row_wave_pool_sync()` now call it too instead of
  duplicating the scan inline. The encoder (which never touched this timer at all, since it's
  triggered directly rather than through the core hit-tracker real keys use) now explicitly pulses
  it via `glow_note_encoder_input()`.
- **Shift's glow was a fixed ~2s timed fade, not held-duration-aware.** It looked identical to any
  other key's glow regardless of how long Shift was actually held. Fixed with a live query
  (`glow_shift_is_held()`) checked first in the render path (`overlay_shift_held()`, rendered both
  in its usual spot and again after Enter/Backspace/Delete's hard takeovers, so a wave passing
  through can't visually cancel it while still held); `glow_trigger_shift_release()` (called from
  keymap.c on Shift's actual release, which still processes completely normally as a real
  modifier) starts a fresh ordinary decay for the brief fade-out tail once it's let go.
- **`row_wave_pool` had no layer-transition staleness protection.** `glow_pool` already had this
  (falling-edge clear when Layer 2/3/5 stops suppressing visuals), but Row Wave's own pool -- fed
  from the same raw hit-tracker regardless of layer -- didn't, so it could accumulate presses made
  while adjusting RGB Settings and suddenly animate them once that layer closed. Same fix,
  mirrored: `row_wave_pool_check_staleness()`.
- **Fairy Orb's movement bug had a concrete cause, not just "feels off."** It stepped x and y by
  the *same fixed magnitude* every frame regardless of the actual dx:dy ratio -- only a true
  diagonal when |dx|==|dy|, a bent/staircase path otherwise -- and, on arrival, snapped straight
  to a brand new target's full speed with zero transition. Replaced with easing (`pos += (target -
  pos) >> shift`, naturally decelerating as it approaches) capped at a cruising speed with both
  axes scaled down *together* by the same ratio when capped, so the capped path's direction still
  matches the true direction to the target rather than bending toward whichever axis was smaller.
- **Cleaning Mode never wired up F-row reactivity at all.** `cleaning_mode_visual_passthrough()`
  had no case for F1-F12, so the only way an F-key ever visibly reacted during Cleaning Mode was
  as a side effect of an Escape wave happening to sweep across it -- exactly the wrong mechanism,
  per the report. Added the same `glow_trigger_fkey_group()` call real (non-Cleaning-Mode) F-key
  presses already use.
- **Fn-help blanked the whole board before drawing, indistinguishable from actually pausing the
  active effect.** Holding bare `Fn` (before Layer 2/3/5) cleared every LED to black first, the
  same as those three layers still correctly do. The active effect's render function was never
  actually paused -- QMK calls it every frame regardless -- but hiding its output for as long as
  Fn was held, then revealing however far it had silently continued the moment Fn let go, reads
  exactly like "the animation reset", which is what repeated reports (including against a stock
  random-pixel-style effect, where no code-level reset mechanism could be found even after two
  separate close readings) kept describing. Fixed by making Fn-help-alone a pure additive overlay
  -- no blanking -- while Layer 2/3/5 keep blanking, since their own explicit "unrelated key ->
  OFF" requirement is different from and doesn't apply to bare Fn-help.

### Investigated, no code defect found (documented rather than silently dropped)

Row Wave "not responding to its configured color" was re-traced end-to-end twice this round (once
before, once after the slot-system redesign below) without finding a mechanism that would produce
that symptom -- `target_for_slot()` correctly resolves slot 1 to `RGBCTL_TARGET_ROWWAVE` only while
Row Wave is the actual active `rgb_matrix_mode`, and the color read/write path is otherwise
identical to every other target's. The most plausible explanation that *does* fit the report:
slot 1 means a different target depending on the active mode (Base everywhere except Row Wave and
reactive_energy), so adjusting "slot 1" while not actually on Row Wave changes a different color
entirely, and the change is real but invisible until you switch to Row Wave and look. The manual
now calls this out explicitly (§11) as the most likely explanation. If it persists after this
build with Row Wave definitely active first, it needs a fresh look with that specifically ruled
out, since static reading hasn't turned up a cause.

### Layer 2 color-selection system: redesigned again, not just patched

Section 6/20/24 of the update spec asked for a different visual language than the first pass
built: no confirmation flash on selection (removed entirely), the *selected* slot/target shows its
own real color, every other available one shows plain white (not a dimmed version of its own
color). Implemented by rewriting `render_rgbctl_layer()`'s slot loop and adding the same
selected/white treatment to two new dedicated-key targets:

- **Caps Lock** (sec 7) now selects `RGBCTL_TARGET_CAPS` for editing when pressed, and toggles its
  on/off state instead if pressed again while already selected -- both behaviors on the one
  physical key, disambiguated by current selection state (`rgbctl_select_caps_target()`).
- **`~`, Left Alt, and `Enter`** (sec 25/27/32) all select `RGBCTL_TARGET_FNVIZ`. Three different
  sections of the update spec each named a different single key for this without saying the
  others no longer apply -- treated as three redundant entry points to the same action (the same
  pattern this project already uses for Val/Speed's arrow-plus-,/. redundancy) rather than
  picking one and guessing wrong about which two to drop.

Both moved *off* the number row entirely (previously slots 7/8 in the first pass), since both
indicators render in every RGB mode via the mode-independent `indicators.c` overlay, unlike Enter/
Backspace/Delete/Space/Encoder/Row Wave which are genuinely mode-specific -- tying their color
access to "is reactive_energy active" never made much sense and was flagged directly ("the current
implementation is confusing and visually incorrect", sec 6). reactive_energy's own numbered slots
shrank from 9 to 6 as a result (Base/Enter/Backspace/Delete/Space/Encoder only).

`current_slot` (rgb_control.c) now doubles as a general "current selection" value: 1-6 for the
numbered slots (mode-dependent, via `target_for_slot()`), or one of two sentinels (`SLOT_CAPS`=11,
`SLOT_FNVIZ`=12) for the two dedicated-key targets, resolved in `rgbctl_current_target()`. One
variable rather than two, so there's always exactly one active selection to reason about.

### Fn+Shift removed; Options layer moved to Fn+RAlt, not deleted

Fn+Shift's only real binding was RShift's role as the Options layer's entry key (LShift was
already `KC_NO`). Removing "Fn+Shift" per sec 8 without also removing access to the Options
layer's other 8 functions (reset, EEPROM clear, SOCD/NKRO/OpenRGB/SignalRGB toggles, layer
switching) -- none of which the spec update mentions removing -- would have been a large,
unstated side effect. Relocated the entry key to Right Alt instead (free on Layer 1, not used for
anything else Fn-held): `FN_OPTIONS_LYR`'s physical-position tracking in `process_record_user`
moved from `row==4,col==11` to `row==5,col==4`, and the Layer 1 `LAYOUT()` table's RShift/RAlt
cells swapped accordingly. Fn+M (sec 10) is a separate, new binding (`TO(4)`, same mechanism the
Options layer's own F2 already uses) -- not a relocation of Shift's old role.

### Holdable adjustment keys (sec 23)

None of hue/sat/val/speed's keycodes are ever sent as real HID (`return false`), so OS-level
key-repeat never applied to them -- holding one previously fired the adjustment exactly once, same
as a tap. Added a small from-scratch repeat engine in `rgb_control.c`: `rgbctl_repeat_key_event()`
fires immediately on press (unchanged tap feel), then repeats every 70ms after an initial 380ms
delay for as long as the key stays down, polled from the existing `rgbctl_task()`. Each of the 8
directions (hue/sat/val/speed × up/down) got a tiny no-argument wrapper function so a plain
`void(*)(void)` can represent "which one is currently repeating." Only one repeat stream is active
at a time -- holding a second adjustment key while the first is still down takes over the repeat
and releasing the second stops it, even if the first key is technically still held. Documented
rather than solved with a more complex per-key tracker, since holding two different adjustment
keys at once is not a realistic single-handed gesture.

### Everything else from the update spec, briefly

- Sec 3 (Enter/Backspace/Delete progressive erase excluding Caps/Esc/F-row): already correct from
  the first pass, re-verified unaffected by this round's changes (the exclusion lives in
  `is_glow_pool_excluded_led()`, which none of this round's edits touched).
- Sec 9 (Fn-help correctness re-audit): `\`, Enter, and RAlt were already added in the first pass;
  this round added `M` (now that `Fn`+`M` is a real function, sec 10) to the same list.
- Sec 19 (remove Windows-combination hints): identified as the Ctrl/Cmd+letter shortcut-feedback
  feature (`rgbctl_shortcut_feedback()`) -- removed entirely, including its now-unused
  `RGBCTL_TARGET_SHORTCUT` EEPROM slot (9 stored targets -> 8; `EECONFIG_KB_DATA_SIZE` 53 -> 50).
- Sec 21/22 (`,`/`.` -- the unshifted/shifted `<`/`>` keys -- saturation instead of duplicate
  brightness) and sec 28 (remove K/L): K/L's old saturation binding moved to `,`/`.`, K/L
  themselves are now plain `KC_NO`.
- Sec 24/29 (brightness/saturation/speed indicators reflect actual state): Up/Down, `,`/`.`, and
  Left/Right on Layer 2 now scale their own displayed intensity from the real current value
  instead of showing a fixed color regardless of state.
- Sec 30 (profile-clear delay): `RESET_HOLD_THRESHOLD_MS` 2000 -> 5000.
- Sec 31 (remove `/` backlight-off): `/`'s Layer 2 binding is now `KC_NO`; Space's toggle,
  untouched, remains the only way to turn RGB on/off from this layer.

### ISO note for this round

Shared `features/*.c` fixes above (Delete direction, idle-timer accuracy, Shift hold, row_wave
staleness, Fairy Orb smoothness, EEPROM shrink) all apply to ISO automatically. Added a minimal
`matrix_scan_user()` to ISO's own keymap.c calling just `glow_track_real_input()`, since ISO had no
`matrix_scan_user()` at all and would otherwise have kept the idle-timer bug even after this fix
landed in the shared files. None of this round's *keymap-level* changes (Fn+Shift removal, Fn+M,
RAlt relocation, the Layer 2 redesign, Cleaning Mode's F-row fix) touch ISO's keymap.c -- it
remains the separate, older design documented in the previous round's section below, and now has
one more specific reason not to trust at face value: `indicators.c`'s Fn-help LED list (`\`,
Enter, RAlt, M) reflects *ANSI's* Layer 1 bindings specifically. ISO's own Layer 1 is a
substantially different, older design (media/browser keys on F1-F12 rather than only F9-F12, stock
`QK_RGB_MATRIX_*` keycodes bound directly rather than this project's `RGBCTL_*` system) -- so
Fn-help's highlighted keys are very likely wrong for what ISO's Layer 1 actually does. This isn't
new this round (the shared Fn-help function was already ANSI-specific before these edits); it's
now confirmed and written down rather than assumed compatible.

---

# R75 QMK Firmware — Technical Handoff

This pass implemented the "Custom RGB Animation, Layers, Controls & Bug-Fix Specification"
(54 sections, received 2026-09) on top of the previously-verified `r75_final` continuation-pass
build. This document assumes you have that spec and doesn't repeat its contents — it explains
what the codebase actually does now, why specific implementation choices were made, and where to
look for anything not covered here. Section numbers below are this document's own, not the spec's.

## 0. tl;dr for picking this back up

- Primary target: `r75/ansi:via`. Builds clean (0 warnings, 0 errors), byte-for-byte reproducible.
- `keyboards/r75/` in this archive is a drop-in replacement for the same path in a fresh
  `qmk_firmware` checkout at the pinned commit below — see §17 for exact setup.
- ISO (`r75/iso:via`) also builds clean and inherits every RGB-mode/behavior change that lives in
  `features/*.c` (shared between both variants), but its own `keymap.c` was **not** touched this
  pass and is roughly two continuation-passes behind ANSI's — see §16.
- Everything in this pass was verified by actually building this exact tree with the pinned
  toolchain, not assumed from reading the source. Where something couldn't be verified this way
  (mainly: how anything actually looks/feels on real LEDs), that's called out explicitly rather
  than asserted.

## 1. Source layout

```
config.h                        Shared: MCU/board config, RGB Matrix effect list, EEPROM size
halconf.h, mcuconf.h            ChibiOS HAL config (unchanged this pass)
features/                       Shared between ansi/ and iso/ -- see §3 for what's in each file
  custom_rgb.h / .c             Event pools, state, and the plumbing that feeds them from the
                                 core's g_last_hit_tracker -- no rendering, no keycodes
  rgb_effects.h / .c            All custom-effect rendering (reactive_energy + the 3 new modes)
  rgb_control.h / .c            Fn+\/Fn+Enter hold state machines, color-slot system, profile
                                 store interface, the destructive-reset gesture, parameter bars
  rgb_profiles.h / .c           EEPROM struct shapes and raw read/write (rgb_control.c owns the
                                 in-RAM working copy and all the logic around it)
  indicators.c / indicators.h   rgb_matrix_indicators_advanced_user() -- the one hook that draws
                                 every layer/mode indicator, Cleaning Mode's Esc-red, Fn-help, etc.
  indicator_queue.c / .h        Timed blink/flash primitive used for confirmation cues
  tap_hold.c / .h               Tap-dance reset/clear helpers (Options layer A/Z triple-tap) --
                                 untouched this pass
  socd_cleaner.c / .h           SOCD ("snap tap") cleaner -- untouched this pass
  defines.h                     A few shared small macros
ansi/                           ANSI variant -- primary target
  ansi.c, keyboard.json
  keymaps/via/keymap.c          Layer tables, process_record_user, encoder map -- see §9-§11
  keymaps/via/rgb_matrix_user.inc   Registers the 4 custom RGB Matrix effects
  keymaps/via/rules.mk
iso/                            ISO variant -- see §16 for exactly how far behind it is
  (same shape as ansi/)
```

## 2. Build environment (verified, not assumed)

```
Base:        qmk/qmk_firmware @ 3f26a9232a2696a99fec54239a7e177da53a5105 (2026-08-18)
Submodules:  lib/chibios          @ 6170ddf92d55be54c89b708d1882eea229edeb2c
             lib/chibios-contrib  @ 5a9ad82b6ba4f649cdb800e8c2bbc9be2d3ce767
             lib/printf           @ c2e3b4e10d281e7f0f694d3ecbd9f320977288cc
Core patches: NONE -- everything needed is stock at the commit above (see §13 on why Digital
             Rain was removed rather than patched, to keep it that way)
Toolchain:   arm-none-eabi-gcc 13.2.1 (Ubuntu/gcc-arm-none-eabi package), Python 3.12,
             `qmk` CLI 1.2.0 (PyPI package -- this commit no longer bundles bin/qmk)
```

Setup from scratch:
```
1. git init && git remote add origin https://github.com/qmk/qmk_firmware.git
   git fetch --depth 1 origin 3f26a9232a2696a99fec54239a7e177da53a5105 && git checkout FETCH_HEAD
2. For each of lib/chibios, lib/chibios-contrib, lib/printf: git init that path, add the remote
   above, fetch --depth 1 the pinned commit, checkout FETCH_HEAD. (git submodule update won't
   fetch an arbitrary historical commit without the submodule's own .git already present --
   this per-submodule shallow-fetch is the reliable way to pin exactly these versions.)
3. apt-get install gcc-arm-none-eabi   (13.2.1 on Ubuntu 24.04 at the time of this pass)
   pip install qmk --break-system-packages
   qmk config user.qmk_home=$(pwd)
4. Copy this archive's r75_source/* into keyboards/r75/
5. make r75/ansi:via      (primary target)
   make r75/iso:via       (builds; see §16 for what did/didn't carry over)
```

Verified this pass:
```
r75/ansi:via   Flash: 55,316 / 131,072 B  (42.2%)     RAM: 8,000 / 28,672 B  (27.9%)
r75/iso:via    Flash: 51,268 / 131,072 B  (39.1%)     RAM: 7,972 / 28,672 B  (27.8%)
```
RAM is `.data`+`.bss` read from the linked `.map` file's own section sizes, not
`arm-none-eabi-size`'s bss column -- that tool's bss figure includes the linker script's stack/
heap reservation on this MCU/linker combination and reads far larger than what's actually backed
by real symbols. `r75/ansi:via` grew by 1,924 B flash / 220 B RAM against the pre-this-pass
baseline (53,392 / 7,780) -- see §14 for where that RAM went; it's almost entirely the widened
event pools (sec 6/47 of the spec asked for this explicitly).

Hex reproducibility: identical byte-for-byte across two independent from-scratch rebuilds with
this exact toolchain, both variants. Not guaranteed across different arm-none-eabi-gcc versions
(`LTO_ENABLE=yes`) -- if a rebuild elsewhere produces a different hex with identical source, that
alone isn't a red flag; compare flash/RAM sizes instead, which are toolchain-version-insensitive.

`r75/ansi:default` and `r75/iso:default` still do not build -- pre-existing, unrelated to this or
the previous pass, not investigated (VIA is the only target that's ever mattered here).

## 3. RGB architecture

One QMK `rgb_matrix` install, `RGB_MATRIX_CUSTOM_USER = yes`, 4 custom effects registered in
`rgb_matrix_user.inc` alongside the usual stock ones (config.h's `ENABLE_RGB_MATRIX_*` list):

- **`reactive_energy`** (`rgb_effects.c`) -- the board's main "everyday" effect. Normal reactive
  glow (`custom_rgb.c`'s `glow_pool`), plus Enter/Backspace/Delete/Space/Escape/F-key-group/
  encoder overlays, all layered together in one render function. This is the only mode any of
  those bespoke effects render in -- switch to Solid Color or Row Wave and Enter goes back to
  being a plain Enter key, visually. Also owns the idle animation (§5) and the layer-transition
  staleness fix (§7), both because they're specifically about *this* effect's own event pool.
- **`row_wave`**, **`darkening_glow`**, **`fairy_orb`** (`rgb_effects.c`) -- three new standalone
  modes added this pass (spec sec 23/24/25). Each owns its own `rgb_matrix_mode` slot, not an
  overlay on `reactive_energy`. Unlike the bespoke single-key effects above, all three are written
  entirely against `g_led_config`/`RGB_MATRIX_LED_COUNT` rather than hardcoded LED indices -- see
  §16 on why that matters for ISO.

Every custom effect, and every stock one, gets one universal indicator pass on top:
`rgb_matrix_indicators_advanced_user()` in `indicators.c`. This is the *only* place layer
indicators, Cleaning Mode's red Esc, Fn-help, the parameter bars, and confirmation flashes get
drawn -- it runs after whichever mode rendered the frame, unconditionally, regardless of which
mode that was. That's what makes Row Wave/Darkening/Fairy Orb "just work" with Fn-help/RGB
Settings-layer overlays without any special-casing in their own render functions: they don't need
to know or care about layers at all, `indicators.c` handles it for every mode uniformly.

## 4. Event pools (`custom_rgb.c`)

Every reactive effect is "insert a timestamped event into a small ring-buffer pool, render by
summing each pool's contribution every frame, let a slot free itself once its own lifetime is up."
Sizes, all widened this pass (spec sec 6/47 -- see §14 for the RAM cost):

| Pool | Size | Feeds |
|---|---|---|
| `glow_pool` | 32 | Normal/Tab/Modifier reactive glow, Darkening, the idle animation |
| `enter_pool` / `backspace_pool` / `delete_pool` / `space_pool` | 8 each | Their own key |
| `escseq_pool` | 8 | Escape -- was a single re-triggering instance before this pass, see §8 |
| `encwave_pool` | 8 | Encoder press/CW/CCW |
| `row_wave_pool` | 8 | Row Wave mode |

`glow_pool` and `row_wave_pool` are each fed by their own `*_pool_sync()`, which scans the core's
`g_last_hit_tracker` (an 8-entry *per-frame* snapshot QMK itself maintains, independent of
`process_record_user`'s return value -- this is what lets reactive glow keep working even while
Cleaning Mode blocks everything else in `process_record_user`) and de-duplicates against events
already in that specific pool. `glow_pool_sync()` additionally does three more things every frame,
all folded in here because they all need the exact same "is this a genuinely new real hit"
information that scan already produces:

- **Shift LED sync** (spec sec 9): when either Shift's LED shows up as a new hit, adds a matching
  event for the other Shift LED at the same `start_time`, so both fade in lockstep.
- **Idle animation** (§5).
- **Layer-transition staleness** (§7).

## 5. Idle animation (spec sec 2)

No idle-wave or idle-event code existed anywhere in the source this pass started from -- checked
by grepping the whole tree, not assumed. Implemented fresh, in `custom_rgb.c`, to the end state
the spec describes: after `GLOW_IDLE_THRESHOLD_MS` (12s) with no real key hit, occasionally insert
one synthetic event into `glow_pool` at a random non-excluded LED, tagged exactly the way a real
press at that LED would be (`classify_led()` -- so an idle event landing on, say, a modifier key
gets the modifier's own radius/peak-scale, not the normal-key one). Spacing between events is
randomized (0.9-2.6s) once idle starts, rather than a fixed metronome. Purely additive to
`glow_pool` -- never calls `register_code`/`tap_code`/anything HID-related, so it cannot generate
real input regardless of what's selected as the active window/application. Any real hit resets
the idle clock *and* cancels whatever idle event was scheduled next, before that event is
considered eligible to fire -- real input strictly always wins, by construction, not by priority
ordering that could race.

## 6. Reactive-glow radius and the Shift/Caps "fixed at creation" fix (spec sec 3/9)

`GLOW_RADIUS_NORMAL_BASE`/`_ENLARGED` reduced from the previous pass's 24/34 to 18/26
(`rgb_effects.c`). At 18, a same-row/same-column neighbor (16 or ~13 units away) still gets a dim
touch; a diagonal neighbor (~20.6 units away) no longer reaches inside the radius at all -- a real,
visible tightening, not just a number change, while staying clearly visible rather than shrinking
to nothing.

**Bug found and fixed while implementing this**: the pre-existing code decided narrow-vs-enlarged
radius by calling `glow_wide_radius_active()` (checks live Shift/Caps state) from inside the
per-frame render function itself -- meaning an event's radius was being *re-decided every frame*
from current keyboard state, not fixed at the moment it was created. The spec's own sec 9 walks
through exactly this scenario and explicitly prohibits it ("the already-existing animation must
NOT suddenly become larger"). Fixed by adding a fourth `glow_kind_t` value,
`GLOW_KIND_NORMAL_WIDE`, decided once in `classify_led()` at insertion time
(`glow_wide_radius_active()` is now called from exactly one place, not from the render path at
all) -- see `custom_rgb.h`'s comment on that enum value for the full reasoning. This is the kind
of bug that's easy to miss from reading the spec's high-level description alone; it only became
visible from tracing exactly when the relevant function got called.

## 7. Layer-transition staleness (spec sec 19/45)

`glow_pool_check_staleness()` (`custom_rgb.c`, called from `glow_pool_sync()`) tracks whether any
visually-suppressing context (Fn held, RGB Settings layer, Options layer, Num layer) was active
*last* frame vs *this* one, and clears `glow_pool` on the falling edge. Real key hits still
populate `g_last_hit_tracker` at the raw matrix-scan level no matter what layer is active or what
`process_record_user` returns, so without this, any keys pressed while e.g. adjusting RGB Settings
layer controls would sit in `glow_pool` invisible (blanked by the layer's own indicator overlay)
and then suddenly "light up" the instant that overlay stops drawing -- looking exactly like those
old presses "just happened". Deliberately scoped to `glow_pool` only, not the bespoke pools
(Enter/Backspace/Delete/Space/Escape/encoder): those can only ever be triggered by their own exact
keycode resolving on the *current* layer, and none of Layer 2/3/5's own tables bind any of those
keycodes at those positions, so they physically cannot accumulate stale events this way to begin
with -- this is the same "persistent animation state vs. stale input state" distinction spec sec
45 draws, and the fix only needed to touch the side of that distinction that actually has the bug.

## 8. Escape: single instance → pool (spec sec 4)

`escseq_pool` (8 slots) replaced what was previously a single re-triggering `special_effect_t`.
`overlay_escape_sequence_all()` (`rgb_effects.c`) takes the *brightest* currently-active instance
at each F-row LED across the whole pool, rather than summing them additively -- since every
instance shares the same cascade shape/color and only differs in start time, "brightest wins"
reads as "the newest wave is what's showing right now" without any risk of blowing out toward
white where two happen to overlap. `tick_effect_pools()` retires whichever pool slots have finished
their full cascade-and-fade independently.

## 9. Encoder (spec sec 5/5.1/21)

Origin moved from `(224, 13)` (LED 7/Home's own position, one row too low) to `(224, 0)` --
extrapolated from row 0's y-value (same row as Esc/F-row/Delete) at the board's rightmost column,
matching the encoder's real matrix position `[0,14]` (the one position in that row with no LED of
its own -- confirmed against `keyboard.json`'s `rgb_matrix.layout`, not assumed). Ring thickness
30 units (~2 LED-widths, was ~1); total lifetime 260ms (was 180ms, brought in line with
Enter/Delete's pacing -- spec sec 5 flagged the previous version as too fast).

**Bug found and fixed**: sec 5.1 reported CW's animation as "slightly incomplete" next to CCW's.
The previous implementation stretched dx/dy asymmetrically per rotation direction to give CW/CCW a
directional lean. Because the encoder's origin sits at the board's `x`-*edge* (`ENCWAVE_ORIGIN_X`
is the board's own max x), every LED's dx is `<= 0` -- so the stretch's "favors positive direction"
branch resolved the *same way* for every single LED on the board, not just near some boundary
case, which meant CW's ring needed a uniformly larger radius than CCW's to reach the same LEDs.
Fixed by removing the directional stretch entirely: press/CW/CCW now render an identical, plain
radial ring from the same origin -- which also directly satisfies sec 5.1's actual requirement
("the same complete animation behavior... for encoder press, clockwise rotation, counter-clockwise
rotation") more literally than a fixed-but-still-asymmetric version would have.

Multiple concurrent encoder waves: already pool-based before this pass: benefited from the size
bump (4 → 8), not a structural change.

Fn+Esc (Cleaning Mode) previously silently disabled encoder visual feedback entirely, because
`cleaning_mode_visual_passthrough()` (keymap.c) never had a case for it — process_record_user
returns `false` unconditionally for every key while Cleaning Mode is active except the five
`glow_trigger_*` calls that function makes, and the encoder wasn't one of them. Fixed by adding
the same three checks (`KC_VOLU`/`KC_VOLD` + `IS_ENCODEREVENT`, and the real matrix position
`[0,14]` for the push-button) the main switch statement already uses for real input, duplicated
into that function specifically because its whole contract is "only ever calls a glow_trigger_*
function, nothing else" (see that function's own comment).

## 10. Row Wave / Darkening / Fairy Orb (spec sec 23/24/25)

All three in `rgb_effects.c`, registered in `rgb_matrix_user.inc`. Design choices worth knowing:

- **Row Wave** groups LEDs by *actual* `g_led_config` y-coordinate at runtime, not a matrix-row
  index -- there is no hardcoded row→LED table anywhere in this effect. This board's 6 physical
  rows have 9-15 LEDs each with irregular x-spacing (stepped bottom row, ANSI gap), so a
  matrix-row assumption would have been visibly wrong on exactly those keys, which is the failure
  mode spec sec 46 warns about generally.
- **Darkening** reuses `glow_pool`/`glow_event_contribution` directly rather than a second,
  parallel event tracker -- same spatial/temporal falloff shape as normal glow, just subtracted
  from full brightness instead of added to zero. One accepted, inherited limitation: since
  `glow_pool_sync()` never inserts events for the protected F-row/Esc/Delete strip or for
  Caps/Backspace/Enter/Space's own LEDs (by design, for `reactive_energy`'s sake), those specific
  keys don't darken when pressed in Darkening mode -- they stay at resting brightness. Building a
  second key-hit tracker just to lift that one limitation for a secondary mode wasn't judged worth
  the added state/complexity; flagging it here in case that judgment call is worth revisiting.
- **Fairy Orb**'s position is fixed-point (`x64`, plain `int16_t`, no floating point -- this MCU
  has no FPU) in `custom_rgb.c`'s `orb_tick()`. Chase-the-last-key detection is self-contained
  (compares the newest `g_last_hit_tracker` tick against one remembered value) rather than sharing
  `glow_pool`'s own hit-tracking, since Fairy Orb needs to work as a fully standalone mode whether
  or not `reactive_energy`'s pool is even being synced.
- Neither Darkening nor Fairy Orb has its own configurable color (`rgb_control.c`'s
  `target_for_slot()` maps their one slot to `RGBCTL_TARGET_BASE`) -- Darkening literally darkens
  the live base color, and the orb is a dot of it. Row Wave *does* get its own target
  (`RGBCTL_TARGET_ROWWAVE`), for the same reason Enter/Space/etc do: it has no "resting" state to
  derive a color from (dark at rest, wave is the only thing ever drawn), so tying it to the base
  color the way Darkening/Fairy Orb are wouldn't make the same sense.

## 11. Color targets / slots and independent brightness (spec sec 26/32-33)

`rgbctl_color_target_t` (`rgb_control.h`) is the underlying, fixed 10-entry enum (Base + 9 stored
targets, `RGBCTL_TARGET_ROWWAVE` added this pass). "Slots" (spec sec 32) are a *view* onto that
enum that depends on the active RGB mode -- `rgb_control.c`'s `target_for_slot(slot)`:

- `reactive_energy` active: 9 slots, 1=Base…9=Shortcut feedback (see the table in `rgb_control.h`).
- `row_wave` active: 1 slot, mapped to `RGBCTL_TARGET_ROWWAVE`.
- everything else: 1 slot, mapped to `RGBCTL_TARGET_BASE`.

`current_slot` (a plain 1-based number) is what's actually stored as "currently selected" --
*not* a target directly -- specifically so it survives a `[`/`]` mode switch and keeps meaning
"this mode's primary color" under whatever the new mode's own slot 1 happens to map to, rather
than pointing at a target that might not even be exposed any more. It resets to 1 on every mode
switch and profile load, since the old number may not be valid under the new context at all.

**Brightness became genuinely independent per target this pass** (spec sec 26) -- previously,
`effect_colors[target]` stored a full HSV including `.v`, but every renderer hardcoded `hsv.v =
255` and nothing ever *wrote* a non-255 value there, so the field existed but had no real effect.
Fixed in two halves: `rgbctl_adjust_val()` now writes to `effect_colors[target].v` when the
current target isn't Base (previously it only ever touched the live board-global value); and every
renderer that used to hardcode `.v = 255` either simply stopped overwriting it (letting the value
`rgbctl_get_effect_color()` already returned flow through) or, where a temporal/spatial falloff
needed combining with it, switched to `hsv.v = scale8(falloff, hsv.v)`. EEPROM storage grew from
`[hue,sat]` (2 B/target) to `[hue,sat,val]` (3 B/target) to match -- see §15.

## 12. RGB Settings layer (spec sec 27-42; internally still QMK layer index 2)

The spec's own prose calls this "Layer 3" throughout. This project keeps its existing internal
numbering (`RGBCTL_LAYER` = 2 in `rgb_control.c`) rather than renumbering the whole layer stack to
match, because layer index 3 already means something else here (the pre-existing, unrelated
momentary Options layer, `FN_OPTIONS_LYR` in keymap.c) -- remapping every layer index across
`keymap.c`/`indicators.c`/`ansi.c` to free up "3" purely for a cosmetic label would have been
exactly the unnecessary-rewrite churn the general implementation rules ask to avoid. Both
`USER_MANUAL.md` and this document call it "the RGB Settings layer" throughout instead of a
number, which sidesteps the mismatch for anyone not reading this source directly. If a future pass
*does* want the internal numbering to match the spec's own language, the rename touches: the
`RGBCTL_LAYER`/`layer_on`/`layer_off`/`IS_LAYER_ON(2)` call sites in `rgb_control.c`/`keymap.c`/
`indicators.c`, and would need `FN_OPTIONS_LYR`'s own layer to move to some other free index at
the same time.

**Contents were substantially redesigned**, not incrementally patched -- sec 27 explicitly calls
the previous layout "too confusing" and asks for a from-scratch rebuild of just this layer's
control set, which general rule 1 ("don't rebuild unnecessarily") doesn't apply against, since the
spec itself is what's asking for it here specifically. What changed structurally:

- The old 16-color preset table (`RGBCTL_PRESET_*`, spread across 3 rows) is **removed**. The
  number row is now used entirely for color-slot selection (sec 32) instead, and there wasn't
  room for both systems to coexist on the same physical keys without one meaning two different
  things depending on context -- the spec's sec 32-34 already describes a complete, coherent
  alternative (slot-select + hue/sat adjust) that doesn't need a preset table alongside it.
- The old single "cycle through targets" key (`RGBCTL_TARGET_NEXT`, one key, 9 states) is
  **replaced** by direct number-key selection (`RGBCTL_SLOT_1`..`RGBCTL_SLOT_9`,`_0`) -- see §11.
- Profile load moved from the number row (1-4) to `F5`-`F8`, freeing the whole number row for
  color slots; `Home` gained a new "cycle to next profile" function alongside it (sec 29 names
  both `Home` and F5-F8 as the profile-selection controls; direct-select vs. cycle is the natural
  split between them).
- The old dedicated "Save" key is **removed** -- folded into the pre-existing auto-save-on-
  Layer-release behavior, which already covered the common case and wasn't itself changed. Sec
  27-42's control list doesn't include a Save key, and sec 35/36 ask this layer to show only
  controls that do something *right now*; a manual Save sitting next to a save-that-already-
  happens wasn't earning its place. `rgbctl_save_profile()` still exists (now file-local to
  `rgb_control.c`) and is a one-line keymap.c binding away from being exposed again if wanted.
- `[`/`]` (mode prev/next), the arrow keys (speed/brightness), and `K`/`L` (saturation) were
  already bound exactly where sec 28/30/31/34 ask for them and needed no change -- kept as-is.
  Saturation specifically isn't named anywhere in sec 27-42's control list, but was kept rather
  than dropped: sec 34's "don't add saturation/contrast controls the richer hue system makes
  unnecessary" is conditional, and a hue-only system genuinely cannot reach white/pastel colors on
  its own -- that condition doesn't hold here, so the existing K/L binding stayed.

**Visual language (sec 35-41)** is enforced by `render_rgbctl_layer()` (`indicators.c`) drawing
exactly one of: that control's own live state color, plain white (informational, e.g. `Home`),
red (the destructive-reset gesture only, via a separate function -- see §12.1), or nothing at all.
This was checked mechanically, not just by eye, before considering it done: every non-`KC_NO`
keycode in the Layer 2 `LAYOUT()` table in `keymap.c` was cross-referenced against every LED
`render_rgbctl_layer()` actually touches, confirming a 1:1 correspondence with exactly one
documented, deliberate exception (Esc, handled by the reset-gesture renderer instead, conditional
on an active hold). If you add a new Layer 2 control in a future pass, it's worth re-running that
same cross-check -- a stray `RGB_MATRIX_INDICATOR_SET_COLOR` call for an LED whose key doesn't
actually do anything on this layer is exactly the sec 36 violation the spec calls out by name.

### 12.1 Destructive reset gesture (spec sec 38/42)

No "Esc + profile" combo of any kind existed in the source this pass started from -- the previous
implementation was a single `RGBCTL_PROFILE_RESET` keycode that reset instantly on tap, no delay,
no confirmation. This is a fresh implementation of the end state the spec describes, not a
preserved one. Hold `Esc` together with any of `F5`-`F8` for `RESET_HOLD_THRESHOLD_MS` (2000ms,
`rgb_control.c`) to reset *that* profile; releasing either key early cancels with a quick fade,
no effect on anything. State machine (`esc_key_down`/`profile_key_is_down[4]`/`reset_hold_*`) and
rendering (`rgbctl_render_reset_bar()`) mirror the pre-existing DFU-hold pattern closely, on
purpose, for consistency with the one other "hold to confirm a big action" gesture already in this
codebase. **Deliberately scoped to reset only that one profile's own fields** (effect/hue/sat/val/
speed) -- not the shared per-effect-color block (Enter/Backspace/etc, which is used by all 4
profiles, not owned by any one of them). An earlier draft of this pass's implementation coupled
the two, which would have made "reset Profile 2" also silently wipe out Enter/Backspace colors
Profile 1 might equally depend on -- caught and narrowed before considering this section done,
since that's exactly the kind of surprising scope-creep a safety-delayed destructive action should
avoid, not add.

## 13. Digital Rain removed (spec sec 20)

`ENABLE_RGB_MATRIX_DIGITAL_RAIN` commented out in `config.h`. This is a stock QMK effect
(`quantum/rgb_matrix/animations/digital_rain_anim.h`), not this project's code -- confirmed by
reading it, not assumed -- and it drives its "falling" positions purely from raw
`MATRIX_ROWS`/`MATRIX_COLS` + `rgb_matrix_map_row_column_to_led()`, i.e. it assumes the switch
matrix itself is a clean visual grid rather than using this board's actual physical LED geometry.
That's a plausible root cause for "very broken... falling pixels" on this board's irregular
layout, though it couldn't be conclusively reproduced/traced the way the encoder CW/CCW bug in §9
could (there's no obvious single mechanism to point at the way there was there). Properly fixing
it would mean either patching quantum/core (which this project has deliberately avoided
everywhere -- §2's "core patches: NONE" is worth keeping true) or writing a full bespoke
replacement, and the spec explicitly allows removal when a clean fix isn't proportionate ("a
broken mode is worse than not having the mode"). No duplicate solid-color mode was found this pass
(spec sec 16) -- the previous pass's own handoff already describes finding and removing one; this
pass re-verified and found nothing further to remove.

## 14. Fast typing / effect capacity (spec sec 6/47)

All pool sizes increased -- see the table in §4. `glow_pool` (the one actually reported as
insufficient, at "approximately 10 different keys") went from 24 to 32, above the spec's suggested
20-30 range, since the RAM cost is trivial (4 B/slot). Total RAM cost of every pool widening this
pass combined is the entire +220 B this pass added (§2) -- there was no other meaningful new
static state added beyond the pools themselves and the handful of small state machines (reset-hold
gesture, idle-animation timer, Fairy Orb's position) each of which is on the order of 10-15 bytes.
Per-frame cost of a wider pool is one extra comparison per pool slot per LED -- trivially fast at
any realistic RGB Matrix frame rate on this MCU; nothing here does anything more expensive than
integer add/subtract/compare (no floating point, no sqrt, anywhere in this pass's new code).

## 15. EEPROM (spec sec 26/29/38/42)

`EECONFIG_KB_DATA_SIZE` (`config.h`) grew from 42 to 53 bytes: the profile store (25 B, unchanged)
plus the effect-settings block, now `[hue,sat,val]` × 9 targets + 1 caps-enabled byte = 28 B (was
`[hue,sat]` × 8 + 1 = 17 B). Both numbers are cross-checked at compile time by a `_Static_assert`
in `rgb_profiles.c` against `rgb_profiles.h`'s own computed total -- if you change either struct
shape, a mismatch fails the build immediately rather than corrupting EEPROM silently. As with the
previous pass's own EEPROM growth, **Clear EEPROM after flashing is recommended** -- old on-board
bytes at the previous, narrower offsets will be reinterpreted as different fields under the new
layout (nothing crashes; colors may just look wrong until cleared or manually readjusted).

## 16. ISO status

`r75/iso:via` builds clean, 0 warnings, and picks up every shared `features/*.c` improvement in
this pass automatically: wider pools, tighter/fixed radius behavior, the 3 new RGB modes (fully
geometry-driven, so these specifically should render correctly on ISO's own LED layout without any
further work), the `process_indicator_queue()` fix, the layer-transition staleness fix, the
Digital Rain removal, the EEPROM layout change.

What did **not** carry over, and is worth knowing before assuming otherwise: ISO's own
`ansi/keymaps/via/keymap.c` counterpart is **not simply "unverified" the way the previous pass's
own handoff described it** -- tracing it this pass turned up that it's actually a full generation
behind ANSI's, missing the entire RGBCTL_HOLD/Layer-2/Cleaning-Mode/DFU-hold scaffolding this
pass's spec builds on top of:

- No `matrix_scan_user` polling loop at all, so there is no way to reach the RGB Settings layer
  on ISO -- `RGBCTL_HOLD` doesn't exist in its keymap.
- `process_record_user` never calls any `glow_trigger_*` function -- Enter/Backspace/Delete/
  Space/Escape/F-key-group/encoder-wave never fire on ISO. Only the always-on plain reactive glow
  (fed directly from the core tracker, independent of keymap.c) works there today.
- No Cleaning Mode, no DFU-hold.

This is a correction to, not just a restatement of, the previous handoff's own §8 -- worth
flagging explicitly since believing the old, milder framing would understate the actual gap for
whoever picks this up next. Given the spec this pass implements doesn't mention ISO at all, and
the person's own stated hardware is the ANSI board, porting the entire continuation-pass-plus-this-
pass keymap.c architecture to ISO was treated as out of scope rather than attempted partially --
it's a comparable-sized undertaking to this pass itself, not a small follow-up. ISO's LED geometry
constants also remain unverified against real ISO hardware (unchanged from the previous pass's own
caveat) for the same reason: no ISO unit to check against.

## 17. What's build-verified vs. what needs real hardware

Verified by actually building and inspecting this exact tree:
- Clean compile, 0 warnings, both variants; byte-for-byte reproducible hex.
- Every LED index/coordinate constant this pass touches or relies on, cross-checked against
  `keyboard.json`'s `rgb_matrix.layout` directly (not against comments or prior documentation).
- EEPROM struct sizes and the `_Static_assert` that guards them.
- The Layer 2 keycode-to-LED 1:1 correspondence described in §12.
- RAM/flash deltas.

Not verifiable without the real keyboard, same as every previous pass on this project -- exact
visual timing, thickness, and speed of every wave/ring/fade; whether 18/26-unit radii read as
"tight but visible" rather than "too subtle" in person; whether Fairy Orb's movement speed range
feels smooth at the low/high ends of the configured-speed scale; whether the reset-gesture's 2
second hold feels right; whether the new number-row color-slot layout is actually easier to use
than the old preset table it replaced. All of these were chosen by working through the underlying
geometry/timing numbers deliberately (see each section above for the reasoning, not just the
final constant), the same way the previous pass's own handoff was upfront about doing -- they are
reasoned starting points for hardware tuning, not verified-correct final values.

## 18. Continuing this project

Read `features/custom_rgb.h` first -- every pool/state-machine's contract is documented there in
one place. `rgb_effects.c` is the next read for anything about how something actually looks;
`rgb_control.c` for anything about what a keypress on the RGB Settings layer actually does;
`indicators.c` for anything about what gets drawn where and when. `keymap.c`'s `process_record_user`
is the map from physical keycodes to all of the above -- start there if you're trying to find where
a specific key's behavior lives, not in the feature files directly.

Known things worth a closer look if picked back up:
- §16 (ISO keymap parity) is the largest concrete gap.
- §10's Darkening limitation (protected-strip/Caps/Backspace/Enter/Space keys don't darken).
- `process_indicator_queue()`'s blink shape shows the *complementary* color for its first phase,
  then the requested color for its second, before stopping -- pre-existing logic in
  `indicator_queue.c`, unchanged this pass, now actually reachable for the first time (it was
  previously defined but never called from anywhere -- fixed in `indicators.c`, see the comment
  there). Every confirmation flash this pass added inherits that same two-phase shape. Functions
  fine as "something happened here", just not pixel-perfect; a closer look at that file's blink
  state machine could tighten it if it's worth the risk of touching shared logic several call
  sites now depend on.
- §12's internal-layer-numbering mismatch with the spec's own "Layer 3" language, if a future spec
  round makes the mismatch itself the thing to fix.
