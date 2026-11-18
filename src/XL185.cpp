#include "plugin.hpp"
#include "../core/Player.hpp"
#include "../core/Scales.hpp"
#include "../core/Tools.hpp"
#include "Widgets.hpp"
#include "Expander.hpp"
#include <atomic>

static const int RESTART_STEPS[5] = {0, 16, 32, 64, 128};
static const int UPDATE_DIV = 32;	// samples between pattern, settings and STAGE CV updates
static const int AUTO_BARS[3] = {4, 8, 16};

enum Tool {
	TOOL_GLOBAL,
	TOOL_MUTATE,
	TOOL_RESET,
	TOOL_PITCH_DIE,
	TOOL_VEL_DIE,
	TOOL_SHIFT_LEFT,
	TOOL_SHIFT_RIGHT,
	TOOL_COUNT
};

static const char* TOOL_NAMES[TOOL_COUNT] = {"XL-185 global", "XL-185 mutate", "XL-185 reset",
	"XL-185 pitch die", "XL-185 velocity die", "XL-185 shift left", "XL-185 shift right"};

// True when a tool changed a value the panel shows (stage rows or Length).
static bool stagesDiffer(const xl::Pattern& a, const xl::Pattern& b) {
	for (int i = 0; i < xl::NUM_STAGES; i++) {
		const xl::Stage& x = a.stage[i];
		const xl::Stage& y = b.stage[i];
		if (x.pitch != y.pitch || x.velocity != y.velocity || x.mode != y.mode || x.len != y.len
			|| x.prob != y.prob || x.slide != y.slide || x.on != y.on || x.lock != y.lock)
			return true;
	}
	return a.length != b.length;
}

struct XL185 : Module {
	enum ParamId {
		ENUMS(ON_PARAMS, 8),
		ENUMS(PITCH_PARAMS, 8),
		ENUMS(VEL_PARAMS, 8),
		ENUMS(MODE_PARAMS, 8),
		ENUMS(LEN_PARAMS, 8),
		ENUMS(PROB_PARAMS, 8),
		ENUMS(SLIDE_PARAMS, 8),
		ENUMS(LOCK_PARAMS, 8),
		LENGTH_PARAM,
		DIRECTION_PARAM,
		SWING_PARAM,
		GATE_PARAM,
		TRANSPOSE_PARAM,
		GLIDE_PARAM,
		ROOT_PARAM,
		SCALE_PARAM,
		RESTART_PARAM,
		RUN_PARAM,
		GLOBAL_PARAM,
		MUTATE_PARAM,
		RESET_TOOL_PARAM,
		PITCH_DIE_PARAM,
		VEL_DIE_PARAM,
		SHIFT_LEFT_PARAM,
		SHIFT_RIGHT_PARAM,
		AUTO_PARAM,
		EVERY_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		RUN_INPUT,
		TRANSPOSE_INPUT,
		MUTATE_INPUT,
		RANDOM_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		VOCT_OUTPUT,
		GATE_OUTPUT,
		VEL_OUTPUT,
		SLIDE_OUTPUT,
		STAGE_OUTPUT,
		EOC_OUTPUT,
		ENUMS(STAGE_GATE_OUTPUTS, 8),
		STAGE_CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(STAGE_LIGHTS, 8),
		ENUMS(ON_LIGHTS, 8),
		ENUMS(SLIDE_LIGHTS, 8),
		ENUMS(LOCK_LIGHTS, 8),
		RUN_LIGHT,
		AUTO_LIGHT,
		LIGHTS_LEN
	};

	xl::Player player;
	xl::Pattern pattern;
	xl::PlayerIn in;
	xl::PlayerOut out;
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger runTrigger;
	dsp::ClockDivider updateDivider;
	int stageGateMode = xl::STAGE_GATE_NOTE;
	int theme = lay::THEME_KANSU;	// panel colours, saved with the patch

	// Tools. Menu settings are plain ints read by the audio thread.
	int parts = xl::PART_DEFAULT;
	xl::ToolRanges ranges;
	int stepsPerBar = 16;
	xl::Rng rng;
	xl::AutoCount autoCount;
	dsp::BooleanTrigger toolButtons[TOOL_COUNT];
	dsp::SchmittTrigger mutateTrigger;
	dsp::SchmittTrigger randomTrigger;

	// One undo slot: filled by the audio thread, emptied by the widget, which pushes the history entry.
	xl::Pattern undoBefore;
	xl::Pattern undoAfter;
	int undoTool = 0;
	std::atomic<bool> undoFull;

	uint32_t expanderFrame = 0;

	XL185() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> modes = {"Rest", "Once", "Hold", "1/4", "1/3", "1/2", "All"};
		for (int i = 0; i < 8; i++) {
			std::string s = "Stage " + std::to_string(i + 1);
			configSwitch(ON_PARAMS + i, 0.f, 1.f, 1.f, s + " on", {"Off", "On"});
			configParam(PITCH_PARAMS + i, -36.f, 36.f, 0.f, s + " pitch", " semitones")->snapEnabled = true;
			configParam(VEL_PARAMS + i, 0.f, 127.f, 120.f, s + " velocity")->snapEnabled = true;
			configSwitch(MODE_PARAMS + i, 0.f, 6.f, 1.f, s + " mode", modes);
			configParam(LEN_PARAMS + i, 1.f, 8.f, 1.f, s + " length", " steps")->snapEnabled = true;
			configParam(PROB_PARAMS + i, 0.f, 100.f, 100.f, s + " probability", " %")->snapEnabled = true;
			configSwitch(SLIDE_PARAMS + i, 0.f, 1.f, 0.f, s + " slide", {"Off", "On"});
			SwitchQuantity* lock = configSwitch(LOCK_PARAMS + i, 0.f, 1.f, 0.f, s + " lock", {"Off", "On"});
			lock->description = "Kept by the random tools and Reset";
			lock->randomizeEnabled = false;
			configOutput(STAGE_GATE_OUTPUTS + i, s + " gate");
			configLight(STAGE_LIGHTS + i, s + " playing");
		}
		configParam(LENGTH_PARAM, 1.f, 8.f, 8.f, "Length", " stages")->snapEnabled = true;
		configSwitch(DIRECTION_PARAM, 0.f, 3.f, 0.f, "Direction", {"Forward", "Backward", "Back and forth", "Random"});
		configParam(SWING_PARAM, 0.f, 100.f, 0.f, "Swing", " %")->description = "Delays every second step; needs a straight clock";
		configParam(GATE_PARAM, 1.f, 100.f, 100.f, "Gate length", " %");
		configParam(TRANSPOSE_PARAM, -24.f, 24.f, 0.f, "Transpose", " semitones")->snapEnabled = true;
		configParam(GLIDE_PARAM, 0.f, 500.f, 60.f, "Glide", " ms");
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
		std::vector<std::string> scales = {"Off"};
		for (int i = 0; i < xl::SCALE_COUNT; i++)
			scales.push_back(xl::scaleDef(i).name);
		configSwitch(SCALE_PARAM, 0.f, (float) xl::SCALE_COUNT, 0.f, "Scale", scales);
		configSwitch(RESTART_PARAM, 0.f, 4.f, 0.f, "Restart", {"Off", "16 steps", "32 steps", "64 steps", "128 steps"});

		configInput(CLOCK_INPUT, "Clock (one step per pulse)");
		configInput(RESET_INPUT, "Reset (trigger)");
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"})->randomizeEnabled = false;
		configInput(RUN_INPUT, "Run (trigger, toggles Run)");
		configInput(TRANSPOSE_INPUT, "Transpose (V/Oct)");
		configOutput(VOCT_OUTPUT, "Pitch (V/Oct)");
		configOutput(GATE_OUTPUT, "Gate (0 or 10 V)");
		configOutput(VEL_OUTPUT, "Velocity (0 to 10 V)");
		configOutput(SLIDE_OUTPUT, "Slide gate");
		configOutput(STAGE_OUTPUT, "Stage start trigger");
		configOutput(EOC_OUTPUT, "End of cycle trigger");
		configOutput(STAGE_CV_OUTPUT, "Stage pitches (8 channels, V/Oct)");

		configButton(GLOBAL_PARAM, "Global (new random values on every unlocked stage)");
		configButton(MUTATE_PARAM, "Mutate (new random values on one stage)");
		configButton(RESET_TOOL_PARAM, "Reset unlocked stages to defaults");
		configButton(PITCH_DIE_PARAM, "Pitch die (random pitches)");
		configButton(VEL_DIE_PARAM, "Velocity die (random velocities)");
		configButton(SHIFT_LEFT_PARAM, "Shift stages left");
		configButton(SHIFT_RIGHT_PARAM, "Shift stages right");
		configSwitch(AUTO_PARAM, 0.f, 1.f, 0.f, "Auto Mutate", {"Off", "On"})->randomizeEnabled = false;
		configSwitch(EVERY_PARAM, 0.f, 2.f, 0.f, "Auto Mutate interval", {"4 bars", "8 bars", "16 bars"})->randomizeEnabled = false;
		configInput(MUTATE_INPUT, "Mutate (trigger)");
		configInput(RANDOM_INPUT, "Random (trigger, as Global)");
		undoFull.store(false);
		rng.seed(random::u32());

		updateDivider.setDivision(UPDATE_DIV);
		player.seed(random::u32());
		update();
	}

	int paramInt(int id) {
		return (int) std::round(params[id].getValue());
	}

	// Params to Pattern and settings, STAGE CV. Cheap, every UPDATE_DIV samples.
	void update() {
		for (int i = 0; i < 8; i++) {
			xl::Stage& s = pattern.stage[i];
			s.on = params[ON_PARAMS + i].getValue() > 0.5f;
			s.pitch = paramInt(PITCH_PARAMS + i);
			s.velocity = paramInt(VEL_PARAMS + i);
			s.mode = clamp(paramInt(MODE_PARAMS + i), 0, xl::MODE_COUNT - 1);
			s.len = clamp(paramInt(LEN_PARAMS + i), 1, 8);
			s.prob = paramInt(PROB_PARAMS + i);
			s.slide = params[SLIDE_PARAMS + i].getValue() > 0.5f;
			s.lock = params[LOCK_PARAMS + i].getValue() > 0.5f;
		}
		pattern.length = clamp(paramInt(LENGTH_PARAM), 1, 8);
		pattern.direction = clamp(paramInt(DIRECTION_PARAM), 0, xl::DIR_COUNT - 1);
		pattern.transpose = paramInt(TRANSPOSE_PARAM);
		pattern.restart = RESTART_STEPS[clamp(paramInt(RESTART_PARAM), 0, 4)];
		pattern.scale = clamp(paramInt(SCALE_PARAM), 0, xl::SCALE_COUNT) - 1;
		pattern.root = clamp(paramInt(ROOT_PARAM), 0, 11);

		float tv = clamp(inputs[TRANSPOSE_INPUT].getVoltage(), -10.f, 10.f);
		in.transpose = (int) std::round(tv * 12.f);
		in.swing = params[SWING_PARAM].getValue();
		in.gate = params[GATE_PARAM].getValue();
		in.glide = params[GLIDE_PARAM].getValue();
		in.stageGateMode = stageGateMode;

		Output& cv = outputs[STAGE_CV_OUTPUT];
		cv.setChannels(8);
		for (int i = 0; i < 8; i++) {
			int semis = clamp(60 + pattern.stage[i].pitch + pattern.transpose + in.transpose, 0, 127);
			double cents = xl::snapCents(semis, pattern.root, pattern.scale);
			cv.setVoltage((float) ((cents - 6000.0) / 1200.0), i);
		}
	}

	void process(const ProcessArgs& args) override {
		bool clock = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		in.reset = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		// RUN is a trigger that toggles the Run button, as clock modules send it.
		if (runTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
			params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		in.run = params[RUN_PARAM].getValue() > 0.5f;
		in.clock = clock;
		bool tick = updateDivider.process();
		if (tick || clock)
			update();

		// Tools: buttons and trigger jacks, before the step so it reads the new values.
		// Only button presses write undo entries; triggers at the jacks do not, as Auto.
		bool fire[TOOL_COUNT];
		bool pressed[TOOL_COUNT];
		for (int t = 0; t < TOOL_COUNT; t++)
			fire[t] = pressed[t] = toolButtons[t].process(params[GLOBAL_PARAM + t].getValue() > 0.f);
		if (randomTrigger.process(inputs[RANDOM_INPUT].getVoltage(), 0.1f, 1.f))
			fire[TOOL_GLOBAL] = true;
		if (mutateTrigger.process(inputs[MUTATE_INPUT].getVoltage(), 0.1f, 1.f))
			fire[TOOL_MUTATE] = true;
		for (int t = 0; t < TOOL_COUNT; t++)
			if (fire[t])
				runTool(t, pressed[t]);

		player.process(pattern, in, args.sampleTime, out);

		// Auto Mutate right after the last step of every Nth bar; no undo entry.
		int bars = AUTO_BARS[clamp(paramInt(EVERY_PARAM), 0, 2)];
		if (autoCount.process(in.reset, in.run, out.stepRan, bars, stepsPerBar) && params[AUTO_PARAM].getValue() > 0.5f)
			runTool(TOOL_MUTATE, false);

		outputs[VOCT_OUTPUT].setVoltage(out.vOct);
		outputs[GATE_OUTPUT].setVoltage(out.gate ? 10.f : 0.f);
		outputs[VEL_OUTPUT].setVoltage(out.velocity);
		outputs[SLIDE_OUTPUT].setVoltage(out.slide ? 10.f : 0.f);
		outputs[STAGE_OUTPUT].setVoltage(out.stageTrig ? 10.f : 0.f);
		outputs[EOC_OUTPUT].setVoltage(out.eoc ? 10.f : 0.f);
		for (int i = 0; i < 8; i++)
			outputs[STAGE_GATE_OUTPUTS + i].setVoltage(out.stageGate[i] ? 10.f : 0.f);
		writeExpander();

		if (tick) {
			for (int i = 0; i < 8; i++) {
				lights[STAGE_LIGHTS + i].setBrightness(out.stage == i + 1 ? 1.f : 0.f);
				lights[RUN_LIGHT].setBrightness(params[RUN_PARAM].getValue());
				lights[ON_LIGHTS + i].setBrightness(params[ON_PARAMS + i].getValue());
				lights[SLIDE_LIGHTS + i].setBrightness(params[SLIDE_PARAMS + i].getValue());
				lights[LOCK_LIGHTS + i].setBrightness(params[LOCK_PARAMS + i].getValue());
			}
			lights[AUTO_LIGHT].setBrightness(params[AUTO_PARAM].getValue());
		}
	}

	// Tells an XL-185 MOD on the right what played on this sample; it reads it one sample later.
	void writeExpander() {
		Module* r = rightExpander.module;
		if (!r || r->model != modelXL185Mod || !r->leftExpander.producerMessage)
			return;
		ExpanderMessage* m = (ExpanderMessage*) r->leftExpander.producerMessage;
		m->marker = EXPANDER_MARKER;
		m->frame = ++expanderFrame;
		m->stage = out.stage;
		m->stageStart = out.stageStart;
		m->stepRan = out.stepRan;
		m->reset = in.reset;
		m->run = in.run;
		m->length = pattern.length;
		uint8_t locks = 0, ons = 0;
		for (int i = 0; i < 8; i++) {
			if (pattern.stage[i].lock)
				locks |= 1 << i;
			if (pattern.stage[i].on)
				ons |= 1 << i;
		}
		m->lockBits = locks;
		m->onBits = ons;
		m->stepsPerBar = stepsPerBar;
		m->theme = theme;
		r->leftExpander.requestMessageFlip();
	}

	// Reads the params, runs one tool and writes back only what it changed. Audio thread, no allocation.
	void runTool(int tool, bool undo) {
		update();
		xl::Pattern before = pattern;
		unsigned p = (unsigned) parts;
		switch (tool) {
			case TOOL_GLOBAL: xl::toolGlobal(pattern, p, ranges, rng); break;
			case TOOL_MUTATE: xl::toolMutate(pattern, p, ranges, rng); break;
			case TOOL_RESET: xl::toolReset(pattern); break;
			case TOOL_PITCH_DIE: xl::toolDie(pattern, xl::PART_PITCH, ranges, rng); break;
			case TOOL_VEL_DIE: xl::toolDie(pattern, xl::PART_VELOCITY, ranges, rng); break;
			case TOOL_SHIFT_LEFT: xl::toolShift(pattern, -1); break;
			case TOOL_SHIFT_RIGHT: xl::toolShift(pattern, 1); break;
			default: return;
		}
		if (!stagesDiffer(before, pattern))
			return;
		writeChanged(pattern, before);
		// A full slot means the widget has not picked up the last entry: this one goes without undo.
		if (undo && !undoFull.load(std::memory_order_acquire)) {
			undoBefore = before;
			undoAfter = pattern;
			undoTool = tool;
			undoFull.store(true, std::memory_order_release);
		}
	}

	// Sets the params whose values differ between to and from to the values in to.
	void writeChanged(const xl::Pattern& to, const xl::Pattern& from) {
		for (int i = 0; i < 8; i++) {
			const xl::Stage& a = to.stage[i];
			const xl::Stage& b = from.stage[i];
			if (a.pitch != b.pitch)
				params[PITCH_PARAMS + i].setValue((float) a.pitch);
			if (a.velocity != b.velocity)
				params[VEL_PARAMS + i].setValue((float) a.velocity);
			if (a.mode != b.mode)
				params[MODE_PARAMS + i].setValue((float) a.mode);
			if (a.len != b.len)
				params[LEN_PARAMS + i].setValue((float) a.len);
			if (a.prob != b.prob)
				params[PROB_PARAMS + i].setValue((float) a.prob);
			if (a.slide != b.slide)
				params[SLIDE_PARAMS + i].setValue(a.slide ? 1.f : 0.f);
			if (a.on != b.on)
				params[ON_PARAMS + i].setValue(a.on ? 1.f : 0.f);
			if (a.lock != b.lock)
				params[LOCK_PARAMS + i].setValue(a.lock ? 1.f : 0.f);
		}
		if (to.length != from.length)
			params[LENGTH_PARAM].setValue((float) to.length);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		stageGateMode = xl::STAGE_GATE_NOTE;
		parts = xl::PART_DEFAULT;
		ranges = xl::ToolRanges();
		stepsPerBar = 16;
		player.reset();
		autoCount.restart();
		update();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "stageGateMode", json_integer(stageGateMode));
		json_object_set_new(rootJ, "toolParts", json_integer(parts));
		json_object_set_new(rootJ, "pitchLow", json_integer(ranges.pitchLo));
		json_object_set_new(rootJ, "pitchHigh", json_integer(ranges.pitchHi));
		json_object_set_new(rootJ, "velocityLow", json_integer(ranges.velLo));
		json_object_set_new(rootJ, "velocityHigh", json_integer(ranges.velHi));
		json_object_set_new(rootJ, "stepsPerBar", json_integer(stepsPerBar));
		json_object_set_new(rootJ, "panel", json_integer(theme));
		return rootJ;
	}

	static void readInt(json_t* rootJ, const char* key, int& v, int lo, int hi) {
		json_t* j = json_object_get(rootJ, key);
		if (j)
			v = clamp((int) json_integer_value(j), lo, hi);
	}

	void dataFromJson(json_t* rootJ) override {
		readInt(rootJ, "stageGateMode", stageGateMode, 0, xl::STAGE_GATE_COUNT - 1);
		readInt(rootJ, "toolParts", parts, 0, 127);
		readInt(rootJ, "pitchLow", ranges.pitchLo, 24, 96);
		readInt(rootJ, "pitchHigh", ranges.pitchHi, 24, 96);
		readInt(rootJ, "velocityLow", ranges.velLo, 0, 127);
		readInt(rootJ, "velocityHigh", ranges.velHi, 0, 127);
		readInt(rootJ, "stepsPerBar", stepsPerBar, 1, 64);
		readInt(rootJ, "panel", theme, 0, lay::THEME_COUNT - 1);
		player.reset();
		autoCount.restart();
	}
};

// One tool press in the undo history: puts back exactly the values the tool changed.
struct ToolAction : history::ModuleAction {
	xl::Pattern before;
	xl::Pattern after;

	void apply(const xl::Pattern& to, const xl::Pattern& from) {
		XL185* m = dynamic_cast<XL185*>(APP->engine->getModule(moduleId));
		if (m)
			m->writeChanged(to, from);
	}
	void undo() override {
		apply(before, after);
	}
	void redo() override {
		apply(after, before);
	}
};

struct XL185Widget : ModuleWidget {
	xlw::Ctx ctx;
	SvgPanel* panel = NULL;
	int shownTheme = -1;

	std::string panelPath(int theme) {
		return asset::plugin(pluginInstance, theme == lay::THEME_ABLETON ? "res/XL185-ableton.svg" : "res/XL185-kansu.svg");
	}

	xlw::CellButton* cell(xlw::CellButton* w, const lay::Box& b, float dx, int id, std::string text, int glyph, float def,
		int stage = -1) {
		w->box = xlw::mmRect(b, dx);
		w->module = module;
		w->paramId = id;
		w->initParamQuantity();
		w->ctx = &ctx;
		w->text = text;
		w->glyph = glyph;
		w->def = def;
		w->stage = stage;
		addParam(w);
		return w;
	}

	xlw::CellButton* latch(const lay::Box& b, float dx, int id, std::string text, int glyph, float def, int stage = -1) {
		return cell(new xlw::CellButton, b, dx, id, text, glyph, def, stage);
	}

	xlw::CellButton* push(const lay::Box& b, int id, std::string text, int glyph) {
		xlw::CellButton* w = new xlw::CellButton;
		w->momentary = true;
		return cell(w, b, 0.f, id, text, glyph, 0.f);
	}

	xlw::Screen* screen(const lay::Box& b, float dx, int id, int kind, float def, int stage = -1) {
		xlw::Screen* w = createParam<xlw::Screen>(Vec(), module, id);
		w->box = xlw::mmRect(b, dx);
		w->ctx = &ctx;
		w->kind = kind;
		w->def = def;
		w->stage = stage;
		addParam(w);
		return w;
	}

	XL185Widget(XL185* module) {
		setModule(module);
		// The theme is read from the patch before the widget exists.
		shownTheme = module ? clamp(module->theme, 0, lay::THEME_COUNT - 1) : lay::THEME_KANSU;
		panel = createPanel(panelPath(shownTheme));
		setPanel(panel);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		xlw::LabelLayer* labels = createWidget<xlw::LabelLayer>(Vec(0, 0));
		labels->box.size = box.size;
		labels->ctx = &ctx;
		addChild(labels);

		// Settings.
		xlw::ScaleScreen* scale = createWidget<xlw::ScaleScreen>(Vec(0, 0));
		scale->box = xlw::mmRect(lay::SCALE_SCREEN);
		scale->ctx = &ctx;
		scale->module = module;
		scale->rootParam = XL185::ROOT_PARAM;
		scale->scaleParam = XL185::SCALE_PARAM;
		addChild(scale);
		lay::Box rootBox = lay::SCALE_SCREEN;
		rootBox.w = lay::SCALE_ROOT_W;
		lay::Box nameBox = lay::SCALE_SCREEN;
		nameBox.x += lay::SCALE_ROOT_W;
		nameBox.w -= lay::SCALE_ROOT_W;
		const lay::Box hitBox[2] = {rootBox, nameBox};
		const int hitId[2] = {XL185::ROOT_PARAM, XL185::SCALE_PARAM};
		for (int k = 0; k < 2; k++) {
			xlw::ScaleHit* h = createParam<xlw::ScaleHit>(Vec(), module, hitId[k]);
			h->box = xlw::mmRect(hitBox[k]);
			h->rootParam = XL185::ROOT_PARAM;
			h->scaleParam = XL185::SCALE_PARAM;
			addParam(h);
		}
		xlw::DirectionSelector* dir = createParam<xlw::DirectionSelector>(Vec(), module, XL185::DIRECTION_PARAM);
		dir->box = xlw::mmRect(lay::DIRECTION);
		dir->ctx = &ctx;
		addParam(dir);
		screen(lay::LENGTH, 0.f, XL185::LENGTH_PARAM, xlw::SCREEN_LENGTH, 8.f);
		screen(lay::TRANSPOSE, 0.f, XL185::TRANSPOSE_PARAM, xlw::SCREEN_TRANSPOSE, 0.f);
		screen(lay::RESTART, 0.f, XL185::RESTART_PARAM, xlw::SCREEN_RESTART, 0.f);
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(lay::SWING.x, lay::SWING.y)), module, XL185::SWING_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(lay::GATE.x, lay::GATE.y)), module, XL185::GATE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(lay::GLIDE.x, lay::GLIDE.y)), module, XL185::GLIDE_PARAM));

		// Tools.
		push(lay::GLOBAL, XL185::GLOBAL_PARAM, "global", xlw::GLYPH_NONE);
		push(lay::MUTATE, XL185::MUTATE_PARAM, "mutate", xlw::GLYPH_NONE);
		push(lay::RESET, XL185::RESET_TOOL_PARAM, "reset", xlw::GLYPH_NONE);
		latch(lay::AUTO, 0.f, XL185::AUTO_PARAM, "auto", xlw::GLYPH_NONE, 0.f);
		screen(lay::EVERY, 0.f, XL185::EVERY_PARAM, xlw::SCREEN_EVERY, 0.f)->autoParam = XL185::AUTO_PARAM;
		push(lay::SHIFT_LEFT, XL185::SHIFT_LEFT_PARAM, "", xlw::GLYPH_LEFT);
		push(lay::SHIFT_RIGHT, XL185::SHIFT_RIGHT_PARAM, "", xlw::GLYPH_RIGHT);
		push(lay::PITCH_DIE, XL185::PITCH_DIE_PARAM, "", xlw::GLYPH_DIE);
		push(lay::VEL_DIE, XL185::VEL_DIE_PARAM, "", xlw::GLYPH_DIE);

		// Eight stage columns.
		for (int i = 0; i < 8; i++) {
			float x = lay::STAGE_X[i];
			xlw::PlayLight* light = createLightCentered<xlw::PlayLight>(mm2px(Vec(x, lay::LIGHT_Y)), module, XL185::STAGE_LIGHTS + i);
			light->ctx = &ctx;
			addChild(light);
			latch(lay::ON_BOX, x, XL185::ON_PARAMS + i, std::to_string(i + 1), xlw::GLYPH_NONE, 1.f, i);
			addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, lay::PITCH_Y)), module, XL185::PITCH_PARAMS + i));
			const lay::Box* barBox[2] = {&lay::VEL_BOX, &lay::PROB_BOX};
			const int barId[2] = {XL185::VEL_PARAMS + i, XL185::PROB_PARAMS + i};
			const float barDef[2] = {120.f / 127.f, 1.f};
			for (int k = 0; k < 2; k++) {
				xlw::BarSlider* bar = createParam<xlw::BarSlider>(Vec(), module, barId[k]);
				bar->box = xlw::mmRect(*barBox[k], x);
				bar->ctx = &ctx;
				bar->stage = i;
				bar->def = barDef[k];
				addParam(bar);
			}
			xlw::LenCells* len = createParam<xlw::LenCells>(Vec(), module, XL185::LEN_PARAMS + i);
			len->box = xlw::mmRect(lay::LEN_BOX, x);
			len->ctx = &ctx;
			len->stage = i;
			len->modeParam = XL185::MODE_PARAMS + i;
			addParam(len);
			screen(lay::MODE_BOX, x, XL185::MODE_PARAMS + i, xlw::SCREEN_MODE, 1.f, i);
			latch(lay::SLIDE_BOX, x, XL185::SLIDE_PARAMS + i, "S", xlw::GLYPH_NONE, 0.f);
			latch(lay::LOCK_BOX, x, XL185::LOCK_PARAMS + i, "L", xlw::GLYPH_NONE, 0.f);
			addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(x, lay::GATE_Y)), module, XL185::STAGE_GATE_OUTPUTS + i));
		}

		// In and Out.
		const lay::Pt inPt[6] = {lay::CLOCK, lay::RESET_IN, lay::RUN_IN, lay::TRANSPOSE_IN, lay::MUTATE_IN, lay::RANDOM_IN};
		const int inId[6] = {XL185::CLOCK_INPUT, XL185::RESET_INPUT, XL185::RUN_INPUT, XL185::TRANSPOSE_INPUT,
			XL185::MUTATE_INPUT, XL185::RANDOM_INPUT};
		for (int k = 0; k < 6; k++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(inPt[k].x, inPt[k].y)), module, inId[k]));
		latch(lay::RUN, 0.f, XL185::RUN_PARAM, "", xlw::GLYPH_PLAY, 1.f);
		const lay::Pt outPt[7] = {lay::VOCT, lay::GATE_OUT, lay::VEL_OUT, lay::SLIDE_OUT, lay::STAGE_OUT, lay::EOC, lay::STAGE_CV};
		const int outId[7] = {XL185::VOCT_OUTPUT, XL185::GATE_OUTPUT, XL185::VEL_OUTPUT, XL185::SLIDE_OUTPUT,
			XL185::STAGE_OUTPUT, XL185::EOC_OUTPUT, XL185::STAGE_CV_OUTPUT};
		for (int k = 0; k < 7; k++)
			addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outPt[k].x, outPt[k].y)), module, outId[k]));
	}

	// Picks up a tool press from the audio thread and puts it in the undo history.
	void step() override {
		XL185* m = getModule<XL185>();
		// Theme and Length for the drawn parts; swap the panel when the theme changes.
		ctx.theme = m ? clamp(m->theme, 0, lay::THEME_COUNT - 1) : lay::THEME_KANSU;
		ctx.length = m ? clamp(m->paramInt(XL185::LENGTH_PARAM), 1, 8) : 8;
		if (ctx.theme != shownTheme) {
			panel->setBackground(window::Svg::load(panelPath(ctx.theme)));
			shownTheme = ctx.theme;
		}
		if (m && m->undoFull.load(std::memory_order_acquire)) {
			ToolAction* a = new ToolAction;
			a->moduleId = m->id;
			a->name = TOOL_NAMES[clamp(m->undoTool, 0, TOOL_COUNT - 1)];
			a->before = m->undoBefore;
			a->after = m->undoAfter;
			m->undoFull.store(false, std::memory_order_release);
			APP->history->push(a);
		}
		ModuleWidget::step();
	}

	// A submenu of values; the index of the current value, or none ticked when it is not in the list.
	static MenuItem* valueMenu(std::string text, std::vector<std::string> labels, std::vector<int> values, int* ptr) {
		return createIndexSubmenuItem(text, labels,
			[=]() {
				for (size_t i = 0; i < values.size(); i++)
					if (values[i] == *ptr)
						return i;
				return values.size();
			},
			[=](size_t i) {
				*ptr = values[i];
			});
	}

	void appendContextMenu(Menu* menu) override {
		XL185* module = getModule<XL185>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Panel", {lay::THEMES[0].name, lay::THEMES[1].name}, &module->theme));
		menu->addChild(createIndexPtrSubmenuItem("Stage gate outputs",
			{"Note gates", "High for the whole stage", "Trigger at stage start"}, &module->stageGateMode));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Global and Mutate change"));
		const char* partName[7] = {"Pitch", "Velocity", "Probability", "Slide", "Mode", "Length", "Seq Length (Global only)"};
		const int partBit[7] = {xl::PART_PITCH, xl::PART_VELOCITY, xl::PART_PROB, xl::PART_SLIDE, xl::PART_MODE,
			xl::PART_LEN, xl::PART_SEQLEN};
		for (int k = 0; k < 7; k++) {
			int bit = partBit[k];
			menu->addChild(createBoolMenuItem(partName[k], "",
				[=]() { return (module->parts & bit) != 0; },
				[=](bool on) { module->parts = on ? (module->parts | bit) : (module->parts & ~bit); }));
		}
		std::vector<std::string> notes = {"C1", "C2", "C3", "C4", "C5", "C6", "C7"};
		std::vector<int> noteValues = {24, 36, 48, 60, 72, 84, 96};
		menu->addChild(valueMenu("Pitch range low", notes, noteValues, &module->ranges.pitchLo));
		menu->addChild(valueMenu("Pitch range high", notes, noteValues, &module->ranges.pitchHi));
		std::vector<std::string> vels = {"1", "32", "64", "96", "110", "127"};
		std::vector<int> velValues = {1, 32, 64, 96, 110, 127};
		menu->addChild(valueMenu("Velocity range low", vels, velValues, &module->ranges.velLo));
		menu->addChild(valueMenu("Velocity range high", vels, velValues, &module->ranges.velHi));
		menu->addChild(valueMenu("Steps per bar (Auto)", {"8", "12", "16", "24", "32"}, {8, 12, 16, 24, 32}, &module->stepsPerBar));
	}
};

Model* modelXL185 = createModel<XL185, XL185Widget>("XL185");
