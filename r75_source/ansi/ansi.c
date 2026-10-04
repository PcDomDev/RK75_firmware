// Copyright 2026 Saurabh Nakkarike (@snakkarike)
// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H

#define LED_ENABLE_PIN A5

void keyboard_pre_init_kb(void) {
    gpio_set_pin_output(LED_ENABLE_PIN);
    gpio_write_pin_high(LED_ENABLE_PIN);

    // Note: CAPS_LOCK is set in the keyboard json
    //       under "indicators": {"caps_lock": "B0","on_state": 0}

    // setup the Win Lock and Mac LEDs
    // Note: These must be set high to turn them off
    gpio_set_pin_output(LED_WIN_LOCK_PIN);
    gpio_write_pin_high(LED_WIN_LOCK_PIN);

    gpio_set_pin_output(LED_MAC_PIN);
    gpio_write_pin_high(LED_MAC_PIN);

    keyboard_pre_init_user();
}

void suspend_power_down_kb(void) {
    // turn off our RGB LEDs
    gpio_write_pin_low(LED_ENABLE_PIN);

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    // turn on our RGB LEDs
    gpio_write_pin_high(LED_ENABLE_PIN);

    suspend_wakeup_init_user();
}

void housekeeping_task_kb(void) {
    // Bugfix (continuation pass): this used to also write LED_MAC_PIN from no_gui here, but
    // core's housekeeping_task() (quantum/keyboard.c) calls this and housekeeping_task_user()
    // separately, every scan, in that order -- so that write was always immediately overwritten
    // a moment later by keymap.c's housekeeping_task_user(), which (correctly) drives the Mac
    // LED from whether the Mac layer is active, not from the unrelated no_gui/"GUI key locked"
    // toggle. It was fully dead code kept alive only by this file's own comment already saying
    // the opposite of what it did. Win-Lock isn't affected -- both files already agree it should
    // track no_gui.
    if (keymap_config.no_gui) {
        gpio_write_pin_low(LED_WIN_LOCK_PIN); // low means turn on
    } else {
        gpio_write_pin_high(LED_WIN_LOCK_PIN); // high means turn off
    }
}

#if !defined(VIA_ENABLE)
void raw_hid_receive(uint8_t *data, uint8_t length) {
    switch (data[0]) {
        case RAW_HID_CMD:
            via_command_kb(data, length);
            break;
    }
}
#endif
