# Regatta start timer (ESP32)

An Arduino sketch for an ESP32 that runs the 5-4-1-0 sailing start sequence:
a horn at 5 minutes (warning), 4 minutes (preparatory), 1 minute, and 0 minutes
(start), with a lamp showing the minute you are in.

A front panel switch selects one run or two runs back to back. On two runs the
second class takes its warning signal at the moment the first class starts, the
way the club runs non-spinnaker off the spinnaker start, so the whole sequence
is ten minutes with a single blast in the middle serving both classes.

Every signal is a single blast: one second at the warning, preparatory and
start signals, three seconds at the one-minute signal. The 5:00 signal in a
two run sequence is class 1 starting and class 2 being warned at once, and
sounds as one 1 s blast rather than a blast for each. The 3 and 2 minute
marks are silent, lamps only. Blast lengths are `HORN_SHORT_MS` and
`HORN_LONG_MS` in `config.h`.

## Signals

Single run, elapsed time from pressing start:

| Time | Signal | Horn | Lamp |
|---|---|---|---|
| 0:00 | warning (5 min) | 1 s | 5 |
| 1:00 | preparatory (4 min) | 1 s | 4 |
| 2:00 | — | — | 3 |
| 3:00 | — | — | 2 |
| 4:00 | one minute | 3 s | 1, blinking |
| 5:00 | **start** | 1 s | all flash |

Two runs, elapsed time from pressing start:

| Time | Signal | Horn | Lamp |
|---|---|---|---|
| 0:00 | class 1 warning | 1 s | 5 |
| 1:00 | class 1 preparatory | 1 s | 4 |
| 4:00 | class 1 one minute | 3 s | 1, blinking |
| 5:00 | **class 1 start** + class 2 warning | 1 s | all flash, then 5 |
| 6:00 | class 2 preparatory | 1 s | 4 |
| 9:00 | class 2 one minute | 3 s | 1, blinking |
| 10:00 | **class 2 start** | 1 s | all flash |

The lamp for minute N stays lit through the whole of that minute, so 4:59
remaining still shows the 5 lamp. The 1 lamp blinks through the final minute
and blinks faster inside the last ten seconds. All five lamps flash for three
seconds at each start, and for thirty seconds after the last one.

## Controls

- **Start / reset button** — tap while idle to arm the sequence. Tap after a
  finished sequence to return to idle. Taps during a sequence are ignored, so a
  knock against the panel cannot restart the countdown.
- **Hold the button 1.5 s** — recall: abort, silence the horn, return to idle.
- **Run selector** — read when you arm, so flipping it mid-sequence changes
  nothing. While idle, one lamp lit means the next press runs one class, two
  lamps means two.

## Recall part way through

A recall only re-runs the classes that had not started yet, so a fleet over
early in the second half of a two-class start does not cost you the full ten
minutes again:

| Recall at | Classes not yet started | Next press runs |
|---|---|---|
| two classes, before 5:00 | both | 10 min, both classes |
| two classes, at or after 5:00 | class 2 only | 5 min, one class |
| one class, any time | that class | 5 min |

Recalling before 5:00 means class 1 never started, so both fleets still need a
sequence and the full ten minutes is right. After 5:00 class 1 is racing and
only class 2 needs sending again.

The pending count overrides the selector for that one start, and the idle lamps
show it: one lamp after a second-half recall even with the switch still set to
two. It is spent as soon as you start, so the sequence after that follows the
switch again. To cancel it before then, flip the run selector — the flip is
debounced, so switch bounce cannot throw the count away by accident.

Class 2 always follows class 1, so it never needs a start of its own. If class 1
is the fleet over early — a recall right at the 5:00 mark, while class 2 has
already picked up the countdown — flick the run selector to one and back to two
before pressing start. That clears the pending count, and the full ten minutes
runs again with class 2 following class 1 as usual.

## Wiring

Defaults are in `RegattaTimer/config.h`.

| Function | GPIO |
|---|---|
| 5 minute lamp | 32 |
| 4 minute lamp | 33 |
| 3 minute lamp | 25 |
| 2 minute lamp | 26 |
| 1 minute lamp | 27 |
| Horn driver | 13 |
| Start / reset button | 4 |
| Run selector switch | 16 |

The button and the switch go to GND and use the internal pullups, so no
external resistors are needed for them.

Do not drive lamps or a horn directly from a GPIO. Each lamp wants a series
resistor and, above ~10 mA, a small transistor. The horn goes through a MOSFET
or a solid state relay, with a **10k pulldown from the gate to GND** — that
resistor is what keeps the horn silent while the ESP32 boots and the pin is
still floating. Put a flyback diode across the coil if the horn is a relay-fed
DC klaxon, and run the horn off its own supply with a common ground.

If your hardware sinks current rather than sourcing it, set `LED_ACTIVE_HIGH`
or `HORN_ACTIVE_HIGH` to `false` in `config.h` instead of rewiring.

## Timing

Everything runs off `millis()` in a non-blocking loop, so blast lengths and
horn gaps never push the schedule late. Signal times are absolute offsets from
the moment you arm, which means a long blast cannot make the next signal drift.
`HORN_MAX_ON_MS` caps any single blast as a compressor safeguard.

## Building

Arduino IDE: open `RegattaTimer/RegattaTimer.ino`, pick your ESP32 board, and
upload. Serial monitor at 115200 prints the countdown and every signal.

The sketch has no dependencies beyond the ESP32 Arduino core. A `platformio.ini`
is included if you prefer PlatformIO.

## Tests

`test/` compiles the sketch on a host machine against a stub Arduino API and
runs the whole sequence against a virtual clock, checking signal times, blast
counts and lamp states for both run modes plus the abort path. No hardware
needed:

```
make -C test check
```
