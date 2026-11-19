#include "plugin.hpp"
#include "../core/LaneOut.hpp"
#include "../core/Player.hpp"
#include "../core/Tools.hpp"
#include "Widgets.hpp"
#include "LayoutMod.hpp"
#include "Expander.hpp"
#include <atomic>

static const int MOD_AUTO_BARS[3] = {4, 8, 16};
static const char* LANE_NAMES[3] = {"A", "B", "C"};

enum ModTool {
	MTOOL_RANDOM,
	MTOOL_MUTATE,
	MTOOL_RESET,
	MTOOL_DIE,	// + lane
	MTOOL_SHIFT_LEFT = MTOOL_DIE + 3,
	MTOOL_SHIFT_RIGHT = MTOOL_SHIFT_LEFT + 3,
	MTOOL_COUNT = MTOOL_SHIFT_RIGHT + 3
};

static const char* MTOOL_NAMES[MTOOL_COUNT] = {"XL-185 MOD random", "XL-185 MOD mutate", "XL-185 MOD reset",
	"XL-185 MOD lane A die", "XL-185 MOD lane B die", "XL-185 MOD lane C die",
	"XL-185 MOD lane A shift left", "XL-185 MOD lane B shift left", "XL-185 MOD lane C shift left",
	"XL-185 MOD lane A shift right", "XL-185 MOD lane B shift right", "XL-185 MOD lane C shift right"};

// True when a tool changed a lane value, Min, Max or Smooth.
static bool lanesDiffer(const xl::Pattern& a, const xl::Pattern& b) {
	for (int l = 0; l < xl::NUM_LANES; l++) {
		const xl::Lane& x = a.lane[l];
		const xl::Lane& y = b.lane[l];
		if (x.min != y.min || x.max != y.max || x.smooth != y.smooth)
			return true;
		for (int i = 0; i < xl::NUM_STAGES; i++)
			if (x.value[i] != y.value[i])
				return true;
	}
	return false;
}

struct XL185Mod : Module {
	enum ParamId {
		ENUMS(VALUE_PARAMS, 24),	// lane l, stage i: l * 8 + i
		ENUMS(MIN_PARAMS, 3),
		ENUMS(MAX_PARAMS, 3),
		ENUMS(SMOOTH_PARAMS, 3),
		ENUMS(DIE_PARAMS, 3),
		ENUMS(SHIFT_LEFT_PARAMS, 3),
		ENUMS(SHIFT_RIGHT_PARAMS, 3),
		RANDOM_PARAM,
		MUTATE_PARAM,
		RESET_PARAM,
		AUTO_PARAM,
		EVERY_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		RANDOM_INPUT,
		MUTATE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(LANE_OUTPUTS, 3),
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	ExpanderMessage messages[2];	// the left expander's double buffer
	uint32_t lastFrame = 0;
	bool wasLinked = false;
	xl::LaneOut lanes[3];
	xl::Rng rng;
	xl::AutoCount autoCount;
	dsp::BooleanTrigger toolButtons[MTOOL_COUNT];
	dsp::SchmittTrigger randomTrigger;
	dsp::SchmittTrigger mutateTrigger;
	int theme = lay::THEME_KANSU;	// own panel choice, used while no sequencer is attached

	// What the widget draws, written by the audio thread.
	int shownTheme = lay::THEME_KANSU;
	int shownLength = 0;	// 0 while alone: every bar dimmed
	int shownStage = 0;

	// The sequencer's locks and Length for the tools; alone: no locks, Length 8.
	int length = 8;
	uint8_t lockBits = 0;

	// One undo slot, as on the sequencer.
	xl::Pattern undoBefore;
	xl::Pattern undoAfter;
	int undoTool = 0;
	std::atomic<bool> undoFull;

	XL185Mod() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int l = 0; l < 3; l++) {
			std::string s = std::string("Lane ") + LANE_NAMES[l];
			for (int i = 0; i < 8; i++)
				configParam(VALUE_PARAMS + l * 8 + i, 0.f, 100.f, 0.f, s + " stage " + std::to_string(i + 1), " %")->snapEnabled = true;
			configParam(MIN_PARAMS + l, -100.f, 100.f, 0.f, s + " minimum", " %")->snapEnabled = true;
			configParam(MAX_PARAMS + l, -100.f, 100.f, 100.f, s + " maximum", " %")->snapEnabled = true;
			configParam(SMOOTH_PARAMS + l, 0.f, 2000.f, 0.f, s + " smooth (ramp time)", " ms")->snapEnabled = true;
			configButton(DIE_PARAMS + l, s + " die (random values)");
			configButton(SHIFT_LEFT_PARAMS + l, s + " shift values left");
			configButton(SHIFT_RIGHT_PARAMS + l, s + " shift values right");
			configOutput(LANE_OUTPUTS + l, s + " (0 to 10 V)");
		}
		configButton(RANDOM_PARAM, "Random (all three lanes)");
		configButton(MUTATE_PARAM, "Mutate (new values on one stage, all lanes)");
		configButton(RESET_PARAM, "Reset lanes to defaults");
		configSwitch(AUTO_PARAM, 0.f, 1.f, 0.f, "Auto Mutate", {"Off", "On"})->randomizeEnabled = false;
		configSwitch(EVERY_PARAM, 0.f, 2.f, 0.f, "Auto Mutate interval", {"4 bars", "8 bars", "16 bars"})->randomizeEnabled = false;
		configInput(RANDOM_INPUT, "Random (trigger)");
		configInput(MUTATE_INPUT, "Mutate (trigger)");
		leftExpander.producerMessage = &messages[0];
		leftExpander.consumerMessage = &messages[1];
		undoFull.store(false);
		rng.seed(random::u32());
	}

	int paramInt(int id) {
		return (int) std::round(params[id].getValue());
	}

	// The lanes from the params, with the sequencer's locks and Length.
	void readPattern(xl::Pattern& p) {
		for (int l = 0; l < 3; l++) {
			xl::Lane& ln = p.lane[l];
			for (int i = 0; i < 8; i++)
				ln.value[i] = params[VALUE_PARAMS + l * 8 + i].getValue();
			ln.min = params[MIN_PARAMS + l].getValue();
			ln.max = params[MAX_PARAMS + l].getValue();
			ln.smooth = params[SMOOTH_PARAMS + l].getValue();
		}
		for (int i = 0; i < 8; i++)
			p.stage[i].lock = (lockBits >> i) & 1;
		p.length = length;
	}

	void process(const ProcessArgs& args) override {
		Module* left = leftExpander.module;
		// Linked only on a message the sequencer wrote since the last sample: while alone the buffer
		// still holds the old sequencer's last message, which must not count on re-attach.
		const ExpanderMessage* msg = NULL;
		if (left && left->model == modelXL185) {
			const ExpanderMessage* m = (const ExpanderMessage*) leftExpander.consumerMessage;
			if (m->marker == EXPANDER_MARKER && m->frame != lastFrame)
				msg = m;
		}
		bool fresh = msg != NULL;
		if (msg)
			lastFrame = msg->frame;
		length = msg ? clamp(msg->length, 1, 8) : 8;
		lockBits = msg ? msg->lockBits : 0;

		// Tools: buttons and trigger jacks.
		bool fire[MTOOL_COUNT];
		for (int t = 0; t < MTOOL_COUNT; t++)
			fire[t] = false;
		fire[MTOOL_RANDOM] = toolButtons[MTOOL_RANDOM].process(params[RANDOM_PARAM].getValue() > 0.f);
		fire[MTOOL_MUTATE] = toolButtons[MTOOL_MUTATE].process(params[MUTATE_PARAM].getValue() > 0.f);
		fire[MTOOL_RESET] = toolButtons[MTOOL_RESET].process(params[RESET_PARAM].getValue() > 0.f);
		for (int l = 0; l < 3; l++) {
			fire[MTOOL_DIE + l] = toolButtons[MTOOL_DIE + l].process(params[DIE_PARAMS + l].getValue() > 0.f);
			fire[MTOOL_SHIFT_LEFT + l] = toolButtons[MTOOL_SHIFT_LEFT + l].process(params[SHIFT_LEFT_PARAMS + l].getValue() > 0.f);
			fire[MTOOL_SHIFT_RIGHT + l] = toolButtons[MTOOL_SHIFT_RIGHT + l].process(params[SHIFT_RIGHT_PARAMS + l].getValue() > 0.f);
		}
		// Only button presses write undo entries; triggers at the jacks do not, as Auto.
		bool pressed[MTOOL_COUNT];
		for (int t = 0; t < MTOOL_COUNT; t++)
			pressed[t] = fire[t];
		if (randomTrigger.process(inputs[RANDOM_INPUT].getVoltage(), 0.1f, 1.f))
			fire[MTOOL_RANDOM] = true;
		if (mutateTrigger.process(inputs[MUTATE_INPUT].getVoltage(), 0.1f, 1.f))
			fire[MTOOL_MUTATE] = true;
		for (int t = 0; t < MTOOL_COUNT; t++)
			if (fire[t])
				runTool(t, pressed[t]);

		if (!msg) {
			// Alone: 0 V, and the next sequencer starts the lanes afresh.
			if (wasLinked) {
				for (int l = 0; l < 3; l++)
					lanes[l].reset();
				autoCount.restart();
			}
			wasLinked = false;
			for (int l = 0; l < 3; l++)
				outputs[LANE_OUTPUTS + l].setVoltage(0.f);
			shownTheme = clamp(theme, 0, lay::THEME_COUNT - 1);
			shownLength = 0;
			shownStage = 0;
			return;
		}

		// Each lane takes its stage's value at every stage start; attached mid-play, at once.
		int stage = msg->stage;
		bool start = fresh && msg->stageStart;
		if (!wasLinked && stage > 0)
			start = true;
		wasLinked = true;
		if (start && stage >= 1 && stage <= 8) {
			for (int l = 0; l < 3; l++) {
				float v = params[VALUE_PARAMS + l * 8 + stage - 1].getValue();
				float target = xl::LaneOut::target(v, params[MIN_PARAMS + l].getValue(), params[MAX_PARAMS + l].getValue());
				lanes[l].start(target, params[SMOOTH_PARAMS + l].getValue());
			}
		}

		// Auto Mutate after the step, so the next bar starts changed; no undo entry.
		if (fresh) {
			int bars = MOD_AUTO_BARS[clamp(paramInt(EVERY_PARAM), 0, 2)];
			if (autoCount.process(msg->reset, msg->run, msg->stepRan, bars, msg->stepsPerBar) && params[AUTO_PARAM].getValue() > 0.5f)
				runTool(MTOOL_MUTATE, false);
		}

		for (int l = 0; l < 3; l++)
			outputs[LANE_OUTPUTS + l].setVoltage(lanes[l].process(args.sampleTime));
		shownTheme = clamp(msg->theme, 0, lay::THEME_COUNT - 1);
		shownLength = length;
		shownStage = stage;
	}

	// Runs one tool on the lanes and writes back only what it changed. Audio thread, no allocation.
	void runTool(int tool, bool undo) {
		xl::Pattern p;
		readPattern(p);
		xl::Pattern before = p;
		if (tool == MTOOL_RANDOM)
			xl::toolLaneRandom(p, -1, rng);
		else if (tool == MTOOL_MUTATE)
			xl::toolLaneMutate(p, rng);
		else if (tool == MTOOL_RESET)
			xl::toolLaneReset(p);
		else if (tool < MTOOL_SHIFT_LEFT)
			xl::toolLaneRandom(p, tool - MTOOL_DIE, rng);
		else if (tool < MTOOL_SHIFT_RIGHT)
			xl::toolLaneShift(p, tool - MTOOL_SHIFT_LEFT, -1);
		else
			xl::toolLaneShift(p, tool - MTOOL_SHIFT_RIGHT, 1);
		if (!lanesDiffer(before, p))
			return;
		writeChanged(p, before);
		if (undo && !undoFull.load(std::memory_order_acquire)) {
			undoBefore = before;
			undoAfter = p;
			undoTool = tool;
			undoFull.store(true, std::memory_order_release);
		}
	}

	// Sets the lane params whose values differ between to and from to the values in to.
	void writeChanged(const xl::Pattern& to, const xl::Pattern& from) {
		for (int l = 0; l < 3; l++) {
			const xl::Lane& a = to.lane[l];
			const xl::Lane& b = from.lane[l];
			for (int i = 0; i < 8; i++)
				if (a.value[i] != b.value[i])
					params[VALUE_PARAMS + l * 8 + i].setValue(a.value[i]);
			if (a.min != b.min)
				params[MIN_PARAMS + l].setValue(a.min);
			if (a.max != b.max)
				params[MAX_PARAMS + l].setValue(a.max);
			if (a.smooth != b.smooth)
				params[SMOOTH_PARAMS + l].setValue(a.smooth);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int l = 0; l < 3; l++)
			lanes[l].reset();
		autoCount.restart();
		wasLinked = false;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "panel", json_integer(theme));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* j = json_object_get(rootJ, "panel");
		if (j)
			theme = clamp((int) json_integer_value(j), 0, lay::THEME_COUNT - 1);
		autoCount.restart();
	}
};

// One tool press in the undo history: puts back exactly the lane values the tool changed.
struct ModToolAction : history::ModuleAction {
	xl::Pattern before;
	xl::Pattern after;

	void apply(const xl::Pattern& to, const xl::Pattern& from) {
		XL185Mod* m = dynamic_cast<XL185Mod*>(APP->engine->getModule(moduleId));
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

struct XL185ModWidget : ModuleWidget {
	xlw::Ctx ctx;
	SvgPanel* panel = NULL;
	int shownTheme = -1;

	std::string panelPath(int theme) {
		return asset::plugin(pluginInstance, theme == lay::THEME_ABLETON ? "res/XL185Mod-ableton.svg" : "res/XL185Mod-kansu.svg");
	}

	xlw::CellButton* cell(xlw::CellButton* w, const lay::Box& b, float dy, int id, std::string text, int glyph) {
		w->box = xlw::mmRect(lay::Box{b.x, b.y + dy, b.w, b.h});
		w->module = module;
		w->paramId = id;
		w->initParamQuantity();
		w->ctx = &ctx;
		w->text = text;
		w->glyph = glyph;
		addParam(w);
		return w;
	}

	xlw::CellButton* push(const lay::Box& b, float dy, int id, std::string text, int glyph) {
		xlw::CellButton* w = new xlw::CellButton;
		w->momentary = true;
		return cell(w, b, dy, id, text, glyph);
	}

	xlw::Screen* screen(const lay::Box& b, float dy, int id, int kind, float def) {
		xlw::Screen* w = createParam<xlw::Screen>(Vec(), module, id);
		w->box = xlw::mmRect(lay::Box{b.x, b.y + dy, b.w, b.h});
		w->ctx = &ctx;
		w->kind = kind;
		w->def = def;
		addParam(w);
		return w;
	}

	XL185ModWidget(XL185Mod* module) {
		setModule(module);
		shownTheme = module ? clamp(module->theme, 0, lay::THEME_COUNT - 1) : lay::THEME_KANSU;
		ctx.theme = shownTheme;
		ctx.length = module ? 0 : 8;
		panel = createPanel(panelPath(shownTheme));
		setPanel(panel);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		xlw::LabelLayer* labels = createWidget<xlw::LabelLayer>(Vec(0, 0));
		labels->box.size = box.size;
		labels->ctx = &ctx;
		labels->table = laym::LABELS;
		labels->count = laym::LABEL_COUNT;
		addChild(labels);

		// Three lanes: die and shift arrows, eight value bars, Min / Max / Smooth, the output.
		for (int l = 0; l < 3; l++) {
			float y = laym::LANE_Y[l];
			push(laym::DIE_BOX, y, XL185Mod::DIE_PARAMS + l, "", xlw::GLYPH_DIE);
			push(laym::SHIFT_LEFT_BOX, y, XL185Mod::SHIFT_LEFT_PARAMS + l, "", xlw::GLYPH_LEFT);
			push(laym::SHIFT_RIGHT_BOX, y, XL185Mod::SHIFT_RIGHT_PARAMS + l, "", xlw::GLYPH_RIGHT);
			for (int i = 0; i < 8; i++) {
				xlw::BarSlider* bar = createParam<xlw::BarSlider>(Vec(), module, XL185Mod::VALUE_PARAMS + l * 8 + i);
				bar->box = xlw::mmRect(lay::Box{laym::BAR_BOX.x, laym::BAR_BOX.y + y, laym::BAR_BOX.w, laym::BAR_BOX.h},
					laym::COL_X[i]);
				bar->ctx = &ctx;
				bar->stage = i;
				bar->markPlaying = true;
				addParam(bar);
			}
			screen(laym::MIN_BOX, y, XL185Mod::MIN_PARAMS + l, xlw::SCREEN_WIDE, 0.f);
			screen(laym::MAX_BOX, y, XL185Mod::MAX_PARAMS + l, xlw::SCREEN_WIDE, 100.f);
			screen(laym::SMOOTH_BOX, y, XL185Mod::SMOOTH_PARAMS + l, xlw::SCREEN_WIDE, 0.f);
			addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(laym::OUT_X, y + laym::OUT_DY)), module,
				XL185Mod::LANE_OUTPUTS + l));
		}

		// Tools and In.
		push(laym::RANDOM, 0.f, XL185Mod::RANDOM_PARAM, "random", xlw::GLYPH_NONE);
		push(laym::MUTATE, 0.f, XL185Mod::MUTATE_PARAM, "mutate", xlw::GLYPH_NONE);
		push(laym::RESET, 0.f, XL185Mod::RESET_PARAM, "reset", xlw::GLYPH_NONE);
		cell(new xlw::CellButton, laym::AUTO, 0.f, XL185Mod::AUTO_PARAM, "auto", xlw::GLYPH_NONE);
		screen(laym::EVERY, 0.f, XL185Mod::EVERY_PARAM, xlw::SCREEN_EVERY, 0.f)->autoParam = XL185Mod::AUTO_PARAM;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laym::RANDOM_IN.x, laym::RANDOM_IN.y)), module, XL185Mod::RANDOM_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laym::MUTATE_IN.x, laym::MUTATE_IN.y)), module, XL185Mod::MUTATE_INPUT));
	}

	// Theme, Length and playing stage for the drawn parts; picks up a tool press for the undo history.
	void step() override {
		XL185Mod* m = getModule<XL185Mod>();
		if (m) {
			ctx.theme = clamp(m->shownTheme, 0, lay::THEME_COUNT - 1);
			ctx.length = clamp(m->shownLength, 0, 8);
			ctx.playing = m->shownStage;
		}
		if (ctx.theme != shownTheme) {
			panel->setBackground(window::Svg::load(panelPath(ctx.theme)));
			shownTheme = ctx.theme;
		}
		if (m && m->undoFull.load(std::memory_order_acquire)) {
			ModToolAction* a = new ModToolAction;
			a->moduleId = m->id;
			a->name = MTOOL_NAMES[clamp(m->undoTool, 0, MTOOL_COUNT - 1)];
			a->before = m->undoBefore;
			a->after = m->undoAfter;
			m->undoFull.store(false, std::memory_order_release);
			APP->history->push(a);
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		XL185Mod* module = getModule<XL185Mod>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Panel", {lay::THEMES[0].name, lay::THEMES[1].name}, &module->theme));
	}
};

Model* modelXL185Mod = createModel<XL185Mod, XL185ModWidget>("XL185Mod");
