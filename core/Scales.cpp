#include "Scales.hpp"
#include <math.h>

namespace xl {

static const ScaleDef SCALES[SCALE_COUNT] = {
	{"Chromatic", 12, {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100}},
	{"Major", 7, {0, 200, 400, 500, 700, 900, 1100}},
	{"Minor", 7, {0, 200, 300, 500, 700, 800, 1000}},
	{"Dorian", 7, {0, 200, 300, 500, 700, 900, 1000}},
	{"Phrygian", 7, {0, 100, 300, 500, 700, 800, 1000}},
	{"Lydian", 7, {0, 200, 400, 600, 700, 900, 1100}},
	{"Mixolydian", 7, {0, 200, 400, 500, 700, 900, 1000}},
	{"Locrian", 7, {0, 100, 300, 500, 600, 800, 1000}},
	{"Harmonic Minor", 7, {0, 200, 300, 500, 700, 800, 1100}},
	{"Melodic Minor", 7, {0, 200, 300, 500, 700, 900, 1100}},
	{"Phrygian Dominant", 7, {0, 100, 400, 500, 700, 800, 1000}},
	{"Hungarian Minor", 7, {0, 200, 300, 600, 700, 800, 1100}},
	{"Major Pentatonic", 5, {0, 200, 400, 700, 900}},
	{"Minor Pentatonic", 5, {0, 300, 500, 700, 1000}},
	{"Minor Blues", 6, {0, 300, 500, 600, 700, 1000}},
	{"Whole Tone", 6, {0, 200, 400, 600, 800, 1000}},
	{"Hirajoshi", 5, {0, 200, 300, 700, 800}},
	{"In-Sen", 5, {0, 100, 500, 700, 1000}},
	// Microtonal from here (SCALE_FIRST_MICRO): exact cents.
	{"Rast ~", 7, {0, 200, 350, 500, 700, 900, 1050}},
	{"Bayati ~", 7, {0, 150, 300, 500, 700, 800, 1000}},
	{"Sikah ~", 7, {0, 150, 350, 550, 700, 850, 1050}},
	{"Just Major ~", 7, {0, 203.91, 386.31, 498.04, 701.96, 884.36, 1088.27}},
	{"Just Minor ~", 7, {0, 203.91, 315.64, 498.04, 701.96, 813.69, 1017.6}},
	{"Harmonic 8-16 ~", 8, {0, 203.91, 386.31, 551.32, 701.96, 840.53, 968.83, 1088.27}},
	{"Slendro 5-EDO ~", 5, {0, 240, 480, 720, 960}},
	{"7-EDO ~", 7, {0, 171.43, 342.86, 514.29, 685.71, 857.14, 1028.57}},
};

const ScaleDef& scaleDef(int index) {
	if (index < 0)
		index = 0;
	if (index >= SCALE_COUNT)
		index = SCALE_COUNT - 1;
	return SCALES[index];
}

double snapCents(int semitones, int root, int scale) {
	if (semitones < 0)
		semitones = 0;
	if (semitones > 127)
		semitones = 127;
	double t = semitones * 100.0;
	if (scale < 0 || scale >= SCALE_COUNT)
		return t;
	const ScaleDef& s = SCALES[scale];
	double r = (double) (((root % 12) + 12) % 12) * 100.0;
	int k0 = (int) floor((t - r) / 1200.0);
	double best = t;
	double bestD = 1e9;
	bool found = false;
	for (int k = k0 - 1; k <= k0 + 1; k++) {
		for (int i = 0; i < s.count; i++) {
			double c = r + k * 1200.0 + s.cents[i];
			// Only pitches that round to MIDI notes 0..127 count.
			if (c < -50.0 || c >= 12750.0)
				continue;
			double d = fabs(c - t);
			// Nearer wins; an equal distance keeps the lower pitch.
			if (!found || d < bestD - 1e-6 || (fabs(d - bestD) <= 1e-6 && c < best)) {
				best = c;
				bestD = d;
				found = true;
			}
		}
	}
	return best;
}

int nearestNote(double cents) {
	return (int) floor(cents / 100.0 + 0.5);
}

} // namespace xl
