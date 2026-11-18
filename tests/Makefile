# Core tests: `make test` builds with zero warnings allowed and runs the unit tests and the player tests.
# `make bench` times the core on a busy pattern; it is not part of `make test`.
CXX ?= c++
CXXFLAGS = -std=c++11 -Wall -Wextra -Werror -O2
CORE = ../core/Engine.cpp ../core/Scales.cpp ../core/Tools.cpp ../core/Player.cpp
HEADERS = $(wildcard ../core/*.hpp) check.hpp
BUILD = build

.PHONY: test unit player bench clean

test: unit player

unit: $(BUILD)/unit
	./$(BUILD)/unit

player: $(BUILD)/player
	./$(BUILD)/player

bench: $(BUILD)/bench
	./$(BUILD)/bench

$(BUILD)/unit: test_main.cpp $(CORE) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ test_main.cpp $(CORE)

$(BUILD)/player: test_player.cpp $(CORE) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ test_player.cpp $(CORE)

$(BUILD)/bench: bench.cpp $(CORE) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ bench.cpp $(CORE)

clean:
	rm -rf $(BUILD)
