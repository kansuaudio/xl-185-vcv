// CPU benchmark of the core: `make bench`, not part of `make test`.
// A 120 BPM 16th clock at 48 kHz drives the Player for 60 seconds of samples with a busy pattern
// (every stage repeating, slides, probability, swing, a microtonal scale), and three lane outputs
// take a new value at every stage start. Prints nanoseconds per sample and the share of one
// 48 kHz sample period. The Rack layer around it (param reads, port writes, triggers) is not timed.
#include "../core/Player.hpp"
#include "../core/LaneOut.hpp"
#include "../core/Scales.hpp"
#include <chrono>
#include <stdio.h>

using namespace xl;

static const double RATE = 48000.0;
static const int SECONDS = 60;
static const int PERIOD = 6000;	// samples per 16th at 120 BPM and 48 kHz
static const int UPDATE_DIV = 32;	// as the module: settings and STAGE CV every 32 samples

static Pattern busyPattern() {
	Pattern p;
	const int modes[8] = {MODE_ALL, MODE_HALF, MODE_ONCE, MODE_THIRD, MODE_ALL, MODE_HOLD, MODE_QUARTER, MODE_ALL};
	const int lens[8] = {2, 4, 1, 3, 2, 3, 4, 1};
	for (int i = 0; i < NUM_STAGES; i++) {
		Stage& s = p.stage[i];
		s.pitch = -12 + 5 * i;
		s.velocity = 60 + 8 * i;
		s.mode = modes[i];
		s.len = lens[i];
		s.prob = (i % 2) ? 70 : 100;
		s.slide = (i % 3) == 1;
		for (int l = 0; l < NUM_LANES; l++)
			p.lane[l].value[i] = (float) ((i * 37 + l * 13) % 101);
	}
	p.direction = DIR_PINGPONG;
	p.scale = SCALE_FIRST_MICRO;	// Rast, exact cents
	p.root = 2;
	return p;
}

struct Result {
	double ns;
	double sink;
};

// One run; withUpdate adds the module's 32-sample STAGE CV work (8 scale snaps).
static Result run(bool withLanes, bool withUpdate) {
	Pattern p = busyPattern();
	Player pl;
	pl.seed(7);
	PlayerIn in;
	in.swing = 50.f;
	in.gate = 60.f;
	in.glide = 60.f;
	PlayerOut out;
	LaneOut lanes[NUM_LANES];
	const float smooth[NUM_LANES] = {0.f, 40.f, 400.f};
	const float dt = (float) (1.0 / RATE);
	const long long total = (long long) (RATE * SECONDS);
	double sink = 0.0;

	auto t0 = std::chrono::steady_clock::now();
	for (long long n = 0; n < total; n++) {
		int ph = (int) (n % PERIOD);
		in.clock = ph == 0;	// rising edge, as the module's trigger hands it on
		in.reset = n == 0;
		pl.process(p, in, dt, out);
		if (withLanes) {
			if (out.stageStart && out.stage >= 1) {
				for (int l = 0; l < NUM_LANES; l++) {
					const Lane& ln = p.lane[l];
					lanes[l].start(LaneOut::target(ln.value[out.stage - 1], ln.min, ln.max), smooth[l]);
				}
			}
			for (int l = 0; l < NUM_LANES; l++)
				sink += lanes[l].process(dt);
		}
		if (withUpdate && n % UPDATE_DIV == 0) {
			for (int i = 0; i < NUM_STAGES; i++)
				sink += snapCents(60 + p.stage[i].pitch, p.root, p.scale);
		}
		sink += out.vOct + out.velocity + (out.gate ? 1.0 : 0.0);
	}
	auto t1 = std::chrono::steady_clock::now();
	Result r;
	r.ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / (double) total;
	r.sink = sink;
	return r;
}

static void report(const char* name, bool withLanes, bool withUpdate) {
	// Best of three, after one warm-up run.
	run(withLanes, withUpdate);
	Result best = run(withLanes, withUpdate);
	for (int k = 0; k < 2; k++) {
		Result r = run(withLanes, withUpdate);
		if (r.ns < best.ns)
			best = r;
	}
	double periodNs = 1e9 / RATE;
	printf("%-40s %7.2f ns per sample  %6.3f %% of a 48 kHz sample period  (check %.1f)\n", name, best.ns,
		100.0 * best.ns / periodNs, best.sink);
}

int main() {
	printf("120 BPM 16ths at 48 kHz, %d s of samples, best of three\n", SECONDS);
	report("Player", false, false);
	report("Player + three lanes", true, false);
	report("Player + three lanes + STAGE CV update", true, true);
	return 0;
}
