# RK75 Firmware

Custom [QMK](https://qmk.fm) firmware for the **Royal Kludge R75** (ANSI and ISO), built around a rewritten RGB system:
20+ lighting effects, four saveable lighting profiles, a full on-keyboard lighting editor (no software needed),
and a handful of quality-of-life features. VIA, OpenRGB and SignalRGB are supported.

- **MCU:** WB32FQ95, `wb32-dfu` bootloader
- **Keyboard:** 75% layout, rotary encoder, per-key RGB (80 LEDs), NKRO

## Lighting effects

Step through effects with `[` / `]` in the lighting editor (see below). The list runs from simple to complex.

**Stock QMK effects:** Solid Color, Gradient Up/Down, Gradient Left/Right, Breathing, Band Pinwheel, Band Spiral,
Cycle All, Cycle Spiral, Rainbow Beacon, Rainbow Pinwheels, Jellybean Raindrops, Hue Pendulum, Typing Heatmap,
Solid Multisplash.

**Custom effects:**

| Effect | What it does | Reacts to keys? |
|---|---|---|
| **Rainbow Right / Rainbow Left** | A continuous rainbow sweeping across the board in either direction (listed side by side). | No |
| **Orb** | A glowing orb glides across the keyboard like a fish in a lake, leaving a short fading tail. It turns gradually along ever-changing curved paths (never a straight line to a random point) and keeps swimming while you type or sit idle. Color, brightness and speed are adjustable; speed changes only how fast it moves, never how smooth it is. | No |
| **Darkening Glow** | The board stays lit and dims around the keys you press. | Yes |
| **Row Wave** | A band of light ripples outward along the pressed key's row. | Yes |
| **Ripple Pool** | A ring expands from the pressed key across the whole board. | Yes |
| **Comet Trail** | Each press launches a small comet with two fading echoes, flying diagonally away from the center. | Yes |
| **Custom Mode** | The full show. Every key leaves a soft glow, and special keys get their own animation: **Enter** (explosion), **Backspace** (eraser sweep), **Delete** (vacuum), **Space** (rings), **Esc** (sequence), plus Shift sync and F-key groups. Each animation has its own color and speed. | Yes |

Reactive effects can play occasional simulated key presses while you're idle, so the board doesn't go dark.

## On-keyboard lighting editor

Hold **Fn + `\`** to open the lighting layer. Everything is saved automatically when you let go.

| Key | Action |
|---|---|
| `` ` `` | Choose the color used to highlight Fn shortcuts |
| `1`–`6` | Pick which animation slot you're editing (Custom Mode: Base, Enter, Backspace, Delete, Space, Esc) |
| `-` / `=` or **encoder** | Hue down / up |
| `,` / `.` | Saturation down / up |
| `↑` / `↓` | Brightness up / down (constant, linear steps) |
| `←` / `→` | Speed down / up for the selected slot |
| `[` / `]` | Previous / next effect |
| `Space` | RGB on / off |
| `Caps` | Edit the Caps Lock indicator color; press again to turn that indicator off |
| `F5`–`F8` | Select lighting profile 1–4 (the key right of Backspace cycles them) |
| Hold `Esc` + `F5`–`F8` for 5 s | Reset that profile to factory defaults |

Each of the four profiles stores its own effect, colors and speeds. Level bars along the Esc–Delete strip show
saturation, brightness, speed and a hue spectrum while you adjust, then fade out.

## Keys and layers

**Fn shortcuts** (hold Fn; keys that have an Fn function light up in your chosen highlight color):

| Fn + | Action |
|---|---|
| `Esc` | **Cleaning Mode** — locks typing so you can wipe the keys (Esc glows red; animations still play). Press Fn + Esc again to exit. After releasing and pressing Fn again, Esc fades to the Fn color as an exit hint. |
| `F9`–`F12` | Mute, previous, play/pause, next |
| `Del` / `Backspace` | Print Screen / Calculator |
| key right of `Backspace` | Sleep |
| `Left Win` | Windows-key lock |
| `M` | Switch to Mac layer |
| Hold `Right Alt` | Options layer (below) |
| Hold `Enter` for 5 s | Enter bootloader (progress bar fills as you hold) |

**Options layer** (Fn + hold Right Alt):

| Key | Action |
|---|---|
| `F1` / `F2` / `F3` | Back to base / Mac layer / Num layer |
| `T` | Toggle SOCD cleaner (W/S and A/D, last input wins) |
| `N` | Toggle NKRO |
| `O` / `S` | Toggle OpenRGB / SignalRGB mode |
| `Q` ×3 | Reset to bootloader |
| `Z` ×3 | Clear saved settings |

**Rotary encoder:** volume (press = play/pause), rewind / fast-forward while Fn is held, hue in the lighting editor.
The Caps Lock LED has its own color and can be switched off in the lighting editor.

## Build and flash

Built against QMK commit `3f26a92` (with submodules). Copy the keyboard folder into the QMK tree and compile:

```bash
cp -r r75_source/r75_source keyboards/r75
make r75/ansi:via        # or r75/iso:via
```

To flash, enter the bootloader (any one of: hold `Esc` while plugging in USB, which also clears saved settings;
hold the reset switch on the PCB while plugging in; `Q` ×3 in the Options layer; Fn + hold `Enter`), then:

```bash
make r75/ansi:via:flash
```

Prebuilt `.hex` files are included for both variants. After a firmware that changes the stored lighting layout,
the board resets its lighting profiles once on first boot.

## Project layout

| File (`features/`) | Role |
|---|---|
| `rgb_control.c` | Lighting editor, profiles, hold gestures |
| `rgb_effects.c` | Effect renderers (Row Wave, Ripple Pool, Comet Trail, Orb, Custom Mode, ...) |
| `orb_motion.c` | Orb's steering model (smooth, noise-driven, speed-independent path) |
| `custom_rgb.c` | Key-press event pools and idle simulation |
| `indicators.c` | Caps Lock, Fn highlight, bars, Cleaning Mode indicator |
| `rgb_profiles.c` | Profile storage in EEPROM |

See [`HANDOFF.md`](HANDOFF.md) for design notes and the full change history.

## Credits

Original R75 QMK port by [snakkarike](https://github.com/snakkarike). Built on [QMK Firmware](https://github.com/qmk/qmk_firmware)
(GPL-2.0-or-later).
