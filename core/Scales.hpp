#pragma once

// The scale table of XL 1.1 (26 scales, the last 8 microtonal) and the snap.

namespace xl {

enum { SCALE_COUNT = 26, SCALE_MAX_DEGREES = 12, SCALE_FIRST_MICRO = 18 };

struct ScaleDef {
	const char* name;
	int count;
	double cents[SCALE_MAX_DEGREES];	// above the root, inside one octave
};

const ScaleDef& scaleDef(int index);

// Absolute pitch in cents (MIDI note 0 = 0 cents) of a pitch in semitones (MIDI numbering):
// clipped to 0..127, then snapped to the nearest pitch of the scale on root (ties go down).
// scale SCALE_OFF passes the clipped note through.
double snapCents(int semitones, int root, int scale);

// Nearest MIDI note of a cents value; half a semitone rounds up, as XL's note table does.
int nearestNote(double cents);

} // namespace xl
