#include "Player.hpp"

namespace xl {

static const double TRIG = 0.001;	// 1 ms: triggers, re-attack gap, reset window
static const double EPS = 1e-7;	// a timer at or below this has run out
static const double START_PERIOD = 0.125;	// a 16th at 120 BPM until two clocks are seen
static const double MAX_PERIOD = 2.0;	// a longer measure is a stopped clock, not a tempo

Player::Player() {
	reset();
}

void Player::reset() {
	engine.reset();
	engine.endNote();
	P = START_PERIOD;
	sinceClock = 0.0;
	haveClock = false;
	resetLeft = 0.0;
	count = 0;
	pending = false;
	pendingLeft = 0.0;
	lastRun = true;
	lastGate = false;
	ran = false;
	started = false;
	noteOn = false;
	noteSlide = false;
	noteStage = 0;
	noteLeft = 0.0;
	gapLeft = 0.0;
	volts = 0.f;
	glideFrom = 0.f;
	glideTo = 0.f;
	glideTime = 0.0;
	glideAt = 0.0;
	velocity = 0.f;
	stage = 0;
	stageGapLeft = 0.0;
	stageTrigLeft = 0.0;
	eocLeft = 0.0;
}

void Player::runStep(const Pattern& p, const PlayerIn& in) {
	StepResult r;
	engine.step(p, in.transpose, r);
	ran = true;
	stage = r.stage;
	if (r.stage > 0 && r.stageStart) {
		stageTrigLeft = TRIG;
		stageGapLeft = TRIG;
		started = true;
	}
	if (r.eoc)
		eocLeft = TRIG;
	if (!r.note)
		return;

	double steps = r.length;
	double len;
	if (r.slide) {
		len = steps * P;
	}
	else {
		len = steps * P * in.gate / 100.0;
		double cap = steps * P - TRIG;
		if (len > cap)
			len = cap;
		if (len < TRIG)
			len = TRIG;
	}
	velocity = r.velocity / 127.f * 10.f;

	if (noteOn && r.tie) {
		// Tie: gate and pitch stay.
	}
	else if (noteOn && noteSlide && r.volts != glideTo) {
		// Legato: the gate stays, the pitch glides from where it is.
		glideFrom = volts;
		glideTo = r.volts;
		glideTime = in.glide * 0.001;
		glideAt = 0.0;
	}
	else {
		// New attack; a gate that is or just was high drops for 1 ms first.
		if (noteOn || lastGate)
			gapLeft = TRIG;
		glideFrom = glideTo = r.volts;
		glideTime = 0.0;
		glideAt = 0.0;
	}
	noteOn = true;
	noteSlide = r.slide;
	noteStage = r.stage;
	noteLeft = len;
}

void Player::process(const Pattern& p, const PlayerIn& in, float sampleTime, PlayerOut& out) {
	double dt = sampleTime;
	ran = false;
	started = false;

	// Time passes.
	sinceClock += dt;
	if (resetLeft > 0.0)
		resetLeft -= dt;
	if (gapLeft > 0.0)
		gapLeft -= dt;
	if (stageGapLeft > 0.0)
		stageGapLeft -= dt;
	if (stageTrigLeft > 0.0)
		stageTrigLeft -= dt;
	if (eocLeft > 0.0)
		eocLeft -= dt;
	if (glideTime > 0.0 && glideAt < glideTime)
		glideAt += dt;
	if (noteOn) {
		noteLeft -= dt;
		if (noteLeft <= EPS)
			noteOn = false;
	}
	bool pendingDue = false;
	if (pending) {
		pendingLeft -= dt;
		pendingDue = pendingLeft <= EPS;
	}

	// RUN low: the note ends, a waiting step is dropped and plays with the next clock.
	if (!in.run && lastRun) {
		noteOn = false;
		gapLeft = 0.0;
		engine.endNote();
		if (pending) {
			pending = false;
			pendingDue = false;
			count--;
		}
	}
	lastRun = in.run;

	if (pendingDue) {
		pending = false;
		runStep(p, in);
	}

	// Reset: running, step 1 plays now (never swung); stopped, it waits for the first clock.
	if (in.reset) {
		engine.reset();
		count = 0;
		pending = false;
		resetLeft = TRIG;
		stage = 0;
		if (in.run) {
			count = 1;
			runStep(p, in);
		}
	}

	// A clock in the reset window does not step but still measures the period.
	if (in.clock && in.run && resetLeft > EPS) {
		if (haveClock && sinceClock <= MAX_PERIOD)
			P = sinceClock;
		haveClock = true;
		sinceClock = 0.0;
	}
	else if (in.clock && in.run) {
		// A step still waiting for its swing runs first.
		if (pending) {
			pending = false;
			runStep(p, in);
		}
		if (haveClock && sinceClock <= MAX_PERIOD)
			P = sinceClock;
		haveClock = true;
		sinceClock = 0.0;
		count++;
		if (in.swing > 0.f && count % 2 == 0) {
			pending = true;
			pendingLeft = 0.5 * P * in.swing / 100.0;
		}
		else {
			runStep(p, in);
		}
	}

	// Pitch.
	if (glideTime > 0.0 && glideAt < glideTime)
		volts = glideFrom + (float) ((glideTo - glideFrom) * (glideAt / glideTime));
	else
		volts = glideTo;

	bool gate = noteOn && gapLeft <= EPS;
	lastGate = gate;
	out.vOct = volts;
	out.gate = gate;
	out.velocity = velocity;
	out.slide = gate && noteSlide;
	out.stageTrig = stageTrigLeft > EPS;
	out.eoc = eocLeft > EPS;
	out.stage = stage;
	out.stepRan = ran;
	out.stageStart = started;
	for (int i = 0; i < NUM_STAGES; i++) {
		bool here = stage == i + 1;
		switch (in.stageGateMode) {
			case STAGE_GATE_STAGE:
				out.stageGate[i] = here && in.run && stageGapLeft <= EPS;
				break;
			case STAGE_GATE_TRIGGER:
				out.stageGate[i] = here && stageTrigLeft > EPS;
				break;
			default:
				out.stageGate[i] = gate && noteStage == i + 1;
				break;
		}
	}
}

} // namespace xl
