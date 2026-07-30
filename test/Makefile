# Host-side simulation of the sketch. No ESP32 hardware needed:
#   make && ./sim
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -I.

sim: test_main.cpp Arduino.h ../RegattaTimer/RegattaTimer.ino ../RegattaTimer/config.h
	$(CXX) $(CXXFLAGS) -x c++ test_main.cpp -o sim

.PHONY: check clean
check: sim
	./sim

clean:
	rm -f sim
