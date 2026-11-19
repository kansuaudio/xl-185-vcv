// Player tests: sample streams through the time layer. Built and run by `make test`.
#include "check.hpp"
#include "../core/Player.hpp"
#include "../core/LaneOut.hpp"
#include <math.h>
#include <vector>

using namespace xl;

int g_checks = 0;
int g_failures = 0;

struct Rise {
	long long t;
	int stage;
	float vOct;
};

// Feeds a Player sample by sample and keeps the output of every sample and the gate edges.
struct Sim {
	double sr;
	float dt;
	Player pl;
	Pattern p;
	PlayerIn in;
	PlayerOut o;
	long long t;
	std::vector<PlayerOut> trace;
	std::vector<Rise> rises;
	std::vector<long long> falls;

	Sim(double rate) : sr(rate), dt((float) (1.0 / rate)), t(0) {
		pl.seed(1);
	}

	void tick(bool clk = false, bool rst = false) {
		in.clock = clk;
		in.reset = rst;
		pl.process(p, in, dt, o);
		bool was = trace.empty() ? false : trace.back().gate;
		if (o.gate && !was) {
			Rise r = {t, o.stage, o.vOct};
			rises.push_back(r);
		}
		if (!o.gate && was)
			falls.push_back(t);
		trace.push_back(o);
		t++;
	}

	void idle(long long n) {
		for (long long i = 0; i < n; i++)
			tick();
	}

	// n clocks, per samples apart, the first on the current sample.
	void clocks(int n, long long per) {
		for (int k = 0; k < n; k++) {
			tick(true);
			idle(per - 1);
		}
	}

	const PlayerOut& at(long long i) const { return trace[i]; }

	// Samples a flag stays high from sample i on.
	long long highFor(long long i, bool PlayerOut::*f) const {
		long long n = 0;
		while (i + n < (long long) trace.size() && trace[i + n].*f)
			n++;
		return n;
	}
};

static void distinctPitches(Pattern& p) {
	for (int i = 0; i < NUM_STAGES; i++)
		p.stage[i].pitch = i;
}

static void testGateLength() {
	printf("gate length at 50 %% and 100 %%, one rising edge per note\n");
	Sim a(48000);
	a.in.gate = 50.f;
	a.clocks(4, 24000);
	CHECK_EQ(a.rises.size(), 4);
	CHECK_EQ(a.rises[0].t, 0);
	CHECK_EQ(a.falls[0], 3000);	// first note: P = 125 ms before two clocks
	CHECK_EQ(a.rises[1].t, 24000);
	CHECK_NEAR(a.falls[1] - a.rises[1].t, 12000, 1);
	CHECK_NEAR(a.falls[2] - a.rises[2].t, 12000, 1);

	Sim b(48000);
	b.in.gate = 100.f;
	b.clocks(4, 24000);
	CHECK_EQ(b.rises.size(), 4);
	for (int i = 1; i < 3; i++) {
		CHECK_NEAR(b.falls[i] - b.rises[i].t, 24000 - 48, 1);
		CHECK_NEAR(b.rises[i + 1].t - b.falls[i], 48, 1);
	}
}

static void testHold() {
	printf("hold: Len x P x Gate, capped 1 ms before the next note\n");
	for (int g = 0; g < 2; g++) {
		Sim s(48000);
		s.p.length = 1;
		s.p.stage[0].mode = MODE_HOLD;
		s.p.stage[0].len = 4;
		s.in.gate = g ? 100.f : 50.f;
		s.clocks(9, 24000);
		CHECK_EQ(s.rises.size(), 3);
		CHECK_EQ(s.rises[1].t, 96000);
		CHECK_NEAR(s.falls[1] - s.rises[1].t, g ? 96000 - 48 : 48000, 1);
	}
}

// Stage 2 slides into stage 3; stage 3 is set by the caller.
static void slideSim(Sim& s) {
	s.p.length = 4;
	s.p.stage[1].slide = true;
	s.in.gate = 50.f;
	s.in.glide = 100.f;
	s.clocks(4, 24000);
}

static void testSlide() {
	printf("slide: legato glide, tie, slide into a rest\n");
	{
		Sim s(48000);
		s.p.stage[2].pitch = 12;
		slideSim(s);
		CHECK_EQ(s.rises.size(), 3);	// stage 1, stage 2, stage 4
		CHECK_EQ(s.rises[1].t, 24000);
		CHECK_EQ(s.rises[2].t, 72000);
		bool high = true;
		for (long long i = 24000; i < 60000 - 1; i++)
			high = high && s.at(i).gate;
		CHECK(high);
		CHECK_NEAR(s.falls[1], 60000, 1);
		CHECK(s.at(47999).slide);
		CHECK(!s.at(48001).slide);
		CHECK_NEAR(s.at(48000).vOct, 0.0, 1e-6);
		CHECK_NEAR(s.at(48000 + 1200).vOct, 0.25, 1e-3);
		CHECK_NEAR(s.at(48000 + 2400).vOct, 0.5, 1e-3);
		CHECK_NEAR(s.at(48000 + 3600).vOct, 0.75, 1e-3);
		CHECK(s.at(48000 + 4790).vOct < 1.f);
		CHECK_NEAR(s.at(48000 + 4801).vOct, 1.0, 1e-6);
		CHECK_NEAR(s.at(55000).vOct, 1.0, 1e-6);
	}
	{
		Sim s(48000);
		s.p.stage[2].pitch = 12;
		s.p.stage[1].slide = true;
		s.p.length = 4;
		s.in.glide = 0.f;
		s.clocks(4, 24000);
		CHECK_NEAR(s.at(48000).vOct, 1.0, 1e-6);	// glide 0 jumps
		CHECK(s.at(48000).gate);
	}
	{
		Sim s(48000);
		slideSim(s);	// stage 3 has the same pitch: tie
		CHECK_EQ(s.rises.size(), 3);
		CHECK_EQ(s.rises[2].t, 72000);
		CHECK_NEAR(s.falls[1], 60000, 1);
	}
	{
		Sim s(48000);
		s.p.stage[2].mode = MODE_REST;
		slideSim(s);
		CHECK_EQ(s.rises.size(), 3);
		CHECK_NEAR(s.falls[1], 24000 + 42000, 1);	// slot + 0.75 steps
	}
}

static void testSwing() {
	printf("swing 50 and 100 delay the even steps by 0.5 x P x swing\n");
	for (int k = 0; k < 2; k++) {
		Sim s(48000);
		s.in.swing = k ? 100.f : 50.f;
		s.in.gate = 25.f;
		s.clocks(6, 24000);
		long long d = k ? 12000 : 6000;
		CHECK_EQ(s.rises.size(), 6);
		for (int i = 0; i < 6; i++)
			CHECK_NEAR(s.rises[i].t, i * 24000 + ((i % 2) ? d : 0), 1);
		CHECK_NEAR(s.falls[1] - s.rises[1].t, 6000, 1);	// not shortened
	}
}

static void testReset() {
	printf("reset: step 1 plays at once, clocks within 1 ms are ignored, stopped resets wait\n");
	// Reset alone mid-pattern, a note sounding.
	Sim s(48000);
	distinctPitches(s.p);
	s.in.gate = 100.f;
	s.clocks(2, 24000);
	s.tick(true);	// stage 3 at 48000
	s.idle(11999);
	size_t n = s.rises.size();
	s.tick(false, true);	// 60000
	CHECK_EQ(s.o.stage, 1);
	CHECK_NEAR(s.o.vOct, 0.0, 1e-6);
	CHECK(!s.o.gate);	// re-attack: 1 ms low
	s.idle(46);
	s.tick(true);	// 47 samples after the reset: ignored
	CHECK_EQ(s.o.stage, 1);
	s.idle(100);
	CHECK_EQ(s.rises.size(), n + 1);
	CHECK_EQ(s.rises.back().t, 60048);
	s.idle(72000 - s.t);
	s.tick(true);
	CHECK_EQ(s.o.stage, 2);

	// Reset together with a clock: stage 1 on time, the next clock is step 2 (swung).
	Sim c(48000);
	distinctPitches(c.p);
	c.in.swing = 50.f;
	c.in.gate = 25.f;
	c.clocks(2, 24000);
	c.tick(true, true);	// 48000
	CHECK_EQ(c.o.stage, 1);
	CHECK(c.o.gate);
	CHECK_NEAR(c.pl.period(), 0.5, 1e-6);	// the ignored clock still measures
	c.idle(23999);
	c.tick(true);	// 72000: step 2 waits 6000
	CHECK_EQ(c.o.stage, 1);
	c.idle(5999);
	c.tick();
	CHECK_EQ(c.t - 1, 78000);
	CHECK_EQ(c.o.stage, 2);

	// Reset while stopped: armed, the first clock after RUN high plays step 1.
	Sim r(48000);
	distinctPitches(r.p);
	r.clocks(3, 24000);
	r.in.run = false;
	r.tick();
	size_t m = r.rises.size();
	r.tick(false, true);
	CHECK_EQ(r.o.stage, 0);
	r.idle(1000);
	CHECK_EQ(r.rises.size(), m);
	r.in.run = true;
	r.idle(100);
	r.tick(true);
	CHECK_EQ(r.o.stage, 1);
	CHECK(r.o.gate);
}

static void testRun() {
	printf("run low ends the note and drops clocks, run high continues\n");
	Sim s(48000);
	distinctPitches(s.p);
	s.in.gate = 100.f;
	s.clocks(2, 24000);
	s.tick(true);	// stage 3
	s.idle(1000);
	CHECK(s.o.gate);
	s.in.run = false;
	s.tick();
	CHECK(!s.o.gate);
	size_t n = s.rises.size();
	s.idle(1000);
	s.clocks(3, 2000);
	CHECK_EQ(s.rises.size(), n);
	s.in.run = true;
	s.idle(100);
	s.tick(true);
	CHECK_EQ(s.o.stage, 4);
	CHECK_NEAR(s.o.vOct, 3.0 / 12.0, 1e-6);
	CHECK(s.o.gate);
}

static void testTempoJump() {
	printf("a clock before the swung step is due runs that step first\n");
	Sim s(48000);
	distinctPitches(s.p);
	s.in.swing = 100.f;
	s.in.gate = 25.f;
	s.tick(true);
	s.idle(23999);
	s.tick(true);	// step 2 waits until 36000
	s.idle(5999);
	CHECK_EQ(s.o.stage, 1);
	s.tick(true);	// 30000: step 2 then step 3
	CHECK_EQ(s.o.stage, 3);
	CHECK_NEAR(s.o.vOct, 2.0 / 12.0, 1e-6);
	CHECK_NEAR(s.pl.period(), 0.125, 1e-6);
	s.idle(5999);
	s.tick(true);	// 36000: step 4 waits 0.5 x 125 ms
	CHECK_EQ(s.o.stage, 3);
	s.idle(2999);
	CHECK_EQ(s.o.stage, 3);
	s.tick();
	CHECK_EQ(s.o.stage, 4);
	CHECK_EQ(s.t - 1, 39000);
}

static void testStageGates() {
	printf("stage gate modes: note, stage, trigger\n");
	for (int m = 0; m < STAGE_GATE_COUNT; m++) {
		Sim s(48000);
		s.p.length = 2;
		s.p.stage[0].len = 2;
		s.p.stage[1].len = 2;
		s.in.gate = 50.f;
		s.in.stageGateMode = m;
		s.clocks(4, 24000);
		if (m == STAGE_GATE_NOTE) {
			CHECK(s.at(48100).stageGate[1]);
			CHECK(!s.at(48100).stageGate[0]);
			CHECK(!s.at(48000 + 12100).stageGate[1]);
			CHECK(!s.at(30000).stageGate[0]);	// stage 1's note is over
		}
		else if (m == STAGE_GATE_STAGE) {
			CHECK(s.at(100).stageGate[0]);
			CHECK(s.at(30000).stageGate[0]);
			CHECK(!s.at(48010).stageGate[1]);	// 1 ms low at the stage start
			CHECK(s.at(48100).stageGate[1]);
			CHECK(!s.at(48100).stageGate[0]);
			CHECK(s.at(90000).stageGate[1]);
		}
		else {
			CHECK(s.at(48010).stageGate[1]);
			CHECK(!s.at(48100).stageGate[1]);
			CHECK(!s.at(24010).stageGate[0]);	// no stage start on step 2
		}
	}
	// One random stage: every pick is a stage start, the stage gate drops and rises again.
	Sim r(48000);
	r.p.length = 1;
	r.p.direction = DIR_RANDOM;
	r.in.stageGateMode = STAGE_GATE_STAGE;
	r.clocks(3, 24000);
	CHECK(r.at(23990).stageGate[0]);
	CHECK(!r.at(24010).stageGate[0]);
	CHECK(r.at(24100).stageGate[0]);
}

static void testTriggers() {
	printf("stage and EOC triggers last 1 ms\n");
	Sim s(48000);
	s.clocks(9, 24000);
	int stageTrigs = 0, eocs = 0;
	for (long long i = 0; i < s.t; i++) {
		bool st = s.at(i).stageTrig && (i == 0 || !s.at(i - 1).stageTrig);
		bool eo = s.at(i).eoc && (i == 0 || !s.at(i - 1).eoc);
		if (st) {
			stageTrigs++;
			CHECK_EQ(s.highFor(i, &PlayerOut::stageTrig), 48);
		}
		if (eo) {
			eocs++;
			CHECK_EQ(i, 8 * 24000);
			CHECK_EQ(s.highFor(i, &PlayerOut::eoc), 48);
		}
	}
	CHECK_EQ(stageTrigs, 9);
	CHECK_EQ(eocs, 1);
}

static void testSampleRates() {
	printf("44.1 kHz gives the same step structure as 48 kHz\n");
	Sim a(48000), b(44100);
	Sim* sims[2] = {&a, &b};
	for (int k = 0; k < 2; k++) {
		Sim& s = *sims[k];
		distinctPitches(s.p);
		s.p.stage[3].slide = true;
		s.p.stage[4].pitch = 7;
		s.in.swing = 50.f;
		s.in.gate = 50.f;
		s.clocks(12, (long long) (s.sr / 2));
	}
	CHECK_EQ(a.rises.size(), b.rises.size());
	CHECK_EQ(a.falls.size(), b.falls.size());
	for (size_t i = 0; i < a.rises.size() && i < b.rises.size(); i++) {
		CHECK_NEAR(a.rises[i].t / 48000.0, b.rises[i].t / 44100.0, 1.0 / 44100.0);
		CHECK_EQ(a.rises[i].stage, b.rises[i].stage);
		CHECK_NEAR(a.rises[i].vOct, b.rises[i].vOct, 1e-6);
	}
	for (size_t i = 0; i < a.falls.size() && i < b.falls.size(); i++)
		CHECK_NEAR(a.falls[i] / 48000.0, b.falls[i] / 44100.0, 2.0 / 44100.0);
	Sim c(44100);
	c.in.gate = 100.f;
	c.clocks(3, 22050);
	CHECK_NEAR(c.rises[2].t - c.falls[1], 44, 1);	// 1 ms gap
}

static void testStepRan() {
	printf("stepRan: once per step, on the sample the step runs (swung steps late, reset at once)\n");
	Sim s(48000);
	s.in.swing = 100.f;
	s.clocks(4, 4800);
	long long n = 0;
	for (size_t i = 0; i < s.trace.size(); i++)
		n += s.trace[i].stepRan;
	CHECK_EQ(n, 4);
	CHECK(s.at(0).stepRan);
	CHECK(!s.at(4800).stepRan);	// step 2 waits for its swing
	CHECK(s.at(4800 + 2400).stepRan);
	s.tick(false, true);
	CHECK(s.o.stepRan);	// reset while running plays step 1 now
	s.in.run = false;
	s.idle(10);
	s.tick(true);
	CHECK(!s.o.stepRan);
}

static void testAutoCount() {
	printf("auto mutate: after the last step of every Nth bar, counted from reset, kept across stop\n");
	Sim s(48000);
	AutoCount a;
	std::vector<int> fired;	// step numbers after which it fired
	int steps = 0;
	for (int k = 0; k < 20; k++) {
		s.tick(true);
		if (s.o.stepRan)
			steps++;
		if (a.process(false, s.in.run, s.o.stepRan, 2, 4))
			fired.push_back(steps);
		s.idle(99);
	}
	CHECK_EQ(fired.size(), 2u);
	CHECK_EQ(fired[0], 8);
	CHECK_EQ(fired[1], 16);
	// Reset restarts the count; the step it plays at once is step 1.
	fired.clear();
	steps = 0;
	s.tick(false, true);
	CHECK(!a.process(true, true, s.o.stepRan, 1, 4));
	CHECK_EQ(a.steps, 1);
	s.idle(100);
	for (int k = 0; k < 3; k++) {
		s.idle(100);
		s.tick(true);
		if (a.process(false, true, s.o.stepRan, 1, 4))
			fired.push_back(k);
	}
	CHECK_EQ(fired.size(), 1u);
	CHECK_EQ(fired[0], 2);
	// Stop and continue keep the count: the bar stays where it was.
	a.process(false, true, true, 1, 4);
	CHECK_EQ(a.steps, 5);
	for (int k = 0; k < 50; k++)
		CHECK(!a.process(false, false, false, 1, 4));
	CHECK_EQ(a.steps, 5);
	for (int k = 0; k < 2; k++)
		CHECK(!a.process(false, true, true, 1, 4));
	CHECK(a.process(false, true, true, 1, 4));
	CHECK_EQ(a.steps, 8);
	// Only Reset restarts it.
	a.process(false, true, true, 1, 4);
	a.process(true, true, false, 1, 4);
	CHECK_EQ(a.steps, 0);
	// Off: bars 0 never fires.
	CHECK(!a.process(false, true, true, 0, 16));
}

static void testStageStart() {
	printf("stageStart: on the sample a stage starts, not on the later steps of a long stage\n");
	Sim s(48000);
	s.p.length = 3;
	s.p.stage[1].len = 2;
	s.clocks(5, 4800);
	CHECK(s.at(0).stageStart);
	CHECK(s.at(4800).stageStart);
	CHECK(!s.at(9600).stageStart);	// stage 2, second step
	CHECK(s.at(14400).stageStart);
	CHECK(s.at(19200).stageStart);	// wrap to stage 1
	long long n = 0;
	for (size_t i = 0; i < s.trace.size(); i++)
		n += s.trace[i].stageStart;
	CHECK_EQ(n, 4);
	s.tick(false, true);
	CHECK(s.o.stageStart);	// reset while running starts stage 1 now
}

// Samples until the ramp is within eps of v.
static long long rampSamples(LaneOut& lo, float v, float dt) {
	long long n = 0;
	while (n < 1000000) {
		n++;
		if (fabsf(lo.process(dt) - v) < 1e-5f)
			break;
	}
	return n;
}

static void testLaneOut() {
	printf("lane out: stage start to target, Min / Max turned over, ramp time at 48 and 44.1 kHz, next start\n");
	CHECK_NEAR(LaneOut::target(0.f, 0.f, 100.f), 0.0, 1e-6);
	CHECK_NEAR(LaneOut::target(100.f, 0.f, 100.f), 10.0, 1e-6);
	CHECK_NEAR(LaneOut::target(50.f, 20.f, 60.f), 4.0, 1e-6);
	CHECK_NEAR(LaneOut::target(25.f, 100.f, 0.f), 7.5, 1e-6);	// Min above Max turns over
	CHECK_NEAR(LaneOut::target(0.f, 100.f, 0.f), 10.0, 1e-6);
	CHECK_NEAR(LaneOut::target(100.f, 100.f, 0.f), 0.0, 1e-6);
	// Min and Max run -100..100: lanes below zero and across it.
	CHECK_NEAR(LaneOut::target(0.f, -100.f, 100.f), -10.0, 1e-6);
	CHECK_NEAR(LaneOut::target(50.f, -100.f, 100.f), 0.0, 1e-6);
	CHECK_NEAR(LaneOut::target(100.f, -100.f, 100.f), 10.0, 1e-6);
	CHECK_NEAR(LaneOut::target(25.f, -50.f, 50.f), -2.5, 1e-6);
	CHECK_NEAR(LaneOut::target(100.f, -100.f, -20.f), -2.0, 1e-6);
	CHECK_NEAR(LaneOut::target(0.f, 100.f, -100.f), 10.0, 1e-6);	// turned over across zero
	// The same formula as the engine's lane values.
	{
		Pattern p;
		p.lane[0].min = 30.f;
		p.lane[0].max = 80.f;
		p.lane[0].value[0] = 40.f;
		p.lane[2].min = 90.f;
		p.lane[2].max = 10.f;
		p.lane[2].value[0] = 75.f;
		Engine e;
		StepResult r;
		e.step(p, 0, r);
		CHECK(r.stageStart);
		CHECK_NEAR(LaneOut::target(40.f, 30.f, 80.f), 10.0 * r.lane[0], 1e-5);
		CHECK_NEAR(LaneOut::target(75.f, 90.f, 10.f), 10.0 * r.lane[2], 1e-5);
	}
	// Smooth 0 jumps on the start.
	{
		LaneOut lo;
		lo.start(6.f, 0.f);
		CHECK_NEAR(lo.volts, 6.0, 1e-6);
		CHECK_NEAR(lo.process(1.f / 48000.f), 6.0, 1e-6);
	}
	// A ramp reaches the target in Smooth ms, linearly, at both rates.
	const double rates[2] = {48000.0, 44100.0};
	for (int k = 0; k < 2; k++) {
		float dt = (float) (1.0 / rates[k]);
		LaneOut lo;
		lo.start(10.f, 100.f);
		long long n = rampSamples(lo, 10.f, dt);
		CHECK_NEAR(n, rates[k] * 0.1, 1);
		LaneOut half;
		half.start(10.f, 100.f);
		for (long long i = 0; i < (long long) (rates[k] * 0.05); i++)
			half.process(dt);
		CHECK_NEAR(half.volts, 5.0, 0.01);
		// A new start mid-ramp goes from where the output is.
		half.start(0.f, 20.f);
		CHECK_NEAR(half.process(dt), 5.0, 0.01);
		n = rampSamples(half, 0.f, dt);
		CHECK_NEAR(n, rates[k] * 0.02, 1);
		CHECK_NEAR(half.process(dt), 0.0, 1e-6);	// stays
	}
	// A value, Min or Max change takes effect at the next stage start, as the Rack layer drives it.
	{
		Sim s(48000);
		s.p.length = 2;
		s.p.lane[0].value[0] = 50.f;
		s.p.lane[0].value[1] = 100.f;
		LaneOut lo;
		float v = 0.f;
		for (int c = 0; c < 4; c++) {
			for (int i = 0; i < 4800; i++) {
				s.tick(i == 0);
				if (s.o.stageStart) {
					const Lane& ln = s.p.lane[0];
					lo.start(LaneOut::target(ln.value[s.o.stage - 1], ln.min, ln.max), ln.smooth);
				}
				v = lo.process(s.dt);
				if (c == 0 && i == 10) {
					CHECK_NEAR(v, 5.0, 1e-6);
					s.p.lane[0].max = 50.f;	// mid-stage edit
				}
				if (c == 0 && i == 4799)
					CHECK_NEAR(v, 5.0, 1e-6);	// unchanged until the next start
				if (c == 1 && i == 0)
					CHECK_NEAR(v, 5.0, 1e-6);	// stage 2: 100 % of 0..50
				if (c == 2 && i == 0)
					CHECK_NEAR(v, 2.5, 1e-6);	// stage 1: 50 % of 0..50
			}
		}
		CHECK_NEAR(v, 5.0, 1e-6);
	}
}

int main() {
	testGateLength();
	testHold();
	testSlide();
	testSwing();
	testReset();
	testRun();
	testTempoJump();
	testStageGates();
	testTriggers();
	testSampleRates();
	testStepRan();
	testAutoCount();
	testStageStart();
	testLaneOut();
	printf("player: %d checks, %d failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
