#include "Engine.hpp"
#include "Scales.hpp"
#include <math.h>

namespace xl {

// First and last stage in use, -1 when none.
static int bottomStage(const Pattern& p) {
	for (int i = 0; i < NUM_STAGES; i++)
		if (p.inLoop(i))
			return i;
	return -1;
}

static int topStage(const Pattern& p) {
	for (int i = NUM_STAGES - 1; i >= 0; i--)
		if (p.inLoop(i))
			return i;
	return -1;
}

// Next stage in use above k, wrapping to the bottom; k itself when it is the only one.
static int nextUp(const Pattern& p, int k) {
	for (int n = 1; n <= NUM_STAGES; n++) {
		int i = (k + n) % NUM_STAGES;
		if (p.inLoop(i))
			return i;
	}
	return -1;
}

static int nextDown(const Pattern& p, int k) {
	for (int n = 1; n <= NUM_STAGES; n++) {
		int i = ((k - n) % NUM_STAGES + NUM_STAGES) % NUM_STAGES;
		if (p.inLoop(i))
			return i;
	}
	return -1;
}

static int stageLen(const Pattern& p, int k) {
	if (k < 0)
		return 1;
	int l = p.stage[k].len;
	return l < 1 ? 1 : (l > 8 ? 8 : l);
}

Engine::Engine() {
	reset();
	noteLeft = 0.f;
	noteSlide = false;
	noteCents = 0.0;
}

void Engine::reset() {
	e = 0;
	needPlace = true;
	k = -1;
	o = 0;
	d = 1;
	c = 0;
	randomSteps = 0;
}

int Engine::pick(const Pattern& p) {
	int list[NUM_STAGES];
	int n = 0;
	for (int i = 0; i < NUM_STAGES; i++)
		if (p.inLoop(i))
			list[n++] = i;
	if (n == 0)
		return -1;
	return list[rng.below(n)];
}

// As XL at song position 0: forward stage 1 step 1, backward the last stage's last step,
// back and forth grid step 1, random a fresh pick.
void Engine::place(const Pattern& p) {
	needPlace = false;
	randomSteps = 0;
	o = 0;
	d = 1;
	switch (p.direction) {
		case DIR_BACKWARD:
			k = topStage(p);
			o = stageLen(p, k) - 1;
			d = -1;
			break;
		case DIR_RANDOM:
			k = pick(p);
			break;
		default:
			k = bottomStage(p);
			break;
	}
	if (k < 0)
		o = 0;
}

// One step of the walk (DESIGN 2 table). Returns true when the loop wraps.
// A stage that left the loop (beyond Length) has length 0 here, so it ends at once.
bool Engine::advance(const Pattern& p) {
	int ln = p.inLoop(k) ? stageLen(p, k) : 0;
	int o2 = (o < ln ? o : ln) - 1;
	switch (p.direction) {
		case DIR_FORWARD: {
			d = 1;
			if (o + 1 < ln) {
				o++;
				return false;
			}
			int n = nextUp(p, k);
			bool wrap = n >= 0 && n <= k;
			k = n;
			o = 0;
			return wrap;
		}
		case DIR_BACKWARD: {
			d = -1;
			if (o2 >= 0) {
				o = o2;
				return false;
			}
			int n = nextDown(p, k);
			bool wrap = n >= 0 && n >= k;
			k = n;
			o = stageLen(p, n) - 1;
			return wrap;
		}
		case DIR_PINGPONG: {
			int top = topStage(p);
			int bottom = bottomStage(p);
			if (d > 0) {
				if (o + 1 < ln) {
					o++;
				}
				else if (k == top) {
					// Turn inside the top stage: its way out and back is one visit.
					d = -1;
					if (o2 >= 0) {
						o = o2;
					}
					else {
						k = nextDown(p, k);
						o = stageLen(p, k) - 1;
					}
				}
				else {
					k = nextUp(p, k);
					o = 0;
				}
			}
			else {
				if (o2 >= 0) {
					o = o2;
				}
				else if (k == bottom) {
					// Turn inside the bottom stage, grid step 1 is not played twice.
					d = 1;
					if (ln > 1) {
						o = 1;
					}
					else {
						k = nextUp(p, k);
						o = 0;
					}
				}
				else {
					k = nextDown(p, k);
					o = stageLen(p, k) - 1;
				}
			}
			// The loop wraps on grid step 1, the bottom turn (every step with a one-step loop).
			bottom = bottomStage(p);
			return k >= 0 && k == bottom && o == 0 && (d < 0 || stageLen(p, k) == 1);
		}
		default:
			break;
	}
	return false;
}

void Engine::step(const Pattern& p, int transposeSemisFromCV, StepResult& out) {
	out.step = e;
	out.stage = 0;
	out.offset = 0;
	out.stageStart = false;
	out.pulse = false;
	out.note = false;
	out.tie = false;
	out.slide = false;
	out.restart = false;
	out.eoc = false;
	out.semitones = 0;
	out.cents = 0.0;
	out.volts = 0.f;
	out.midiNote = 0;
	out.velocity = 0;
	out.length = 0.f;
	for (int l = 0; l < NUM_LANES; l++)
		out.lane[l] = 0.f;

	// The last note ages by one step.
	noteLeft -= 1.f;
	if (noteLeft < 0.f)
		noteLeft = 0.f;

	bool restartNow = p.restart > 0 && e > 0 && (e % p.restart) == 0;
	int prevK = k;
	bool forced = false;
	if (needPlace || k < 0 || restartNow) {
		place(p);
		forced = true;
		out.restart = restartNow;
		out.eoc = restartNow;
	}
	else {
		bool random = p.direction == DIR_RANDOM;
		int ln = p.inLoop(k) ? stageLen(p, k) : 0;
		if (random) {
			// Whole stages, repeats allowed, every pick a stage start.
			if (o + 1 < ln) {
				o++;
			}
			else {
				k = pick(p);
				o = 0;
				forced = true;
			}
			// Random has no loop end: it wraps every L steps.
			randomSteps++;
			int loopLen = 0;
			for (int i = 0; i < NUM_STAGES; i++)
				if (p.inLoop(i))
					loopLen += stageLen(p, i);
			if (loopLen > 0 && randomSteps >= loopLen) {
				out.eoc = true;
				randomSteps = 0;
			}
		}
		else {
			out.eoc = advance(p);
		}
	}
	e++;

	if (k < 0) {
		out.eoc = false;
		return;
	}

	const Stage& s = p.stage[k];
	int len = stageLen(p, k);
	out.stage = k + 1;
	out.offset = o;
	out.stageStart = forced || k != prevK;

	// Count 1..D, restarted by every stage start; D follows edits at once (Max counter wrap).
	int div = modeDiv(s.mode);
	int D = (s.mode == MODE_ONCE || s.mode == MODE_HOLD) ? len : div;
	if (out.stageStart)
		c = 1;
	else
		c = (c + 1 > D) ? 1 : c + 1;
	out.pulse = s.on && s.mode != MODE_REST && c == 1;

	if (out.stageStart) {
		for (int l = 0; l < NUM_LANES; l++) {
			const Lane& ln = p.lane[l];
			out.lane[l] = (ln.min + ln.value[k] / 100.f * (ln.max - ln.min)) / 100.f;
		}
	}

	if (!out.pulse)
		return;

	int semis = 60 + s.pitch + p.transpose + transposeSemisFromCV;
	if (semis < 0)
		semis = 0;
	if (semis > 127)
		semis = 127;
	out.semitones = semis;
	out.cents = snapCents(semis, p.root, p.scale);
	out.volts = (float) ((out.cents - 6000.0) / 1200.0);
	out.midiNote = nearestNote(out.cents);
	out.velocity = s.velocity;
	out.slide = s.slide;
	if (s.slide) {
		int slot = div > 0 ? (div < len ? div : len) : len;
		out.length = slot + 0.75f;
	}
	else {
		out.length = s.mode == MODE_HOLD ? (float) len : 1.f;
	}

	// One roll per pulse, also when velocity is 0, so the random stream does not depend on it.
	int roll = rng.below(100);
	out.note = s.velocity > 0 && roll < s.prob;
	if (!out.note)
		return;

	// Tie: the last note slid and still sounds at this step with the same pitch. Decided in step
	// units: a slide lasts slot + 3/4 step, so it covers the next note at the nominal clock. The Rack
	// layer must still check its gate in time: if the clock slowed down so far that the slide's gate
	// already fell before this step, this note is a new attack, not a tie.
	out.tie = noteSlide && noteLeft > 1e-4f && fabs(out.cents - noteCents) < 1e-6;
	noteLeft = out.length;
	noteSlide = out.slide;
	noteCents = out.cents;
}

} // namespace xl
