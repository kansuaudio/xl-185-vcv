#include "Widgets.hpp"

namespace xlw {

static const char* RESTART_TEXT[5] = {"Off", "16", "32", "64", "128"};
static const char* EVERY_TEXT[3] = {"4 bars", "8 bars", "16 bars"};

math::Rect mmRect(const lay::Box& b, float dx) {
	return math::Rect(mm2px(math::Vec(b.x + dx, b.y)), mm2px(math::Vec(b.w, b.h)));
}

static float mm(float v) {
	return mm2px(v);
}

static NVGcolor col(lay::Rgb c) {
	return nvgRGB(c.r, c.g, c.b);
}

static const lay::Theme& themeOf(const Ctx* ctx) {
	return lay::THEMES[ctx ? clamp(ctx->theme, 0, lay::THEME_COUNT - 1) : 0];
}

// Beyond Length: a solid colour 40 % toward the area tone.
static NVGcolor dim(const lay::Theme& t, NVGcolor c, bool on) {
	return on ? nvgLerpRGBA(c, col(t.area), lay::DIM) : c;
}

static void rect(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, r);
	nvgFillColor(vg, c);
	nvgFill(vg);
}

// Label and screen fonts come from Rack, the title font from the plugin; spacing in Rack px between glyphs.
static void text(NVGcontext* vg, int font, float size, NVGcolor c, float x, float y, int align, const char* s,
	float spacing = 0.f) {
	std::string path = lay::FONT_IN_PLUGIN[font] ? asset::plugin(pluginInstance, lay::FONT_FILES[font])
		: asset::system(lay::FONT_FILES[font]);
	std::shared_ptr<window::Font> f = APP->window->loadFont(path);
	if (!f || f->handle < 0)
		return;
	nvgFontFaceId(vg, f->handle);
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, spacing);
	int h = align < 0 ? NVG_ALIGN_LEFT : align > 0 ? NVG_ALIGN_RIGHT : NVG_ALIGN_CENTER;
	nvgTextAlign(vg, h | NVG_ALIGN_MIDDLE);
	nvgFillColor(vg, c);
	nvgText(vg, x, y, s, NULL);
}

static void triangle(NVGcontext* vg, float cx, float cy, float x0, float y0, float x1, float y1, float x2, float y2,
	NVGcolor c) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx + mm(x0), cy + mm(y0));
	nvgLineTo(vg, cx + mm(x1), cy + mm(y1));
	nvgLineTo(vg, cx + mm(x2), cy + mm(y2));
	nvgClosePath(vg);
	nvgFillColor(vg, c);
	nvgFill(vg);
}

static void line(NVGcontext* vg, float cx, float cy, const float* pts, int n, NVGcolor c) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx + mm(pts[0]), cy + mm(pts[1]));
	for (int i = 1; i < n; i++)
		nvgLineTo(vg, cx + mm(pts[2 * i]), cy + mm(pts[2 * i + 1]));
	nvgStrokeColor(vg, c);
	nvgStrokeWidth(vg, mm(lay::GLYPH_STROKE));
	nvgLineCap(vg, NVG_BUTT);
	nvgLineJoin(vg, NVG_ROUND);
	nvgStroke(vg);
}

// Glyph offsets in mm from the centre (cx, cy in px), as in design/layout.py.
static void drawGlyph(NVGcontext* vg, int g, float cx, float cy, NVGcolor c) {
	if (g == GLYPH_DIE) {
		for (int i = -1; i <= 1; i++) {
			nvgBeginPath(vg);
			nvgCircle(vg, cx + mm(i), cy + mm(i), mm(0.42f));
			nvgFillColor(vg, c);
			nvgFill(vg);
		}
	}
	else if (g == GLYPH_LEFT || g == GLYPH_RIGHT) {
		float k = g == GLYPH_LEFT ? -1.f : 1.f;
		const float pts[4] = {-1.4f * k, 0.f, 1.3f * k, 0.f};
		line(vg, cx, cy, pts, 2, c);
		triangle(vg, cx, cy, 1.6f * k, 0.f, 0.5f * k, -1.f, 0.5f * k, 1.f, c);
	}
	else if (g == GLYPH_PLAY) {
		triangle(vg, cx, cy, -0.9f, -1.2f, 1.2f, 0.f, -0.9f, 1.2f, c);
	}
}

static void arrowHead(NVGcontext* vg, float cx, float cy, float x, float k, NVGcolor c) {
	triangle(vg, cx, cy, x, 0.f, x - 1.f * k, -0.85f, x - 1.f * k, 0.85f, c);
}

static void drawDirection(NVGcontext* vg, int d, float cx, float cy, NVGcolor c) {
	if (d == 0) {
		const float pts[4] = {-2.2f, 0.f, 1.f, 0.f};
		line(vg, cx, cy, pts, 2, c);
		arrowHead(vg, cx, cy, 2.2f, 1.f, c);
	}
	else if (d == 1) {
		const float pts[4] = {-1.f, 0.f, 2.2f, 0.f};
		line(vg, cx, cy, pts, 2, c);
		arrowHead(vg, cx, cy, -2.2f, -1.f, c);
	}
	else if (d == 2) {
		const float pts[4] = {-1.6f, 0.f, 1.6f, 0.f};
		line(vg, cx, cy, pts, 2, c);
		arrowHead(vg, cx, cy, 2.6f, 1.f, c);
		arrowHead(vg, cx, cy, -2.6f, -1.f, c);
	}
	else {
		const float pts[8] = {-2.3f, 1.2f, -0.7f, -1.f, 0.7f, 1.f, 1.4f, 0.f};
		line(vg, cx, cy, pts, 4, c);
		triangle(vg, cx, cy, 2.3f, -1.3f, 0.9f, -0.7f, 2.f, 0.4f, c);
	}
}

static float valueOf(app::ParamWidget* w, float def) {
	engine::ParamQuantity* pq = w->getParamQuantity();
	return pq ? pq->getValue() : def;
}

// Sets a param from a click or a menu, with one undo entry.
static void setWithUndo(engine::Module* m, int paramId, float v) {
	if (!m)
		return;
	engine::ParamQuantity* pq = m->paramQuantities[paramId];
	float old = pq->getValue();
	if (old == v)
		return;
	pq->setValue(v);
	history::ParamChange* h = new history::ParamChange;
	h->name = "set " + pq->getLabel();
	h->moduleId = m->id;
	h->paramId = paramId;
	h->oldValue = old;
	h->newValue = pq->getValue();
	APP->history->push(h);
}

static std::vector<std::string> switchLabels(engine::Module* m, int paramId) {
	engine::SwitchQuantity* sq = m ? dynamic_cast<engine::SwitchQuantity*>(m->paramQuantities[paramId]) : NULL;
	return sq ? sq->labels : std::vector<std::string>();
}

// A list of the param's values with the current one ticked.
static void addValueItems(ui::Menu* menu, engine::Module* m, int paramId) {
	std::vector<std::string> labels = switchLabels(m, paramId);
	for (size_t i = 0; i < labels.size(); i++) {
		menu->addChild(createCheckMenuItem(labels[i], "",
			[=]() { return (int) std::round(m->params[paramId].getValue()) == (int) i; },
			[=]() { setWithUndo(m, paramId, (float) i); }));
	}
}

static bool leftPress(const widget::Widget::ButtonEvent& e) {
	return e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == 0;
}

// ---- CellButton

void CellButton::draw(const DrawArgs& args) {
	const lay::Theme& t = themeOf(ctx);
	bool on = valueOf(this, def) > 0.5f;
	bool dimmed = ctx && stage >= 0 && stage >= ctx->length;
	NVGcolor bg = dim(t, col(on ? t.accentLight : t.off), dimmed);
	NVGcolor fg = dim(t, col(on ? t.onAccent : t.text), dimmed);
	rect(args.vg, 0, 0, box.size.x, box.size.y, mm(lay::R_CELL), bg);
	if (glyph != GLYPH_NONE)
		drawGlyph(args.vg, glyph, box.size.x / 2, box.size.y / 2, fg);
	else
		xlw::text(args.vg, lay::FONT_LABEL, lay::LABEL_PX, fg, box.size.x / 2, box.size.y / 2, 0, text.c_str());
	app::Switch::draw(args);
}

// ---- BarSlider

BarSlider::BarSlider() {
	forceLinear = true;
}

void BarSlider::onDragStart(const DragStartEvent& e) {
	// Full range over one and a half bar heights.
	speed = 1.f / (settings::knobLinearSensitivity * box.size.y * 1.5f);
	app::SliderKnob::onDragStart(e);
}

void BarSlider::draw(const DrawArgs& args) {
	const lay::Theme& t = themeOf(ctx);
	bool dimmed = ctx && stage >= ctx->length;
	engine::ParamQuantity* pq = getParamQuantity();
	float frac = clamp(pq ? pq->getScaledValue() : def, 0.f, 1.f);
	float r = mm(lay::R_SMALL);
	bool playing = markPlaying && ctx && ctx->playing == stage + 1;
	rect(args.vg, 0, 0, box.size.x, box.size.y, r, dim(t, col(playing ? t.mid : t.off), dimmed));
	float h = box.size.y * frac;
	if (h > 0.f)
		rect(args.vg, 0, box.size.y - h, box.size.x, h, std::min(r, h / 2), dim(t, col(t.accentLight), dimmed));
	app::SliderKnob::draw(args);
}

// ---- LenCells

// Does a stage in this mode fire on step n (0 based) of its Len?
static bool fires(int mode, int n) {
	switch (mode) {
		case 0: return false;
		case 1:
		case 2: return n == 0;
		case 3: return n % 4 == 0;
		case 4: return n % 3 == 0;
		case 5: return n % 2 == 0;
		default: return true;
	}
}

void LenCells::draw(const DrawArgs& args) {
	const lay::Theme& t = themeOf(ctx);
	bool dimmed = ctx && stage >= ctx->length;
	int len = (int) std::round(valueOf(this, 1.f));
	int mode = (module && modeParam >= 0) ? (int) std::round(module->params[modeParam].getValue()) : 1;
	for (int n = 0; n < 8; n++) {
		float x = mm((n % 4) * (lay::LEN_CELL_W + lay::LEN_GAP));
		float y = mm((n / 4) * (lay::LEN_CELL_H + lay::LEN_GAP));
		lay::Rgb c = n < len ? (fires(mode, n) ? t.accentLight : t.mid) : t.off;
		rect(args.vg, x, y, mm(lay::LEN_CELL_W), mm(lay::LEN_CELL_H), mm(lay::R_SMALL), dim(t, col(c), dimmed));
	}
	app::ParamWidget::draw(args);
}

void LenCells::onButton(const ButtonEvent& e) {
	app::ParamWidget::onButton(e);
	if (leftPress(e)) {
		int c = clamp((int) (e.pos.x / mm(lay::LEN_CELL_W + lay::LEN_GAP)), 0, 3);
		int r = e.pos.y >= mm(lay::LEN_CELL_H + lay::LEN_GAP / 2) ? 1 : 0;
		setWithUndo(module, paramId, (float) (r * 4 + c + 1));
		e.consume(this);
	}
}

// ---- Screen

Screen::Screen() {
	forceLinear = true;
}

std::string Screen::valueText() {
	int v = (int) std::round(valueOf(this, def));
	switch (kind) {
		case SCREEN_MODE: {
			std::vector<std::string> l = switchLabels(module, paramId);
			if (l.empty())
				return "Once";
			return l[clamp(v, 0, (int) l.size() - 1)];
		}
		case SCREEN_TRANSPOSE: return v > 0 ? "+" + std::to_string(v) : std::to_string(v);
		case SCREEN_RESTART: return RESTART_TEXT[clamp(v, 0, 4)];
		case SCREEN_EVERY: return EVERY_TEXT[clamp(v, 0, 2)];
		default: return std::to_string(v);
	}
}

void Screen::onDragStart(const DragStartEvent& e) {
	moved = 0.f;
	engine::ParamQuantity* pq = getParamQuantity();
	if (pq) {
		// One step per 12 px of drag, Transpose per 5 px.
		float range = std::max(pq->getMaxValue() - pq->getMinValue(), 1.f);
		float px = kind == SCREEN_TRANSPOSE ? 5.f : 12.f;
		speed = 1.f / (settings::knobLinearSensitivity * range * px);
		if (kind == SCREEN_WIDE)
			speed = 1.f / (settings::knobLinearSensitivity * 200.f);	// full range over 200 px
	}
	app::Knob::onDragStart(e);
}

void Screen::onDragMove(const DragMoveEvent& e) {
	moved += std::fabs(e.mouseDelta.x) + std::fabs(e.mouseDelta.y);
	app::Knob::onDragMove(e);
}

void Screen::onDragEnd(const DragEndEvent& e) {
	app::Knob::onDragEnd(e);
	// A click without a drag opens the list (Mode, Restart, Every).
	if (e.button != GLFW_MOUSE_BUTTON_LEFT || moved > 3.f || !module)
		return;
	if (kind == SCREEN_MODE || kind == SCREEN_RESTART || kind == SCREEN_EVERY) {
		ui::Menu* menu = createMenu();
		engine::ParamQuantity* pq = getParamQuantity();
		if (pq)
			menu->addChild(createMenuLabel(pq->getLabel()));
		addValueItems(menu, module, paramId);
	}
}

void Screen::draw(const DrawArgs& args) {
	const lay::Theme& t = themeOf(ctx);
	bool dimmed = ctx && stage >= 0 && stage >= ctx->length;
	if (autoParam >= 0)
		dimmed = !module || module->params[autoParam].getValue() < 0.5f;
	rect(args.vg, 0, 0, box.size.x, box.size.y, mm(lay::R_CELL), dim(t, col(t.screen), dimmed));
	std::string s = valueText();
	xlw::text(args.vg, lay::FONT_SCREEN, lay::SCREEN_PX, dim(t, col(t.screenText), dimmed), box.size.x / 2,
		box.size.y / 2, 0, s.c_str());
	app::Knob::draw(args);
}

// ---- ScaleScreen

void ScaleScreen::draw(const DrawArgs& args) {
	const lay::Theme& t = themeOf(ctx);
	rect(args.vg, 0, 0, box.size.x, box.size.y, mm(lay::R_CELL), col(t.screen));
	std::string s = "Off";
	if (module && module->params[scaleParam].getValue() > 0.5f) {
		s = module->paramQuantities[rootParam]->getDisplayValueString() + " "
			+ module->paramQuantities[scaleParam]->getDisplayValueString();
	}
	xlw::text(args.vg, lay::FONT_SCREEN, lay::SCREEN_PX, col(t.screenText), box.size.x / 2, box.size.y / 2, 0, s.c_str());
	widget::TransparentWidget::draw(args);
}

void ScaleHit::onButton(const ButtonEvent& e) {
	app::ParamWidget::onButton(e);
	if (!leftPress(e) || !module)
		return;
	e.consume(this);
	engine::Module* m = module;
	int rp = rootParam;
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel("Scale"));
	menu->addChild(createSubmenuItem("Root", m->paramQuantities[rp]->getDisplayValueString(),
		[=](ui::Menu* sub) { addValueItems(sub, m, rp); }));
	menu->addChild(new ui::MenuSeparator);
	addValueItems(menu, m, scaleParam);
}

// ---- DirectionSelector

void DirectionSelector::draw(const DrawArgs& args) {
	const lay::Theme& t = themeOf(ctx);
	int v = (int) std::round(valueOf(this, 0.f));
	float g = mm(lay::SEG_GAP);
	float w = (box.size.x - 3 * g) / 4;
	for (int i = 0; i < 4; i++) {
		float x = i * (w + g);
		bool on = i == v;
		rect(args.vg, x, 0, w, box.size.y, mm(lay::R_CELL), col(on ? t.accentLight : t.off));
		drawDirection(args.vg, i, x + w / 2, box.size.y / 2, col(on ? t.onAccent : t.text));
	}
	app::ParamWidget::draw(args);
}

void DirectionSelector::onButton(const ButtonEvent& e) {
	app::ParamWidget::onButton(e);
	if (leftPress(e)) {
		int i = clamp((int) (e.pos.x / (box.size.x / 4)), 0, 3);
		setWithUndo(module, paramId, (float) i);
		e.consume(this);
	}
}

// ---- PlayLight

PlayLight::PlayLight() {
	box.size = mm2px(math::Vec(2.f, 2.f));
	addBaseColor(nvgRGB(0, 0, 0));
	borderColor = nvgRGBA(0, 0, 0, 0);
}

// Rack blends its lights additively, which makes a dark light on a light panel invisible, so the
// dot is drawn here as a plain fill: unlit with the panel, lit on the light layer.
static void playDot(const widget::Widget::DrawArgs& args, math::Vec size, NVGcolor c) {
	nvgBeginPath(args.vg);
	nvgCircle(args.vg, size.x / 2, size.y / 2, std::min(size.x, size.y) / 2);
	nvgFillColor(args.vg, c);
	nvgFill(args.vg);
}

void PlayLight::draw(const DrawArgs& args) {
	playDot(args, box.size, col(themeOf(ctx).off));
}

void PlayLight::drawLayer(const DrawArgs& args, int layer) {
	if (layer != 1 || !module)
		return;
	if (module->lights[firstLightId].getBrightness() > 0.5f)
		playDot(args, box.size, col(themeOf(ctx).accentLight));
}

// ---- LabelLayer

void LabelLayer::draw(const DrawArgs& args) {
	const lay::Theme& t = themeOf(ctx);
	for (int i = 0; i < count; i++) {
		const lay::Label& l = table[i];
		NVGcolor c = col(l.tone == lay::TONE_TEXT_DARK ? t.textDark : t.text);
		xlw::text(args.vg, l.font, l.size, c, mm(l.x), mm(l.y), l.align, l.text, l.spacing);
	}
	widget::TransparentWidget::draw(args);
}

} // namespace xlw
