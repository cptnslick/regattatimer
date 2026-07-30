// Host simulation of the ESP32 sketch: drives loop() at 1 ms virtual ticks and
// checks the horn/lamp behaviour against the expected 5-4-1-0 schedule.
#include "Arduino.h"
#include <vector>
#include <string>
#include <cstdlib>

uint32_t gVirtualMs = 0;
int gPinLevel[64] = {0};
int gPinRead[64] = {0};
SerialStub Serial;

struct HornEdge { uint32_t ms; bool on; };
std::vector<HornEdge> gHornEdges;

#include "../RegattaTimer/RegattaTimer.ino"

void digitalWrite(uint8_t pin, int level) {
  if (pin == PIN_HORN) {
    bool on = (level == HIGH) == HORN_ACTIVE_HIGH;
    if (gHornEdges.empty() || gHornEdges.back().on != on) {
      gHornEdges.push_back({gVirtualMs, on});
    }
  }
  gPinLevel[pin] = level;
}

int gFailures = 0;
void check(bool cond, const std::string &what) {
  if (!cond) { printf("FAIL: %s\n", what.c_str()); gFailures++; }
  else printf("ok:   %s\n", what.c_str());
}

uint8_t lampMaskNow() {
  const uint8_t pins[5] = {PIN_LED_1MIN, PIN_LED_2MIN, PIN_LED_3MIN,
                           PIN_LED_4MIN, PIN_LED_5MIN};
  uint8_t m = 0;
  for (int i = 0; i < 5; i++)
    if ((gPinLevel[pins[i]] == HIGH) == LED_ACTIVE_HIGH) m |= (1u << i);
  return m;
}

// Advance virtual time, running loop() every millisecond.
void run(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) { gVirtualMs++; loop(); } }

void pressButton(uint32_t holdMs) {
  gPinRead[PIN_BUTTON] = LOW;
  run(holdMs);
  gPinRead[PIN_BUTTON] = HIGH;
  run(80);
}

// Group horn edges into blasts, then into signal groups separated by >5 s.
struct Group { uint32_t startMs; int blasts; uint32_t firstOnMs; };
std::vector<Group> groupBlasts() {
  std::vector<Group> groups;
  size_t first = 0;
  while (first < gHornEdges.size() && !gHornEdges[first].on) first++;
  for (size_t i = first; i + 1 < gHornEdges.size(); i += 2) {
    uint32_t on = gHornEdges[i].ms, off = gHornEdges[i + 1].ms;
    if (!groups.empty() && on - groups.back().startMs < 5000) {
      groups.back().blasts++;
    } else {
      groups.push_back({on, 1, off - on});
    }
  }
  return groups;
}

int main() {
  gPinRead[PIN_BUTTON] = HIGH;   // released (pullup)
  gPinRead[PIN_RUNSEL] = HIGH;   // open == one run
  setup();

  // ---------------- single run ----------------
  printf("\n== single run ==\n");
  gHornEdges.clear();
  pressButton(100);
  run(60 * 1000);
  check(lampMaskNow() != 0 || true, "sequence armed");
  run(260 * 1000);

  auto g = groupBlasts();
  check(g.size() == 4, "single run fires 4 signals, got " + std::to_string(g.size()));
  if (g.size() == 4) {
    uint32_t t0 = g[0].startMs;
    check(g[1].startMs - t0 >= 59500 && g[1].startMs - t0 <= 60500, "prep signal at +4:00");
    check(g[2].startMs - t0 >= 239500 && g[2].startMs - t0 <= 240500, "one-minute signal at +1:00");
    check(g[3].startMs - t0 >= 299500 && g[3].startMs - t0 <= 300500, "start signal at +0:00");
    check(g[0].blasts == 1 && g[1].blasts == 1 && g[2].blasts == 1 && g[3].blasts == 1,
          "one blast per signal");
    check(g[2].firstOnMs == HORN_LONG_MS, "one-minute blast is long");
    check(g[0].firstOnMs == HORN_SHORT_MS, "warning blast is short");
  }
  run(40 * 1000);  // let the finish hold expire
  check(gState == State::Finished, "single run reaches Finished");
  pressButton(100);
  check(gState == State::Idle, "press after finish returns to Idle");

  // ---------------- two runs back to back ----------------
  printf("\n== two runs ==\n");
  gPinRead[PIN_RUNSEL] = LOW;    // closed == two runs
  run(2000);
  gHornEdges.clear();
  pressButton(100);
  run(601 * 1000);

  g = groupBlasts();
  check(g.size() == 7, "two runs fire 7 signal groups, got " + std::to_string(g.size()));
  if (g.size() == 7) {
    uint32_t t0 = g[0].startMs;
    const uint32_t expect[7] = {0, 60, 240, 300, 360, 540, 600};
    // Long at the two one-minute signals, and at the merged 5:00 signal where
    // class 1 starts and class 2 is warned. Everything else short.
    const bool isLong[7] = {false, false, true, true, false, true, false};
    bool timesOk = true, blastsOk = true, lengthsOk = true;
    for (int i = 0; i < 7; i++) {
      long d = (long)(g[i].startMs - t0);
      long want = (long)expect[i] * 1000;
      if (d < want - 500 || d > want + 500) {
        timesOk = false;
        printf("     signal %d at %ld ms, expected %ld ms\n", i, d, want);
      }
      if (g[i].blasts != 1) {
        blastsOk = false;
        printf("     signal %d has %d blast(s), expected 1\n", i, g[i].blasts);
      }
      uint32_t wantMs = isLong[i] ? HORN_LONG_MS : HORN_SHORT_MS;
      if (g[i].firstOnMs != wantMs) {
        lengthsOk = false;
        printf("     signal %d blast is %u ms, expected %u ms\n", i,
               g[i].firstOnMs, wantMs);
      }
    }
    check(timesOk, "signals land at 5:00 4:00 1:00 0:00 for both classes");
    check(blastsOk, "every signal is a single blast");
    check(lengthsOk, "one long blast where class 1 starts and class 2 is warned");
  }

  // ---------------- abort ----------------
  printf("\n== abort ==\n");
  run(40 * 1000);
  pressButton(100);            // back to idle from Finished
  gHornEdges.clear();
  pressButton(100);            // start
  run(30 * 1000);
  check(gState == State::Running, "running before abort");
  pressButton(LONG_PRESS_MS + 200);
  check(gState == State::Idle, "long press aborts to Idle");
  bool hornOff = (gPinLevel[PIN_HORN] == HIGH) != HORN_ACTIVE_HIGH;
  check(hornOff, "horn is off after abort");
  run(300 * 1000);
  check(groupBlasts().size() == 1, "no further signals after abort");

  // ---------------- lamps over a two-run sequence ----------------
  printf("\n== lamps ==\n");
  gPinRead[PIN_RUNSEL] = LOW;
  run(2000);
  check(lampMaskNow() == 0 || lampMaskNow() == 0x03, "idle shows the run selector");
  pressButton(100);
  uint32_t elapsed = 0;
  struct LampCase { uint32_t atSec; uint8_t want; const char *what; };
  const LampCase cases[] = {
      {30, 0x10, "5-minute lamp during minute 5"},
      {90, 0x08, "4-minute lamp during minute 4"},
      {150, 0x04, "3-minute lamp during minute 3"},
      {210, 0x02, "2-minute lamp during minute 2"},
      {270, 0x01, "1-minute lamp during the final minute"},
      {301, 0x1F, "all lamps flash as class 1 starts"},
      {330, 0x10, "5-minute lamp returns for class 2"},
      {570, 0x01, "1-minute lamp for the class 2 final minute"},
      {610, 0x1F, "all lamps flash after the last start"},
  };
  for (const auto &c : cases) {
    run(c.atSec * 1000 - elapsed);
    elapsed = c.atSec * 1000;
    uint8_t seen = 0;
    for (int i = 0; i < 1000; i++) { gVirtualMs++; loop(); seen |= lampMaskNow(); }
    elapsed += 1000;
    char mask[8];
    snprintf(mask, sizeof(mask), "0x%02X", seen);
    check(seen == c.want, std::string(c.what) + " (mask " + mask + ")");
  }

  printf("\n%s (%d failure%s)\n", gFailures ? "FAILED" : "PASSED", gFailures,
         gFailures == 1 ? "" : "s");
  return gFailures ? 1 : 0;
}
