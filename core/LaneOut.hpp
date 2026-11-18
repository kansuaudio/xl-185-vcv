#pragma once

// One expander lane output in volts. At each stage start it takes the stage's target and either
// jumps there (Smooth 0) or ramps there linearly from where it is, over Smooth ms.
// Plain C++11, no allocation; time comes from the caller's sample time.

namespace xl {

struct LaneOut {
	float volts;
	float from;
	float to;
	double time;	// ramp length in seconds, 0 = no ramp
	double at;	// time into the ramp

	LaneOut() {
		reset();
	}

	void reset() {
		volts = from = to = 0.f;
		time = at = 0.0;
	}

	// The lane formula of the engine (XL_SEMANTICS 4) in volts: 10 V x (Min + value x (Max - Min)),
	// all three in percent. Min above Max turns the lane over.
	static float target(float value, float min, float max) {
		return 10.f * (min + value / 100.f * (max - min)) / 100.f;
	}

	// A stage start: head for v over smoothMs.
	void start(float v, float smoothMs) {
		if (smoothMs <= 0.f) {
			volts = from = to = v;
			time = at = 0.0;
			return;
		}
		from = volts;
		to = v;
		time = smoothMs * 0.001;
		at = 0.0;
	}

	// Once per sample; returns the output.
	float process(float sampleTime) {
		if (time > 0.0) {
			at += sampleTime;
			if (at >= time - 1e-9) {
				volts = to;
				time = at = 0.0;
			}
			else {
				volts = from + (float) ((to - from) * (at / time));
			}
		}
		return volts;
	}
};

} // namespace xl
