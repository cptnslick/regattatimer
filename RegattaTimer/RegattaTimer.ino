// Regatta start timer for ESP32.
//
// Runs the 5-4-1-0 start sequence: a horn at 5 minutes (warning), 4 minutes
// (preparatory), 1 minute, and 0 minutes (start). A front panel switch selects
// one run or two back-to-back runs; with two runs the second class takes its
// warning signal on the first class's start, so the two sequences overlap and
// the whole thing runs 10 minutes with one blast in the middle covering both.
//
// Everything is non-blocking: the horn, the lamps and the button all run off
// millis() so the signal times never drift behind a delay() somewhere.

#include "config.h"

// ---------------------------------------------------------------------------
// Signal schedule
// ---------------------------------------------------------------------------
struct Signal {
  uint32_t atSec;
  uint8_t blasts;
  bool longBlast;
  bool fired;
};

constexpr uint8_t MAX_SIGNALS = 8;
Signal gSignals[MAX_SIGNALS];
uint8_t gSignalCount = 0;

// Class start times, in seconds from the beginning of the sequence.
constexpr uint8_t MAX_CLASSES = 2;
uint32_t gClassStartSec[MAX_CLASSES];
uint8_t gClassCount = 0;

// ---------------------------------------------------------------------------
// Horn queue
// ---------------------------------------------------------------------------
struct Blast {
  uint16_t onMs;
};

constexpr uint8_t HORN_QUEUE_LEN = 8;
Blast gHornQueue[HORN_QUEUE_LEN];
uint8_t gHornHead = 0;
uint8_t gHornTail = 0;
bool gHornSounding = false;
uint32_t gHornPhaseStartMs = 0;
uint16_t gHornPhaseLenMs = 0;

// ---------------------------------------------------------------------------
// Run state
// ---------------------------------------------------------------------------
enum class State : uint8_t { Idle, Running, Finished };
State gState = State::Idle;

uint32_t gSequenceStartMs = 0;
uint8_t gRunsLatched = 1;   // number of classes this sequence was armed for
int32_t gLastReportedSec = -1;

const uint8_t kLedPins[5] = {PIN_LED_1MIN, PIN_LED_2MIN, PIN_LED_3MIN,
                             PIN_LED_4MIN, PIN_LED_5MIN};

// ---------------------------------------------------------------------------
// Low level output helpers
// ---------------------------------------------------------------------------
void writeLed(uint8_t pin, bool on) {
  digitalWrite(pin, (on == LED_ACTIVE_HIGH) ? HIGH : LOW);
}

void writeHorn(bool on) {
  digitalWrite(PIN_HORN, (on == HORN_ACTIVE_HIGH) ? HIGH : LOW);
}

// mask bit 0 = 1-minute lamp ... bit 4 = 5-minute lamp
void setLampMask(uint8_t mask) {
  for (uint8_t i = 0; i < 5; i++) {
    writeLed(kLedPins[i], mask & (1u << i));
  }
}

bool flashPhase(uint32_t now, uint16_t halfPeriodMs) {
  return ((now / halfPeriodMs) & 1u) == 0;
}

// ---------------------------------------------------------------------------
// Horn
// ---------------------------------------------------------------------------
void hornEnqueue(uint8_t count, bool longBlast) {
  uint16_t onMs = longBlast ? HORN_LONG_MS : HORN_SHORT_MS;
  if (onMs > HORN_MAX_ON_MS) onMs = HORN_MAX_ON_MS;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t next = (uint8_t)((gHornTail + 1) % HORN_QUEUE_LEN);
    if (next == gHornHead) return; // queue full, drop rather than block
    gHornQueue[gHornTail].onMs = onMs;
    gHornTail = next;
  }
}

void hornClear() {
  bool wasSounding = gHornSounding;
  gHornHead = gHornTail = 0;
  gHornSounding = false;
  writeHorn(false);
  // Cutting a blast short still owes the next one its gap; an already idle
  // horn does not, so arming a sequence does not delay the warning signal.
  gHornPhaseStartMs = millis();
  gHornPhaseLenMs = wasSounding ? HORN_GAP_MS : 0;
}

void hornService(uint32_t now) {
  if (gHornPhaseLenMs != 0) {
    if (now - gHornPhaseStartMs < gHornPhaseLenMs) return;
    // Current phase just expired.
    gHornPhaseLenMs = 0;
    if (gHornSounding) {
      gHornSounding = false;
      writeHorn(false);
      gHornPhaseStartMs = now;
      gHornPhaseLenMs = HORN_GAP_MS; // enforce silence between blasts
      return;
    }
  }

  if (gHornHead == gHornTail) return; // nothing queued

  uint16_t onMs = gHornQueue[gHornHead].onMs;
  gHornHead = (uint8_t)((gHornHead + 1) % HORN_QUEUE_LEN);
  gHornSounding = true;
  writeHorn(true);
  gHornPhaseStartMs = now;
  gHornPhaseLenMs = onMs;
}

// ---------------------------------------------------------------------------
// Schedule construction
// ---------------------------------------------------------------------------
void addSignal(uint32_t atSec, uint8_t blasts, bool longBlast) {
  for (uint8_t i = 0; i < gSignalCount; i++) {
    if (gSignals[i].atSec == atSec) {
      // Two classes share this instant: class 1 starting while class 2 takes
      // its warning. One blast covers both, not a blast per class. Both are
      // one second signals, so the merged blast is short.
      gSignals[i].blasts = 1;
      gSignals[i].longBlast = gSignals[i].longBlast || longBlast;
      return;
    }
  }
  if (gSignalCount >= MAX_SIGNALS) return;
  gSignals[gSignalCount++] = {atSec, blasts, longBlast, false};
}

void buildSchedule(uint8_t runs) {
  gSignalCount = 0;
  gClassCount = 0;
  for (uint8_t c = 0; c < runs && c < MAX_CLASSES; c++) {
    uint32_t base = c * CLASS_INTERVAL_SEC;
    addSignal(base + SIG_WARNING_SEC, 1, false);
    addSignal(base + SIG_PREP_SEC, 1, false);
    addSignal(base + SIG_ONE_MIN_SEC, 1, true);
    addSignal(base + SIG_START_SEC, 1, false);
    gClassStartSec[gClassCount++] = base + SIG_START_SEC;
  }
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------
uint8_t readRunSelector() {
  bool closed = (digitalRead(PIN_RUNSEL) == LOW); // pullup: closed reads low
  bool twoRuns = RUNSEL_CLOSED_MEANS_TWO_RUNS ? closed : !closed;
  return twoRuns ? 2 : 1;
}

struct ButtonEvents {
  bool shortPress;
  bool longPress;
};

ButtonEvents serviceButton(uint32_t now) {
  static bool stableDown = false;
  static bool lastRaw = false;
  static uint32_t lastChangeMs = 0;
  static uint32_t pressStartMs = 0;
  static bool longFired = false;

  ButtonEvents ev{false, false};

  bool raw = (digitalRead(PIN_BUTTON) == LOW);
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChangeMs = now;
  }

  if (now - lastChangeMs >= DEBOUNCE_MS && raw != stableDown) {
    stableDown = raw;
    if (stableDown) {
      pressStartMs = now;
      longFired = false;
    } else if (!longFired) {
      ev.shortPress = true;
    }
  }

  if (stableDown && !longFired && now - pressStartMs >= LONG_PRESS_MS) {
    longFired = true;
    ev.longPress = true;
  }

  return ev;
}

// ---------------------------------------------------------------------------
// Sequence control
// ---------------------------------------------------------------------------
void enterIdle(const char *why) {
  gState = State::Idle;
  hornClear();
  setLampMask(0);
  gLastReportedSec = -1;
  Serial.printf("[timer] idle (%s)\n", why);
}

void startSequence(uint32_t now) {
  gRunsLatched = readRunSelector();
  buildSchedule(gRunsLatched);
  gSequenceStartMs = now;
  gLastReportedSec = -1;
  hornClear();
  gState = State::Running;
  Serial.printf("[timer] sequence armed: %u run(s), %lu s total\n",
                gRunsLatched,
                (unsigned long)gClassStartSec[gClassCount - 1]);
}

// Which class is currently counting down, and how long until it starts.
// Returns false once every class has started.
bool currentClass(uint32_t elapsedSec, uint8_t *classIndex, int32_t *remainSec) {
  for (uint8_t i = 0; i < gClassCount; i++) {
    if (elapsedSec < gClassStartSec[i]) {
      *classIndex = i;
      *remainSec = (int32_t)gClassStartSec[i] - (int32_t)elapsedSec;
      return true;
    }
  }
  return false;
}

void updateLamps(uint32_t now, uint32_t elapsedMs) {
  uint32_t elapsedSec = elapsedMs / 1000;

  // Every class start gets a few seconds of all-lamps flash, including the
  // mid-sequence one where class 1 starts and class 2 picks up the countdown.
  for (uint8_t i = 0; i < gClassCount; i++) {
    uint32_t startMs = gClassStartSec[i] * 1000UL;
    if (elapsedMs >= startMs && elapsedMs < startMs + START_FLASH_MS) {
      setLampMask(flashPhase(now, FLASH_SLOW_MS) ? 0x1F : 0x00);
      return;
    }
  }

  uint8_t classIndex;
  int32_t remainSec;
  if (!currentClass(elapsedSec, &classIndex, &remainSec)) {
    setLampMask(flashPhase(now, FLASH_SLOW_MS) ? 0x1F : 0x00);
    return;
  }

  // Lamp N is lit through the whole of minute N, so 4:59 still shows 5.
  int32_t minutesShown = (remainSec + 59) / 60;
  if (minutesShown < 1) minutesShown = 1;
  if (minutesShown > 5) minutesShown = 5;

  uint8_t mask = (uint8_t)(1u << (minutesShown - 1));

  // In the final minute the 1-minute lamp blinks, and blinks faster inside the
  // last ten seconds.
  if (remainSec <= 10) {
    if (!flashPhase(now, FLASH_LAST_10S_MS)) mask = 0;
  } else if (remainSec <= 60) {
    if (!flashPhase(now, FLASH_LAST_MIN_MS)) mask = 0;
  }

  setLampMask(mask);
}

void reportCountdown(uint32_t elapsedMs) {
  int32_t elapsedSec = (int32_t)(elapsedMs / 1000);
  if (elapsedSec == gLastReportedSec) return;
  gLastReportedSec = elapsedSec;

  uint8_t classIndex;
  int32_t remainSec;
  if (currentClass((uint32_t)elapsedSec, &classIndex, &remainSec)) {
    Serial.printf("[timer] class %u  T-%ld:%02ld\n", classIndex + 1,
                  (long)(remainSec / 60), (long)(remainSec % 60));
  }
}

void runSequence(uint32_t now) {
  uint32_t elapsedMs = now - gSequenceStartMs;

  for (uint8_t i = 0; i < gSignalCount; i++) {
    if (!gSignals[i].fired && elapsedMs >= gSignals[i].atSec * 1000UL) {
      gSignals[i].fired = true;
      hornEnqueue(gSignals[i].blasts, gSignals[i].longBlast);
      Serial.printf("[timer] signal at %lu s: %u blast(s)%s\n",
                    (unsigned long)gSignals[i].atSec, gSignals[i].blasts,
                    gSignals[i].longBlast ? " long" : "");
    }
  }

  updateLamps(now, elapsedMs);
  reportCountdown(elapsedMs);

  uint32_t lastStartMs = gClassStartSec[gClassCount - 1] * 1000UL;
  if (elapsedMs >= lastStartMs + FINISH_HOLD_MS &&
      gHornHead == gHornTail && !gHornSounding) {
    gState = State::Finished;
    setLampMask(0);
    Serial.println("[timer] sequence complete");
  }
}

void showIdle(uint32_t now) {
  // Lamps 1..N show how many runs the selector is set for.
  uint8_t runs = readRunSelector();
  uint8_t mask = (runs >= 2) ? 0x03 : 0x01;
  setLampMask(flashPhase(now, IDLE_BLINK_MS) ? mask : 0);
}

// ---------------------------------------------------------------------------
void setup() {
  // Horn off before anything else, so a boot glitch cannot hold it on.
  pinMode(PIN_HORN, OUTPUT);
  writeHorn(false);

  for (uint8_t i = 0; i < 5; i++) {
    pinMode(kLedPins[i], OUTPUT);
    writeLed(kLedPins[i], false);
  }

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_RUNSEL, INPUT_PULLUP);

  Serial.begin(115200);
  delay(100);
  Serial.println("\n[timer] regatta start timer ready");
  Serial.println("[timer] press start to arm, hold 1.5 s to abort");

  // Lamp check, horn stays silent.
  for (uint8_t i = 0; i < 5; i++) {
    writeLed(kLedPins[i], true);
    delay(120);
  }
  delay(300);
  setLampMask(0);
}

void loop() {
  uint32_t now = millis();

  ButtonEvents btn = serviceButton(now);
  if (btn.longPress) {
    enterIdle("aborted");
  } else if (btn.shortPress) {
    switch (gState) {
      case State::Idle:
        startSequence(now);
        break;
      case State::Finished:
        enterIdle("reset");
        break;
      case State::Running:
        break; // ignore taps mid-sequence, hold to abort
    }
  }

  switch (gState) {
    case State::Idle:
      showIdle(now);
      break;
    case State::Running:
      runSequence(now);
      break;
    case State::Finished:
      setLampMask(0);
      break;
  }

  hornService(now);
}
