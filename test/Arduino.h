#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2

extern uint32_t gVirtualMs;
extern int gPinLevel[64];
extern int gPinRead[64];

inline uint32_t millis() { return gVirtualMs; }
inline void delay(uint32_t ms) { gVirtualMs += ms; }
inline void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, int level);
inline int digitalRead(uint8_t pin) { return gPinRead[pin]; }

struct SerialStub {
  bool verbose = false;
  void begin(unsigned long) {}
  void println(const char *s) { if (verbose) printf("%s\n", s); }
  void printf(const char *fmt, ...) {
    if (!verbose) return;
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
  }
};
extern SerialStub Serial;
