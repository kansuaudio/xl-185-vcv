#pragma once
#include "Engine.hpp"

namespace xl {

// What the stage gate jacks show (context menu).
enum StageGateMode {
	STAGE_GATE_NOTE = 0,	// a copy of the gate on the jack of the stage that played the note
	STAGE_GATE_STAGE,	// high for the whole visit of a stage
	STAGE_GATE_TRIGGER,	// 1 ms at a stage start
	STAGE_GATE_COUNT
};

// One sample of input. Edges are already detected by the caller.
struct PlayerIn {
	bool clock;	// rising edge
	bool reset;	// rising edge
	bool run;
	int transpose;	// semitones from the TRANSPOSE input, rounded
	float swing;	// 0..100 %
	float gate;	// 1..100 %
	float glide;	// 0..500 ms
	int stageGateMode;

	PlayerIn() : clock(false), reset(false), run(true), transpose(0), swing(0.f), gate(100.f), glide(60.f), stageGateMode(STAGE_GATE_NOTE) {}
};

// One sample of output.
struct PlayerOut {
	float vOct;	// 0 V = C4
	bool gate;
	float velocity;	// 0..10 V
	bool slide;
	bool stageTrig;	// 1 ms at each stage start
	bool eoc;	// 1 ms at the loop wrap
	bool stageGate[NUM_STAGES];
	int stage;	// playing stage 1..8, 0 none
	bool stepRan;	// a step ran on this sample
	bool stageStart;	// a step on this sample started a stage (stage > 0)
};

// Auto Mutate timing: counts the steps that ran since the last Reset or load. Stop and continue
// keep the count, as the pattern continues where it stopped, so Auto stays on the bar.
struct AutoCount {
	long long steps;

	AutoCount() : steps(0) {}
	void restart() { steps = 0; }

	// Once per sample, after the Player. True right after the last step of every block of
	// bars x stepsPerBar steps, so the next bar starts changed.
	// The run argument is kept for the callers; Run does not change the count.
	bool process(bool reset, bool /*run*/, bool stepRan, int bars, int stepsPerBar) {
		if (reset)
			steps = 0;
		if (!stepRan)
			return false;
		steps++;
		long long n = (long long) bars * stepsPerBar;
		return n > 0 && steps % n == 0;
	}
};

// The time layer: clock period, swing delay, gate lengths, glide and triggers around the Engine.
// Called once per sample. No allocation; all times in seconds from sampleTime.
class Player {
public:
	Player();

	// As constructed: the next clock plays the first step, the period is forgotten.
	// A RESET edge while running plays the first step at once (process()).
	void reset();
	void seed(uint32_t s) { engine.seed(s); }
	void process(const Pattern& p, const PlayerIn& in, float sampleTime, PlayerOut& out);
	double period() const { return P; }

private:
	Engine engine;
	double P;	// clock period
	double sinceClock;	// time since the last accepted clock edge
	bool haveClock;
	double resetLeft;	// clocks are ignored while > 0
	long long count;	// steps since reset, for the swing parity
	bool pending;	// a swung step waits
	double pendingLeft;
	bool lastRun;
	bool lastGate;	// gate output of the previous sample
	bool ran;	// runStep ran on this sample
	bool started;	// a stage started on this sample

	// The sounding note.
	bool noteOn;
	bool noteSlide;
	int noteStage;
	double noteLeft;
	double gapLeft;	// forced low before a new attack
	float volts;
	float glideFrom;
	float glideTo;
	double glideTime;
	double glideAt;
	float velocity;

	int stage;
	double stageGapLeft;
	double stageTrigLeft;
	double eocLeft;

	void runStep(const Pattern& p, const PlayerIn& in);
};

} // namespace xl
