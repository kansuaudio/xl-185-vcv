#pragma once

// Everything the engine and the tools read: per stage values, three lanes and the settings.
// Plain data, no time values except the lane Smooth (kept here so Lane Reset can clear it).

namespace xl {

enum { NUM_STAGES = 8, NUM_LANES = 3 };

enum Mode {
	MODE_REST = 0,
	MODE_ONCE,
	MODE_HOLD,
	MODE_QUARTER,	// repeat every 4th step
	MODE_THIRD,	// every 3rd
	MODE_HALF,	// every 2nd
	MODE_ALL,	// every step
	MODE_COUNT
};

enum Direction {
	DIR_FORWARD = 0,
	DIR_BACKWARD,
	DIR_PINGPONG,
	DIR_RANDOM,
	DIR_COUNT
};

// Scale index: SCALE_OFF or 0..SCALE_COUNT-1 into the table in Scales.hpp.
enum { SCALE_OFF = -1 };

// Repeat interval of a mode in steps, 0 for rest, once and hold.
inline int modeDiv(int mode) {
	switch (mode) {
		case MODE_QUARTER: return 4;
		case MODE_THIRD: return 3;
		case MODE_HALF: return 2;
		case MODE_ALL: return 1;
		default: return 0;
	}
}

struct Stage {
	int pitch;	// semitones from C4 (MIDI 60), -36..36
	int velocity;	// 0..127, 0 never plays
	int mode;	// Mode
	int len;	// 1..8 steps
	int prob;	// 0..100 %
	bool slide;
	bool on;
	bool lock;
};

struct Lane {
	float value[NUM_STAGES];	// 0..100 %
	float min;	// 0..100 %
	float max;	// 0..100 %, below min turns the lane over
	float smooth;	// ms, used by the Rack layer only
};

// Defaults: a fresh XL-185.
enum {
	DEFAULT_PITCH = 0,
	DEFAULT_VELOCITY = 120,
	DEFAULT_MODE = MODE_ONCE,
	DEFAULT_LEN = 1,
	DEFAULT_PROB = 100
};

inline void stageDefaults(Stage& s) {
	s.pitch = DEFAULT_PITCH;
	s.velocity = DEFAULT_VELOCITY;
	s.mode = DEFAULT_MODE;
	s.len = DEFAULT_LEN;
	s.prob = DEFAULT_PROB;
	s.slide = false;
	s.on = true;
	s.lock = false;
}

inline void laneDefaults(Lane& l) {
	for (int i = 0; i < NUM_STAGES; i++)
		l.value[i] = 0.f;
	l.min = 0.f;
	l.max = 100.f;
	l.smooth = 0.f;
}

struct Pattern {
	Stage stage[NUM_STAGES];
	Lane lane[NUM_LANES];
	int length;	// stages in use, 1..8
	int direction;	// Direction
	int transpose;	// semitones
	int restart;	// steps between restarts, 0 = off (panel: 16, 32, 64, 128)
	int scale;	// SCALE_OFF or a table index
	int root;	// 0..11, C = 0

	Pattern() {
		for (int i = 0; i < NUM_STAGES; i++)
			stageDefaults(stage[i]);
		for (int l = 0; l < NUM_LANES; l++)
			laneDefaults(lane[l]);
		length = NUM_STAGES;
		direction = DIR_FORWARD;
		transpose = 0;
		restart = 0;
		scale = SCALE_OFF;
		root = 0;
	}

	// The loop holds the stages inside Length. A stage that is off keeps its place and time,
	// like a rest: it starts and sends its lanes but never pulses (XL 1.1).
	bool inLoop(int i) const {
		return i >= 0 && i < length && i < NUM_STAGES;
	}
};

} // namespace xl
