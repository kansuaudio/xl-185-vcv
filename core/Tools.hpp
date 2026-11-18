#pragma once
#include "Pattern.hpp"
#include "Random.hpp"

// The random tools of XL 1.1 on a Pattern. Each returns true when it changed something
// (one undo entry for the Rack layer), false when it had nothing to do.
// Targets as XL 1.1: Global, the dice, lane Random and both Resets write every unlocked stage 1..8
// (beyond Length and off included); Mutate picks an unlocked stage inside Length that is on, Lane Mutate
// an unlocked stage inside Length (off included); Shift rotates inside 1..Length.

namespace xl {

// Parts ticked for Global and Mutate (context menu). Default: pitch and velocity, as XL.
enum Part {
	PART_PITCH = 1 << 0,
	PART_VELOCITY = 1 << 1,
	PART_PROB = 1 << 2,
	PART_SLIDE = 1 << 3,
	PART_MODE = 1 << 4,
	PART_LEN = 1 << 5,
	PART_SEQLEN = 1 << 6,	// Global only
	PART_DEFAULT = PART_PITCH | PART_VELOCITY
};

// Ranges for the random pitch (MIDI notes, C4 = 60) and velocity. Defaults as XL.
struct ToolRanges {
	int pitchLo;
	int pitchHi;
	int velLo;
	int velHi;
	ToolRanges() : pitchLo(36), pitchHi(60), velLo(1), velHi(127) {}
};

// Global: new values for the ticked parts on every unlocked stage.
bool toolGlobal(Pattern& p, unsigned parts, const ToolRanges& r, Rng& rng);
// Mutate: one random unlocked stage inside Length that is on gets new values for the ticked parts
// (never Seq Length).
bool toolMutate(Pattern& p, unsigned parts, const ToolRanges& r, Rng& rng);
// Reset: every unlocked stage back to the defaults. Length and the settings stay.
bool toolReset(Pattern& p);
// A die for one row: part is one Part bit (not PART_SEQLEN).
bool toolDie(Pattern& p, unsigned part, const ToolRanges& r, Rng& rng);
// Shift: rotate every stage row inside stages 1..Length; dir -1 left, +1 right. Lock and on travel.
bool toolShift(Pattern& p, int dir);

// Lanes. lane = 0..2, or -1 for all three (the expander's Random).
bool toolLaneRandom(Pattern& p, int lane, Rng& rng);
// One random unlocked stage inside Length (on or off) gets new values on all three lanes.
bool toolLaneMutate(Pattern& p, Rng& rng);
// Every unlocked stage's lane values to 0, each lane's Min 0, Max 100, Smooth 0.
bool toolLaneReset(Pattern& p);
bool toolLaneShift(Pattern& p, int lane, int dir);

// Pitches (semitones from C4) the random tools pick from: knob range -36..36, one entry per
// distinct played pitch inside the range. Returns the count; out holds at least 73 entries.
int randomPitchPool(const Pattern& p, const ToolRanges& r, int* out);

} // namespace xl
