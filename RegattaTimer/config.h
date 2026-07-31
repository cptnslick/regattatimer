#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pin map (ESP32 devkit). Avoid GPIO 6-11 (flash) and 34-39 (input only).
// ---------------------------------------------------------------------------
constexpr uint8_t PIN_LED_5MIN = 32;
constexpr uint8_t PIN_LED_4MIN = 33;
constexpr uint8_t PIN_LED_3MIN = 25;
constexpr uint8_t PIN_LED_2MIN = 26;
constexpr uint8_t PIN_LED_1MIN = 27;

constexpr uint8_t PIN_HORN = 13;   // MOSFET / SSR gate. Needs a 10k pulldown.
constexpr uint8_t PIN_BUTTON = 4;  // Start / reset, to GND, internal pullup.
constexpr uint8_t PIN_RUNSEL = 16; // Run selector, to GND, internal pullup.

// Set false if the LED / horn hardware sinks current instead of sourcing it.
constexpr bool LED_ACTIVE_HIGH = true;
constexpr bool HORN_ACTIVE_HIGH = true;

// Run selector: closed to GND selects two back-to-back runs.
constexpr bool RUNSEL_CLOSED_MEANS_TWO_RUNS = true;

// ---------------------------------------------------------------------------
// Start sequence timing, in seconds from the warning signal of each class.
// ---------------------------------------------------------------------------
constexpr uint32_t SIG_WARNING_SEC = 0;   // 5 minutes
constexpr uint32_t SIG_PREP_SEC = 60;     // 4 minutes
constexpr uint32_t SIG_ONE_MIN_SEC = 240; // 1 minute
constexpr uint32_t SIG_START_SEC = 300;   // 0 minutes

// A second class starts one full sequence behind the first, so its warning
// signal lands on the first class's starting signal.
constexpr uint32_t CLASS_INTERVAL_SEC = 300;

// ---------------------------------------------------------------------------
// Horn shaping
// ---------------------------------------------------------------------------
constexpr uint16_t HORN_SHORT_MS = 1000; // warning, preparatory, start
constexpr uint16_t HORN_LONG_MS = 3000;  // one-minute signal
constexpr uint16_t HORN_GAP_MS = 400;    // silence between repeated blasts
constexpr uint16_t HORN_MAX_ON_MS = 4000; // hard cap, protects the compressor

// ---------------------------------------------------------------------------
// Lamp behaviour
// ---------------------------------------------------------------------------
constexpr uint32_t START_FLASH_MS = 3000;  // all-lamps flash at each start
constexpr uint32_t FINISH_HOLD_MS = 30000; // all-lamps flash after the last start
constexpr uint16_t FLASH_SLOW_MS = 500;    // all-lamps flash half period
constexpr uint16_t FLASH_LAST_MIN_MS = 500;// 1-min lamp blink in the final minute
constexpr uint16_t FLASH_LAST_10S_MS = 125;// 1-min lamp blink in the final 10 s
constexpr uint16_t IDLE_BLINK_MS = 1000;   // run-mode indication while idle

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------
constexpr uint16_t DEBOUNCE_MS = 30;
constexpr uint16_t LONG_PRESS_MS = 1500;   // hold this long for a full reset
// A tap resets a running sequence, so a double tap on the start button would
// otherwise arm and immediately kill it. Taps are ignored for this long after
// arming. Well short of the five minutes before anything can be over early.
constexpr uint16_t RESTART_GUARD_MS = 1000;
