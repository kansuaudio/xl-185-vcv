#pragma once
#include "Pattern.hpp"
#include "Random.hpp"

namespace xl {

// What one clock step does. Filled by Engine::step().
struct StepResult {
	long long step;	// step index e since the last reset, 0 on the first step
	int stage;	// 1..8, 0 when there is no stage
	int offset;	// grid step inside the stage, 0 = its first
	bool stageStart;	// stage changed, reset, restart or a random pick
	bool pulse;	// the stage fires (before velocity and probability)
	bool note;	// a note plays (pulse, velocity > 0, probability roll passed)
	bool tie;	// note with the pitch of a slide note still sounding: no new attack
	bool slide;	// the note's stage slides
	bool restart;	// placed by Restart on this step
	bool eoc;	// the loop wraps on this step (EOC output)
	int semitones;	// pulse pitch before the snap: 60 + pitch + transposes, clipped 0..127
	double cents;	// pulse pitch after the snap, MIDI note 0 = 0 cents
	float volts;	// the same as 1 V/oct, 0 V = C4 (MIDI 60)
	int midiNote;	// nearest MIDI note of cents (half a semitone rounds up, as XL)
	int velocity;	// 0..127
	float length;	// note length in steps
	float lane[NUM_LANES];	// 0..1, set on stage starts only
};

// The stage machine: an incremental walk (stage K, offset o, travel d, count c) so an edit
// between steps changes what the next step reads and never moves the playhead.
// No allocation, no time: the Rack layer turns the results into voltages.
class Engine {
public:
	Engine();

	// Next step plays as XL does at song position 0, e restarts at 0. A sounding note is kept.
	void reset();
	void seed(uint32_t s) { rng.seed(s); }

	// One clock step. transposeSemisFromCV is the TRANSPOSE input, already rounded.
	void step(const Pattern& p, int transposeSemisFromCV, StepResult& out);

	// The Rack layer ended the sounding note (RUN low), so nothing can tie to it.
	void endNote() { noteLeft = 0.f; }

private:
	Rng rng;
	long long e;	// steps since reset; the next step gets this index
	bool needPlace;	// place on the next step (after reset or with no stage)
	int k;	// stage index, -1 none
	int o;	// grid offset inside the stage
	int d;	// +1 up, -1 down (back and forth)
	int c;	// pulse count 1..D
	long long randomSteps;	// steps since the last random-direction loop wrap
	// The last played note, in step units, for the tie decision.
	float noteLeft;	// steps it still sounds after the current step
	bool noteSlide;
	double noteCents;

	void place(const Pattern& p);
	bool advance(const Pattern& p);
	int pick(const Pattern& p);
};

} // namespace xl
