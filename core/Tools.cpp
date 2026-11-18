#include "Tools.hpp"
#include "Scales.hpp"
#include <math.h>

namespace xl {

enum { POOL_LO = 24, POOL_HI = 96, POOL_MAX = POOL_HI - POOL_LO + 1 };

static const int PROB_STEPS[4] = {25, 50, 75, 100};

int randomPitchPool(const Pattern& p, const ToolRanges& r, int* out) {
	double cents[POOL_MAX];
	double dist[POOL_MAX];
	int knob[POOL_MAX];
	int n = 0;
	// One entry per played pitch, kept with the knob value nearest to it.
	for (int s = POOL_LO; s <= POOL_HI; s++) {
		double c = snapCents(s, p.root, p.scale);
		double d = fabs(c - s * 100.0);
		int j = 0;
		while (j < n && fabs(cents[j] - c) > 1e-3)
			j++;
		if (j == n) {
			cents[n] = c;
			dist[n] = d;
			knob[n] = s;
			n++;
		}
		else if (d < dist[j]) {
			dist[j] = d;
			knob[j] = s;
		}
	}
	int lo = r.pitchLo < r.pitchHi ? r.pitchLo : r.pitchHi;
	int hi = r.pitchLo < r.pitchHi ? r.pitchHi : r.pitchLo;
	int count = 0;
	for (int j = 0; j < n; j++) {
		if (cents[j] >= lo * 100.0 - 0.5 && cents[j] <= hi * 100.0 + 0.5)
			out[count++] = knob[j] - 60;
	}
	// Ascending, for a stable order.
	for (int a = 1; a < count; a++) {
		int v = out[a];
		int b = a - 1;
		while (b >= 0 && out[b] > v) {
			out[b + 1] = out[b];
			b--;
		}
		out[b + 1] = v;
	}
	return count;
}

static int randomVelocity(const ToolRanges& r, Rng& rng) {
	int a = r.velLo < r.velHi ? r.velLo : r.velHi;
	int b = r.velLo < r.velHi ? r.velHi : r.velLo;
	if (a < 0)
		a = 0;
	if (b > 127)
		b = 127;
	if (b < a)
		b = a;
	return a + rng.below(b - a + 1);
}

// XL picks rest, once, repeat or hold evenly; a repeat gets an interval of 1..4 steps.
static int randomMode(Rng& rng) {
	static const int REPEATS[4] = {MODE_ALL, MODE_HALF, MODE_THIRD, MODE_QUARTER};
	switch (rng.below(4)) {
		case 0: return MODE_REST;
		case 1: return MODE_ONCE;
		case 2: return REPEATS[rng.below(4)];
		default: return MODE_HOLD;
	}
}

// New values for the ticked parts of one stage. pool / poolCount: the pitch pool.
static void randomizeStage(Stage& s, unsigned parts, const ToolRanges& r, const int* pool, int poolCount, Rng& rng) {
	if ((parts & PART_PITCH) && poolCount > 0)
		s.pitch = pool[rng.below(poolCount)];
	if (parts & PART_VELOCITY)
		s.velocity = randomVelocity(r, rng);
	if (parts & PART_MODE)
		s.mode = randomMode(rng);
	if (parts & PART_LEN)
		s.len = 1 + rng.below(8);
	if (parts & PART_PROB)
		s.prob = PROB_STEPS[rng.below(4)];
	if (parts & PART_SLIDE)
		s.slide = rng.below(4) == 0;	// about one stage in four, as XL
}

static const unsigned STAGE_PARTS = PART_PITCH | PART_VELOCITY | PART_PROB | PART_SLIDE | PART_MODE | PART_LEN;

bool toolGlobal(Pattern& p, unsigned parts, const ToolRanges& r, Rng& rng) {
	int pool[POOL_MAX];
	int poolCount = randomPitchPool(p, r, pool);
	bool acted = false;
	if (parts & STAGE_PARTS) {
		for (int i = 0; i < NUM_STAGES; i++) {
			if (p.stage[i].lock)
				continue;
			randomizeStage(p.stage[i], parts, r, pool, poolCount, rng);
			acted = true;
		}
	}
	if (parts & PART_SEQLEN) {
		p.length = 1 + rng.below(NUM_STAGES);
		acted = true;
	}
	return acted;
}

bool toolMutate(Pattern& p, unsigned parts, const ToolRanges& r, Rng& rng) {
	if (!(parts & STAGE_PARTS))
		return false;
	int list[NUM_STAGES];
	int n = 0;
	for (int i = 0; i < NUM_STAGES; i++)
		if (p.inLoop(i) && p.stage[i].on && !p.stage[i].lock)
			list[n++] = i;
	if (n == 0)
		return false;
	int i = list[rng.below(n)];
	int pool[POOL_MAX];
	int poolCount = randomPitchPool(p, r, pool);
	randomizeStage(p.stage[i], parts & STAGE_PARTS, r, pool, poolCount, rng);
	return true;
}

bool toolReset(Pattern& p) {
	bool acted = false;
	for (int i = 0; i < NUM_STAGES; i++) {
		if (p.stage[i].lock)
			continue;
		stageDefaults(p.stage[i]);
		acted = true;
	}
	return acted;
}

bool toolDie(Pattern& p, unsigned part, const ToolRanges& r, Rng& rng) {
	part &= STAGE_PARTS;
	if (!part)
		return false;
	int pool[POOL_MAX];
	int poolCount = randomPitchPool(p, r, pool);
	bool acted = false;
	for (int i = 0; i < NUM_STAGES; i++) {
		if (p.stage[i].lock)
			continue;
		randomizeStage(p.stage[i], part, r, pool, poolCount, rng);
		acted = true;
	}
	return acted;
}

static bool sameStage(const Stage& a, const Stage& b) {
	return a.pitch == b.pitch && a.velocity == b.velocity && a.mode == b.mode && a.len == b.len
		&& a.prob == b.prob && a.slide == b.slide && a.on == b.on && a.lock == b.lock;
}

static int clampLength(const Pattern& p) {
	return p.length < 1 ? 1 : (p.length > NUM_STAGES ? NUM_STAGES : p.length);
}

bool toolShift(Pattern& p, int dir) {
	int n = clampLength(p);
	if (n < 2)
		return false;
	Stage old[NUM_STAGES];
	for (int j = 0; j < n; j++)
		old[j] = p.stage[j];
	bool moved = false;
	for (int j = 0; j < n; j++) {
		// Right: stage j takes stage j - 1; left: j + 1.
		int from = dir < 0 ? (j + 1) % n : (j - 1 + n) % n;
		if (!sameStage(old[from], old[j]))
			moved = true;
		p.stage[j] = old[from];
	}
	return moved;
}

bool toolLaneRandom(Pattern& p, int lane, Rng& rng) {
	bool acted = false;
	for (int l = 0; l < NUM_LANES; l++) {
		if (lane >= 0 && l != lane)
			continue;
		for (int i = 0; i < NUM_STAGES; i++) {
			if (p.stage[i].lock)
				continue;
			p.lane[l].value[i] = (float) rng.below(101);	// whole percent, as XL
			acted = true;
		}
	}
	return acted;
}

bool toolLaneMutate(Pattern& p, Rng& rng) {
	int list[NUM_STAGES];
	int n = 0;
	for (int i = 0; i < NUM_STAGES; i++)
		if (p.inLoop(i) && !p.stage[i].lock)
			list[n++] = i;
	if (n == 0)
		return false;
	int i = list[rng.below(n)];
	for (int l = 0; l < NUM_LANES; l++)
		p.lane[l].value[i] = (float) rng.below(101);
	return true;
}

bool toolLaneReset(Pattern& p) {
	for (int l = 0; l < NUM_LANES; l++) {
		for (int i = 0; i < NUM_STAGES; i++)
			if (!p.stage[i].lock)
				p.lane[l].value[i] = 0.f;
		p.lane[l].min = 0.f;
		p.lane[l].max = 100.f;
		p.lane[l].smooth = 0.f;
	}
	return true;
}

bool toolLaneShift(Pattern& p, int lane, int dir) {
	if (lane < 0 || lane >= NUM_LANES)
		return false;
	int n = clampLength(p);
	if (n < 2)
		return false;
	float old[NUM_STAGES];
	for (int j = 0; j < n; j++)
		old[j] = p.lane[lane].value[j];
	bool moved = false;
	for (int j = 0; j < n; j++) {
		int from = dir < 0 ? (j + 1) % n : (j - 1 + n) % n;
		if (fabs(old[from] - old[j]) >= 1e-6)
			moved = true;
		p.lane[lane].value[j] = old[from];
	}
	return moved;
}

} // namespace xl
