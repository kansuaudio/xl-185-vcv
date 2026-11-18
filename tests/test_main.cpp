// Core unit tests. Build and run with `make test` in this folder.
#include "check.hpp"
#include "../core/Engine.hpp"
#include "../core/Scales.hpp"
#include "../core/Tools.hpp"
#include <math.h>
#include <string.h>

using namespace xl;

int g_checks = 0;
int g_failures = 0;

static void setStage(Pattern& p, int n, int mode, int len, int pitch = 0) {
	Stage& s = p.stage[n - 1];
	s.mode = mode;
	s.len = len;
	s.pitch = pitch;
}

// Run n steps from reset into out.
static void run(Engine& e, const Pattern& p, int n, StepResult* out, int cv = 0) {
	e.reset();
	for (int i = 0; i < n; i++)
		e.step(p, cv, out[i]);
}

// XL's grid from the song position (XL_SEMANTICS 1.4) for the stages in use: stage and offset at step i.
static void xlGrid(const Pattern& p, int i, int& stage, int& offset) {
	int lens[NUM_STAGES], ids[NUM_STAGES], n = 0, L = 0;
	for (int k = 0; k < NUM_STAGES; k++) {
		if (p.inLoop(k)) {
			ids[n] = k;
			lens[n] = p.stage[k].len;
			L += lens[n];
			n++;
		}
	}
	int g;
	if (p.direction == DIR_FORWARD)
		g = i % L + 1;
	else if (p.direction == DIR_BACKWARD)
		g = L - i % L;
	else if (L == 1)
		g = 1;
	else {
		int q = i % (2 * L - 2);
		g = q < L ? q + 1 : 2 * L - 1 - q;
	}
	int end = 0;
	for (int j = 0; j < n; j++) {
		if (g <= end + lens[j]) {
			stage = ids[j] + 1;
			offset = g - end - 1;
			return;
		}
		end += lens[j];
	}
	stage = 0;
	offset = 0;
}

static void testModes() {
	printf("modes: pulses over one visit\n");
	struct Case {
		int mode;
		const char* pulses;	// per step of an 8-step visit
	} cases[] = {
		{MODE_REST, "........"},
		{MODE_ONCE, "x......."},
		{MODE_HOLD, "x......."},
		{MODE_QUARTER, "x...x..."},
		{MODE_THIRD, "x..x..x."},
		{MODE_HALF, "x.x.x.x."},
		{MODE_ALL, "xxxxxxxx"},
	};
	for (unsigned t = 0; t < sizeof(cases) / sizeof(cases[0]); t++) {
		Pattern p;
		p.length = 2;
		setStage(p, 1, cases[t].mode, 8);
		Engine e;
		StepResult r[9];
		run(e, p, 9, r);
		for (int i = 0; i < 8; i++) {
			CHECK_EQ(r[i].stage, 1);
			CHECK_EQ(r[i].stageStart, i == 0);
			CHECK_EQ(r[i].pulse, cases[t].pulses[i] == 'x');
			if (r[i].pulse)
				CHECK_NEAR(r[i].length, cases[t].mode == MODE_HOLD ? 8.0 : 1.0, 1e-6);
		}
		CHECK_EQ(r[8].stage, 2);
		CHECK(r[8].stageStart && r[8].pulse);
		// Rest stages start like any other stage, they just never pulse.
		CHECK_EQ(r[0].stageStart, true);
	}
}

static void compareWithGrid(const Pattern& p, int n, const char* what) {
	Engine e;
	StepResult r[200];
	run(e, p, n, r);
	int bad = 0;
	int prev = -1;
	for (int i = 0; i < n; i++) {
		int st, off;
		xlGrid(p, i, st, off);
		bool start = i == 0 || st != prev;
		if (r[i].stage != st || r[i].offset != off || r[i].stageStart != start)
			bad++;
		prev = st;
	}
	if (bad)
		printf("  %s: %d steps differ from the XL grid\n", what, bad);
	CHECK_EQ(bad, 0);
}

static void testDirections() {
	printf("directions: forward, backward, back and forth against XL's grid formula\n");
	const int lensA[5] = {3, 2, 4, 1, 0};
	const int lensB[5] = {2, 4, 2, 1, 3};	// pp_mixed
	const int lensC[5] = {3, 5, 2, 4, 0};	// pp_ends
	const int* sets[3] = {lensA, lensB, lensC};
	for (int s = 0; s < 3; s++) {
		for (int dir = DIR_FORWARD; dir <= DIR_PINGPONG; dir++) {
			Pattern p;
			p.direction = dir;
			p.length = sets[s][4] ? 5 : 4;
			for (int i = 0; i < p.length; i++)
				setStage(p, i + 1, MODE_ONCE, sets[s][i]);
			compareWithGrid(p, 120, "lens set");
		}
	}
	// Eight stages of Len 1, and Len 1 ends with longer middles.
	for (int dir = DIR_FORWARD; dir <= DIR_PINGPONG; dir++) {
		Pattern p;
		p.direction = dir;
		compareWithGrid(p, 60, "8 x Len 1");
		setStage(p, 4, MODE_ONCE, 5);
		p.length = 6;
		compareWithGrid(p, 80, "Len 1 ends");
	}

	// Back and forth: the merged top visit of hold Len 3 is 5 steps, one start, pulses at 1 and 4.
	Pattern p;
	p.direction = DIR_PINGPONG;
	p.length = 5;
	setStage(p, 1, MODE_ONCE, 2);
	setStage(p, 2, MODE_THIRD, 4);
	setStage(p, 3, MODE_REST, 2);
	setStage(p, 4, MODE_ALL, 1);
	setStage(p, 5, MODE_HOLD, 3);
	Engine e;
	StepResult r[60];
	run(e, p, 50, r);
	// Steps 9..13 are the top visit (g10 g11 g12 g11 g10).
	CHECK_EQ(r[9].stage, 5);
	CHECK(r[9].stageStart);
	for (int i = 10; i <= 13; i++) {
		CHECK_EQ(r[i].stage, 5);
		CHECK(!r[i].stageStart);
	}
	CHECK(r[9].pulse && r[12].pulse);
	CHECK(!r[10].pulse && !r[11].pulse && !r[13].pulse);
	CHECK_EQ(r[14].stage, 4);
	// Bottom once Len 2: g2 g1 g2 at steps 21..23, a pulse on each g2.
	CHECK_EQ(r[21].stage, 1);
	CHECK(r[21].stageStart && r[21].pulse);
	CHECK(!r[22].pulse && r[23].pulse && !r[23].stageStart);
	// The loop wraps on g1 (step 22), once per period of 22.
	int eocs = 0;
	for (int i = 0; i < 50; i++)
		if (r[i].eoc) {
			eocs++;
			CHECK(i == 22 || i == 44);
		}
	CHECK_EQ(eocs, 2);

	// Forward and backward EOC.
	Pattern f;
	f.length = 3;
	setStage(f, 2, MODE_ONCE, 2);
	run(e, f, 12, r);
	for (int i = 0; i < 12; i++)
		CHECK_EQ(r[i].eoc, i == 4 || i == 8);
	f.direction = DIR_BACKWARD;
	run(e, f, 12, r);
	CHECK_EQ(r[0].stage, 3);
	CHECK_EQ(r[1].stage, 2);
	CHECK_EQ(r[1].offset, 1);
	CHECK(r[1].pulse);	// a visit counts from its first step in time
	CHECK(!r[2].pulse);
	for (int i = 0; i < 12; i++)
		CHECK_EQ(r[i].eoc, i == 4 || i == 8);
}

static void testOneStage() {
	printf("one stage: back and forth turns inside it, count runs freely\n");
	Pattern p;
	p.direction = DIR_PINGPONG;
	p.length = 1;
	setStage(p, 1, MODE_ONCE, 4);
	Engine e;
	StepResult r[24];
	run(e, p, 24, r);
	for (int i = 0; i < 24; i++) {
		CHECK_EQ(r[i].stage, 1);
		CHECK_EQ(r[i].stageStart, i == 0);
		CHECK_EQ(r[i].pulse, i % 4 == 0);
		int st, off;
		xlGrid(p, i, st, off);
		CHECK_EQ(r[i].offset, off);
	}
	setStage(p, 1, MODE_ONCE, 1);
	for (int dir = DIR_FORWARD; dir <= DIR_RANDOM; dir++) {
		p.direction = dir;
		run(e, p, 8, r);
		for (int i = 0; i < 8; i++) {
			CHECK(r[i].pulse);
			// Random: every pick is a stage start, also when the same stage repeats.
			CHECK_EQ(r[i].stageStart, dir == DIR_RANDOM || i == 0);
			CHECK_EQ(r[i].eoc, i > 0);
		}
	}
}

static void testRandom() {
	printf("random direction: whole stages, each pick a stage start\n");
	Pattern p;
	p.direction = DIR_RANDOM;
	p.length = 4;
	for (int i = 0; i < 4; i++)
		setStage(p, i + 1, MODE_ONCE, i + 1);
	Engine e;
	e.seed(777);
	static StepResult r[1000];
	run(e, p, 1000, r);
	int used[NUM_STAGES + 1] = {0};
	int picks = 0, repeats = 0, prevStage = 0, eocs = 0;
	int i = 0;
	bool whole = true;
	while (i < 1000) {
		CHECK(r[i].stageStart);
		CHECK(r[i].pulse);
		int s = r[i].stage;
		CHECK(s >= 1 && s <= 4);
		used[s]++;
		picks++;
		if (s == prevStage)
			repeats++;
		prevStage = s;
		int len = p.stage[s - 1].len;
		for (int j = 1; j < len && i + j < 1000; j++) {
			if (r[i + j].stage != s || r[i + j].stageStart || r[i + j].offset != j)
				whole = false;
		}
		i += len;
	}
	for (int j = 0; j < 1000; j++)
		if (r[j].eoc) {
			eocs++;
			CHECK(j % 10 == 0);	// every L = 10 steps
		}
	CHECK(whole);
	CHECK(used[1] > 0 && used[2] > 0 && used[3] > 0 && used[4] > 0);
	CHECK(repeats > 0);
	CHECK(picks > 300);
	CHECK_EQ(eocs, 99);
	// Same seed, same walk.
	Engine e2;
	e2.seed(777);
	StepResult r2[100];
	run(e2, p, 100, r2);
	bool same = true;
	for (int j = 0; j < 100; j++)
		same = same && r2[j].stage == r[j].stage;
	CHECK(same);
}

static void testRestart() {
	printf("restart: placed as after reset on multiples of N\n");
	Engine e;
	StepResult r[40];
	Pattern p;
	p.length = 3;
	p.restart = 16;
	run(e, p, 40, r);
	CHECK_EQ(r[15].stage, 1);
	CHECK_EQ(r[16].stage, 1);
	CHECK(r[16].stageStart && r[16].restart && r[16].eoc && r[16].pulse);
	CHECK(!r[0].restart && !r[0].eoc);
	CHECK_EQ(r[17].stage, 2);
	CHECK(r[32].restart);
	p.direction = DIR_BACKWARD;
	run(e, p, 20, r);
	CHECK_EQ(r[15].stage, 3);
	CHECK_EQ(r[16].stage, 3);
	CHECK(r[16].stageStart && r[16].restart);
	p.direction = DIR_PINGPONG;
	p.length = 4;
	run(e, p, 20, r);
	CHECK_EQ(r[15].stage, 4);
	CHECK_EQ(r[16].stage, 1);
	CHECK(r[16].stageStart && r[16].restart);
	CHECK_EQ(r[17].stage, 2);
	// Random: a fresh pick, a stage start.
	p.direction = DIR_RANDOM;
	p.length = 2;
	setStage(p, 1, MODE_ONCE, 3);
	setStage(p, 2, MODE_ONCE, 3);
	e.seed(5);
	run(e, p, 20, r);
	CHECK(r[16].stageStart && r[16].restart && r[16].offset == 0);
}

static void testOffStages() {
	printf("off stages keep their time like a rest; beyond Length leaves the loop\n");
	Pattern p;
	p.length = 4;
	p.stage[1].on = false;
	p.stage[1].len = 2;
	p.lane[0].value[1] = 50.f;
	Engine e;
	StepResult r[12];
	run(e, p, 10, r);
	const int order[10] = {1, 2, 2, 3, 4, 1, 2, 2, 3, 4};
	for (int i = 0; i < 10; i++) {
		CHECK_EQ(r[i].stage, order[i]);
		CHECK_EQ(r[i].pulse, order[i] != 2);
		CHECK_EQ(r[i].note, order[i] != 2);
	}
	CHECK(r[1].stageStart && !r[2].stageStart);
	CHECK_NEAR(r[1].lane[0], 0.5, 1e-6);	// an off stage still sends its lanes
	CHECK(r[5].eoc);
	// All off: a silent loop that keeps stepping, starting stages and wrapping.
	for (int i = 0; i < NUM_STAGES; i++)
		p.stage[i].on = false;
	run(e, p, 10, r);
	for (int i = 0; i < 10; i++) {
		CHECK_EQ(r[i].stage, order[i]);
		CHECK(!r[i].pulse && !r[i].note);
	}
	CHECK(r[3].stageStart && r[5].eoc);
	// Switched back on between steps: it plays at its next pulse, the playhead does not move.
	e.reset();
	e.step(p, 0, r[0]);
	p.stage[1].on = true;
	e.step(p, 0, r[1]);
	CHECK_EQ(r[1].stage, 2);
	CHECK(r[1].pulse && r[1].stageStart);
	// Stage 1 off in back and forth: still the bottom end, silent.
	Pattern q;
	q.direction = DIR_PINGPONG;
	q.length = 3;
	q.stage[0].on = false;
	run(e, q, 8, r);
	const int pp[8] = {1, 2, 3, 2, 1, 2, 3, 2};
	for (int i = 0; i < 8; i++) {
		CHECK_EQ(r[i].stage, pp[i]);
		CHECK_EQ(r[i].pulse, pp[i] != 1);
	}
	// Stages beyond Length are out of the loop.
	Pattern f;
	f.length = 3;
	run(e, f, 6, r);
	for (int i = 0; i < 6; i++)
		CHECK_EQ(r[i].stage, i % 3 + 1);
}

static void testEdits() {
	printf("edits between steps never move the playhead\n");
	Pattern p;
	Engine e;
	StepResult r;
	e.reset();
	for (int i = 0; i < 6; i++)
		e.step(p, 0, r);
	CHECK_EQ(r.stage, 6);
	p.stage[1].len = 4;	// XL jumps to stage 4 here (edit_len); we go on
	e.step(p, 0, r);
	CHECK_EQ(r.stage, 7);
	CHECK(r.stageStart);
	p.length = 5;	// stage 7 left the loop: it ends at the next step
	e.step(p, 0, r);
	CHECK_EQ(r.stage, 1);
	CHECK(r.stageStart && r.eoc);
	e.step(p, 0, r);
	CHECK_EQ(r.stage, 2);
	p.direction = DIR_BACKWARD;	// stage 2 runs on downwards from where it is
	e.step(p, 0, r);
	CHECK_EQ(r.stage, 1);
	// Len lowered under the running count wraps it to 1 and pulses (Max counter wrap).
	Pattern q;
	q.length = 2;
	setStage(q, 1, MODE_QUARTER, 8);
	e.reset();
	for (int i = 0; i < 3; i++)
		e.step(q, 0, r);
	CHECK(!r.pulse);
	q.stage[0].mode = MODE_HALF;
	e.step(q, 0, r);	// count 3 -> 4 > 2 wraps to 1
	CHECK(r.pulse && !r.stageStart);
	// Shorter Len than the offset: the stage ends at the next step.
	q.stage[0].len = 2;
	e.step(q, 0, r);
	CHECK_EQ(r.stage, 2);
}

static void testProbability() {
	printf("probability and velocity 0\n");
	Pattern p;
	p.length = 1;
	setStage(p, 1, MODE_ALL, 8);
	Engine e;
	e.seed(42);
	StepResult r;
	p.stage[0].prob = 0;
	e.reset();
	int notes = 0;
	for (int i = 0; i < 200; i++) {
		e.step(p, 0, r);
		CHECK(r.pulse);
		notes += r.note;
	}
	CHECK_EQ(notes, 0);
	p.stage[0].prob = 100;
	notes = 0;
	for (int i = 0; i < 200; i++) {
		e.step(p, 0, r);
		notes += r.note;
	}
	CHECK_EQ(notes, 200);
	p.stage[0].velocity = 0;
	notes = 0;
	for (int i = 0; i < 50; i++) {
		e.step(p, 0, r);
		CHECK(r.pulse);
		notes += r.note;
	}
	CHECK_EQ(notes, 0);
	// 25 % and 50 %, one roll per pulse: a repeating stage varies inside its visit.
	p.stage[0].velocity = 100;
	const int probs[2] = {25, 50};
	for (int t = 0; t < 2; t++) {
		p.stage[0].prob = probs[t];
		notes = 0;
		int changes = 0, last = -1;
		for (int i = 0; i < 4000; i++) {
			e.step(p, 0, r);
			notes += r.note;
			if (last >= 0 && last != (int) r.note)
				changes++;
			last = r.note;
		}
		int want = 40 * probs[t];
		CHECK(notes > want - 200 && notes < want + 200);
		CHECK(changes > 500);
	}
}

static void testSlide() {
	printf("slide lengths and the tie decision\n");
	Pattern p;
	p.length = 2;
	Engine e;
	StepResult r[16];
	// Once slide: 1 + 3/4; tie into the same pitch, glide (no tie) into another.
	p.stage[0].slide = true;
	run(e, p, 2, r);
	CHECK_NEAR(r[0].length, 1.75, 1e-6);
	CHECK(r[0].slide);
	CHECK(r[1].note && r[1].tie);
	p.stage[1].pitch = 2;
	run(e, p, 2, r);
	CHECK(r[1].note && !r[1].tie);
	// No slide, same pitch: a new attack.
	Pattern q;
	run(e, q, 2, r);
	CHECK(r[1].note && !r[1].tie);
	// Repeat 1/2 Len 4 sliding: slot 2, 2.75 steps, ties its own repeats.
	Pattern s;
	setStage(s, 1, MODE_HALF, 4);
	s.stage[0].slide = true;
	run(e, s, 4, r);
	CHECK_NEAR(r[0].length, 2.75, 1e-6);
	CHECK(r[2].note && r[2].tie);
	// 1/4 on Len 2: slot = min(Div, Len) = 2.
	setStage(s, 1, MODE_QUARTER, 2);
	run(e, s, 1, r);
	CHECK_NEAR(r[0].length, 2.75, 1e-6);
	// Hold Len 3 sliding: 3.75; ties into a hold of the same pitch.
	Pattern h;
	h.length = 2;
	setStage(h, 1, MODE_HOLD, 3);
	setStage(h, 2, MODE_HOLD, 1);
	h.stage[0].slide = true;
	run(e, h, 4, r);
	CHECK_NEAR(r[0].length, 3.75, 1e-6);
	CHECK(r[3].note && r[3].tie);
	CHECK_NEAR(r[3].length, 1.0, 1e-6);
	// Slide before a rest: ended before the next note, no tie.
	Pattern t;
	t.length = 3;
	t.stage[0].slide = true;
	setStage(t, 2, MODE_REST, 1);
	run(e, t, 3, r);
	CHECK(r[2].note && !r[2].tie);
	// Slide before a failed roll: the next same-pitch note one step later is no tie either.
	t.stage[1].mode = MODE_ONCE;
	t.stage[1].prob = 0;
	run(e, t, 3, r);
	CHECK(!r[1].note && r[2].note && !r[2].tie);
	// A tie restarts the note with the tied note's length.
	Pattern u;
	u.length = 3;
	u.stage[0].slide = true;
	u.stage[1].slide = true;
	run(e, u, 3, r);
	CHECK(r[1].tie && r[2].tie);
}

static void testScales() {
	printf("scales: snap, ties down, microtonal, clip\n");
	const int dorian = 3, rast = 18;
	CHECK(strcmp(scaleDef(dorian).name, "Dorian") == 0);
	CHECK(strcmp(scaleDef(rast).name, "Rast ~") == 0);
	CHECK(strcmp(scaleDef(SCALE_COUNT - 1).name, "7-EDO ~") == 0);
	// D Dorian: 61 -> 60, 63 -> 62, 66 -> 65, 68 -> 67 (ties go down).
	const int in[8] = {60, 61, 62, 63, 64, 65, 66, 67};
	const int out[8] = {60, 60, 62, 62, 64, 65, 65, 67};
	for (int i = 0; i < 8; i++)
		CHECK_NEAR(snapCents(in[i], 2, dorian), out[i] * 100.0, 1e-9);
	CHECK_NEAR(snapCents(68, 2, dorian), 6700.0, 1e-9);
	// Off and chromatic pass the note.
	CHECK_NEAR(snapCents(61, 0, SCALE_OFF), 6100.0, 1e-9);
	CHECK_NEAR(snapCents(61, 5, 0), 6100.0, 1e-9);
	// Clip before the snap.
	CHECK_NEAR(snapCents(187, 0, SCALE_OFF), 12700.0, 1e-9);
	CHECK_NEAR(snapCents(-60, 0, SCALE_OFF), 0.0, 1e-9);
	// Rast on D: exact cents; the -50 cent pitch keeps note 61 (half rounds up, as XL).
	CHECK_NEAR(snapCents(61, 2, rast), 6050.0, 1e-9);
	CHECK_EQ(nearestNote(6050.0), 61);
	CHECK_NEAR(snapCents(66, 2, rast), 6550.0, 1e-9);
	// Just Major on C: E is 386.31 cents.
	CHECK_NEAR(snapCents(64, 0, 21), 6386.31, 1e-9);
	// Engine: volts relative to C4 and the transposes before the snap.
	Pattern p;
	p.length = 1;
	p.scale = dorian;
	p.root = 2;
	p.transpose = 1;
	setStage(p, 1, MODE_ONCE, 1, 2);
	Engine e;
	StepResult r;
	e.reset();
	e.step(p, 0, r);
	CHECK_EQ(r.semitones, 63);
	CHECK_EQ(r.midiNote, 62);
	CHECK_NEAR(r.volts, 2.0 / 12.0, 1e-6);
	e.step(p, 3, r);	// TRANSPOSE in +3: 66 -> 65
	CHECK_EQ(r.midiNote, 65);
	p.scale = SCALE_OFF;
	p.stage[0].pitch = 36;
	p.transpose = 24;
	e.step(p, 67, r);
	CHECK_EQ(r.midiNote, 127);
	CHECK_NEAR(r.volts, 67.0 / 12.0, 1e-5);
}

static int countDiff(const Pattern& a, const Pattern& b, int stageIndex) {
	const Stage& x = a.stage[stageIndex];
	const Stage& y = b.stage[stageIndex];
	return (x.pitch != y.pitch) + (x.velocity != y.velocity) + (x.mode != y.mode) + (x.len != y.len)
		+ (x.prob != y.prob) + (x.slide != y.slide) + (x.on != y.on) + (x.lock != y.lock);
}

static Pattern marked() {
	Pattern p;
	for (int i = 0; i < NUM_STAGES; i++) {
		p.stage[i].pitch = 30 + i;	// outside every random range
		p.stage[i].velocity = 0;	// outside the default velocity range
		p.stage[i].len = 8;
		p.stage[i].prob = 1;	// not one of 25 / 50 / 75 / 100
	}
	return p;
}

static void testTools() {
	printf("tools: Global, Mutate, Reset, dice, shift, lanes; locks and off stages\n");
	Rng rng;
	rng.seed(99);
	ToolRanges ranges;
	const unsigned all = PART_PITCH | PART_VELOCITY | PART_PROB | PART_SLIDE | PART_MODE | PART_LEN;

	// Global: every unlocked stage, off and beyond Length too; values inside the ranges and the scale.
	for (int round = 0; round < 50; round++) {
		Pattern p = marked();
		p.length = 6;
		p.scale = 1;	// C major
		p.stage[0].lock = true;
		p.stage[2].on = false;
		Pattern before = p;
		CHECK(toolGlobal(p, all, ranges, rng));
		CHECK_EQ(countDiff(p, before, 0), 0);
		for (int i = 1; i < NUM_STAGES; i++) {
			const Stage& s = p.stage[i];
			CHECK(s.pitch >= -24 && s.pitch <= 0);
			int pc = ((s.pitch % 12) + 12) % 12;
			CHECK(pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10);
			CHECK(s.velocity >= 1 && s.velocity <= 127);
			CHECK(s.prob == 25 || s.prob == 50 || s.prob == 75 || s.prob == 100);
			CHECK(s.mode >= MODE_REST && s.mode < MODE_COUNT);
			CHECK(s.len >= 1 && s.len <= 8);
			CHECK_EQ(s.on, i != 2);	// Global never touches on / off
		}
		CHECK_EQ(p.length, 6);
		CHECK(toolGlobal(p, PART_SEQLEN, ranges, rng));
		CHECK(p.length >= 1 && p.length <= 8);
	}
	// Every Mode comes up, slide about one in four.
	{
		int modes[MODE_COUNT] = {0};
		int slides = 0, n = 0;
		for (int round = 0; round < 400; round++) {
			Pattern p;
			toolGlobal(p, PART_MODE | PART_SLIDE, ranges, rng);
			for (int i = 0; i < NUM_STAGES; i++) {
				modes[p.stage[i].mode]++;
				slides += p.stage[i].slide;
				n++;
			}
		}
		for (int m = 0; m < MODE_COUNT; m++)
			CHECK(modes[m] > 0);
		CHECK(slides > n / 5 && slides < n * 3 / 10);
	}
	// Pitch pool: Off gives every semitone of 36..60; the knob span is -36..36.
	{
		Pattern p;
		int pool[80];
		CHECK_EQ(randomPitchPool(p, ranges, pool), 25);
		CHECK_EQ(pool[0], -24);
		CHECK_EQ(pool[24], 0);
		ToolRanges wide;
		wide.pitchLo = 0;
		wide.pitchHi = 127;
		CHECK_EQ(randomPitchPool(p, wide, pool), 73);
		p.scale = 18;	// Rast: one entry per played pitch, kept on the nearest knob
		p.root = 2;
		int n = randomPitchPool(p, ranges, pool);
		CHECK(n > 0 && n < 25);
	}

	// Mutate: exactly one eligible stage changes; none eligible or no part: nothing.
	for (int round = 0; round < 50; round++) {
		Pattern p = marked();
		p.length = 5;
		p.stage[1].lock = true;
		p.stage[3].on = false;
		Pattern before = p;
		CHECK(toolMutate(p, PART_PITCH | PART_VELOCITY | PART_SEQLEN, ranges, rng));
		int changed = 0;
		for (int i = 0; i < NUM_STAGES; i++) {
			int d = countDiff(p, before, i);
			if (d) {
				changed++;
				CHECK(i == 0 || i == 2 || i == 4);
			}
		}
		CHECK_EQ(changed, 1);
		CHECK_EQ(p.length, 5);
	}
	{
		Pattern p;
		for (int i = 0; i < NUM_STAGES; i++)
			p.stage[i].lock = true;
		CHECK(!toolMutate(p, all, ranges, rng));
		Pattern q;
		CHECK(!toolMutate(q, PART_SEQLEN, ranges, rng));
	}

	// Reset: every unlocked stage to the defaults, locked kept, Length and settings kept.
	{
		Pattern p = marked();
		p.length = 3;
		p.transpose = 5;
		p.stage[4].lock = true;
		p.stage[6].on = false;
		p.stage[6].slide = true;
		Pattern before = p;
		CHECK(toolReset(p));
		Pattern fresh;
		for (int i = 0; i < NUM_STAGES; i++) {
			if (i == 4)
				CHECK_EQ(countDiff(p, before, i), 0);
			else
				CHECK_EQ(countDiff(p, fresh, i), 0);
		}
		CHECK_EQ(p.length, 3);
		CHECK_EQ(p.transpose, 5);
	}

	// Die: one row, stages in use, locks kept.
	{
		Pattern p = marked();
		p.length = 4;
		p.stage[0].lock = true;
		p.stage[2].on = false;
		Pattern before = p;
		CHECK(toolDie(p, PART_VELOCITY, ranges, rng));
		CHECK_EQ(countDiff(p, before, 0), 0);
		for (int i = 1; i < NUM_STAGES; i++) {
			CHECK(p.stage[i].velocity >= 1);	// beyond Length too
			CHECK_EQ(p.stage[i].pitch, before.stage[i].pitch);
		}
		CHECK(!toolDie(p, PART_SEQLEN, ranges, rng));
	}

	// Shift with Length 5: rotation inside 1..5, lock and on travel, 6..8 stay, lanes stay.
	{
		Pattern p = marked();
		p.length = 5;
		p.stage[1].lock = true;
		p.stage[3].on = false;
		p.lane[0].value[0] = 40.f;
		Pattern before = p;
		CHECK(toolShift(p, 1));
		for (int j = 0; j < 5; j++)
			CHECK_EQ(p.stage[j].pitch, before.stage[(j + 4) % 5].pitch);
		CHECK(p.stage[2].lock && !p.stage[1].lock);
		CHECK(!p.stage[4].on && p.stage[3].on);
		for (int j = 5; j < NUM_STAGES; j++)
			CHECK_EQ(countDiff(p, before, j), 0);
		CHECK_NEAR(p.lane[0].value[0], 40.0, 1e-9);
		CHECK(toolShift(p, -1));
		for (int j = 0; j < NUM_STAGES; j++)
			CHECK_EQ(countDiff(p, before, j), 0);
		p.length = 1;
		CHECK(!toolShift(p, 1));
		Pattern flat;
		flat.length = 4;
		CHECK(!toolShift(flat, -1));	// every value equal: nothing moves
	}

	// Lanes: random in whole percent on unlocked stages in use, mutate one stage, reset, shift.
	{
		Pattern p;
		p.length = 6;
		p.stage[2].lock = true;
		p.stage[4].on = false;
		for (int l = 0; l < NUM_LANES; l++)
			for (int i = 0; i < NUM_STAGES; i++)
				p.lane[l].value[i] = 0.5f;
		CHECK(toolLaneRandom(p, 1, rng));
		for (int i = 0; i < NUM_STAGES; i++) {
			CHECK_NEAR(p.lane[0].value[i], 0.5, 1e-9);
			CHECK_NEAR(p.lane[2].value[i], 0.5, 1e-9);
			float v = p.lane[1].value[i];
			if (i == 2)
				CHECK_NEAR(v, 0.5, 1e-9);
			else
				CHECK(v >= 0.f && v <= 100.f && v == floorf(v));
		}
		CHECK(toolLaneRandom(p, -1, rng));
		CHECK(p.lane[0].value[0] != 0.5f || p.lane[0].value[1] != 0.5f);
		Pattern before = p;
		CHECK(toolLaneMutate(p, rng));
		int stagesChanged = 0;
		for (int i = 0; i < NUM_STAGES; i++) {
			bool any = false;
			for (int l = 0; l < NUM_LANES; l++)
				any = any || p.lane[l].value[i] != before.lane[l].value[i];
			if (any) {
				stagesChanged++;
				CHECK(i == 0 || i == 1 || i == 3 || i == 4 || i == 5);	// off stage 5 can be picked
			}
		}
		CHECK(stagesChanged <= 1);
		p.lane[1].min = 20.f;
		p.lane[1].max = 60.f;
		p.lane[1].smooth = 250.f;
		CHECK(toolLaneReset(p));
		for (int l = 0; l < NUM_LANES; l++) {
			CHECK_NEAR(p.lane[l].min, 0.0, 1e-9);
			CHECK_NEAR(p.lane[l].max, 100.0, 1e-9);
			CHECK_NEAR(p.lane[l].smooth, 0.0, 1e-9);
			for (int i = 0; i < NUM_STAGES; i++)
				if (i != 2)
					CHECK_NEAR(p.lane[l].value[i], 0.0, 1e-9);
		}
		CHECK_NEAR(p.lane[0].value[2], before.lane[0].value[2], 1e-9);
		Pattern s;
		s.length = 3;
		s.lane[2].value[0] = 10.f;
		s.lane[2].value[1] = 20.f;
		s.lane[2].value[2] = 30.f;
		s.lane[2].value[3] = 99.f;
		CHECK(toolLaneShift(s, 2, -1));
		CHECK_NEAR(s.lane[2].value[0], 20.0, 1e-9);
		CHECK_NEAR(s.lane[2].value[1], 30.0, 1e-9);
		CHECK_NEAR(s.lane[2].value[2], 10.0, 1e-9);
		CHECK_NEAR(s.lane[2].value[3], 99.0, 1e-9);
		CHECK(!toolLaneShift(s, 0, 1));	// lane A all zero: nothing moves
	}
}

// Locks: no tool but Shift ever changes a locked stage, whatever is ticked.
static void testLocks() {
	printf("locks: Global, Mutate, Reset and the dice never change a locked stage\n");
	Rng rng;
	rng.seed(7);
	ToolRanges ranges;
	const unsigned all = PART_PITCH | PART_VELOCITY | PART_PROB | PART_SLIDE | PART_MODE | PART_LEN | PART_SEQLEN;
	for (int round = 0; round < 200; round++) {
		Pattern p = marked();
		p.length = 1 + rng.below(8);
		p.scale = rng.below(3) - 1;
		for (int i = 0; i < NUM_STAGES; i++) {
			p.stage[i].lock = rng.below(2) == 0;
			p.stage[i].on = rng.below(4) != 0;
		}
		Pattern before = p;
		switch (round % 5) {
			case 0: toolGlobal(p, all, ranges, rng); break;
			case 1: toolMutate(p, all, ranges, rng); break;
			case 2: toolReset(p); break;
			case 3: toolDie(p, PART_PITCH, ranges, rng); break;
			default: toolDie(p, PART_VELOCITY, ranges, rng); break;
		}
		for (int i = 0; i < NUM_STAGES; i++)
			if (before.stage[i].lock)
				CHECK_EQ(countDiff(p, before, i), 0);
	}
}

static void testLanes() {
	printf("lanes: value at every stage start, Min / Max, turned over\n");
	Pattern p;
	p.length = 4;
	setStage(p, 2, MODE_REST, 2);
	p.lane[0].value[0] = 10.f;
	p.lane[0].value[1] = 50.f;
	p.lane[1].min = 20.f;
	p.lane[1].max = 60.f;
	p.lane[1].value[1] = 50.f;
	p.lane[2].min = 100.f;
	p.lane[2].max = 0.f;
	p.lane[2].value[2] = 25.f;
	Engine e;
	StepResult r[6];
	run(e, p, 6, r);
	CHECK_NEAR(r[0].lane[0], 0.1, 1e-6);
	CHECK_NEAR(r[0].lane[1], 0.2, 1e-6);
	CHECK_NEAR(r[1].lane[0], 0.5, 1e-6);	// rest stage start sends
	CHECK_NEAR(r[1].lane[1], 0.4, 1e-6);
	CHECK(!r[2].stageStart);
	CHECK_NEAR(r[3].lane[2], 0.75, 1e-6);
}

int main() {
	testModes();
	testDirections();
	testOneStage();
	testRandom();
	testRestart();
	testOffStages();
	testEdits();
	testProbability();
	testSlide();
	testScales();
	testTools();
	testLocks();
	testLanes();
	printf("unit: %d checks, %d failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
