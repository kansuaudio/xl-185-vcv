#pragma once
#include "plugin.hpp"
#include "Layout.hpp"

// Panel parts drawn in code. Every control is a ParamWidget bound to its param, so the tooltip,
// right-click menu (value entry, MIDI map, initialize) and undo of manual edits stay Rack's own.

namespace xlw {

// What the parts read every frame; the module widget keeps it current.
struct Ctx {
	int theme = lay::THEME_KANSU;
	int length = 8;	// stages beyond it are drawn dimmed; 0 dims all
	int playing = 0;	// playing stage 1..8 for the expander's bars, 0 none
};

enum Glyph { GLYPH_NONE, GLYPH_DIE, GLYPH_LEFT, GLYPH_RIGHT, GLYPH_PLAY };

enum ScreenKind { SCREEN_MODE, SCREEN_LENGTH, SCREEN_TRANSPOSE, SCREEN_RESTART, SCREEN_EVERY, SCREEN_WIDE };
// SCREEN_WIDE: a plain number with a wide range (expander Min, Max, Smooth), full range over a short drag.

math::Rect mmRect(const lay::Box& b, float dx = 0.f);

// Lit cell: a latch (On, Slide, Lock, Run, Auto) or, with momentary set, a button that lights while held.
struct CellButton : app::Switch {
	const Ctx* ctx = NULL;
	std::string text;
	int glyph = GLYPH_NONE;
	int stage = -1;	// dims beyond Length when set
	float def = 0.f;
	void draw(const DrawArgs& args) override;
};

// Velocity and Probability: vertical drag, lit fill from the bottom.
struct BarSlider : app::SliderKnob {
	const Ctx* ctx = NULL;
	int stage = 0;
	float def = 0.f;
	bool markPlaying = false;	// expander: the playing stage's track in the mid tone
	BarSlider();
	void onDragStart(const DragStartEvent& e) override;
	void draw(const DrawArgs& args) override;
};

// Stage Len: 2 x 4 cells, click a cell to set it.
struct LenCells : app::ParamWidget {
	const Ctx* ctx = NULL;
	int stage = 0;
	int modeParam = -1;
	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override {}
};

// A small screen with a value: vertical drag steps it, a click opens the list where there is one.
struct Screen : app::Knob {
	const Ctx* ctx = NULL;
	int kind = SCREEN_LENGTH;
	int stage = -1;	// mode cells dim beyond Length
	int autoParam = -1;	// Every dims while Auto is off
	float def = 0.f;
	float moved = 0.f;
	Screen();
	std::string valueText();
	void onDragStart(const DragStartEvent& e) override;
	void onDragMove(const DragMoveEvent& e) override;
	void onDragEnd(const DragEndEvent& e) override;
	void draw(const DrawArgs& args) override;
};

// Root and Scale on one screen: the screen draws, two ParamWidgets on top take the clicks.
struct ScaleScreen : widget::TransparentWidget {
	const Ctx* ctx = NULL;
	engine::Module* module = NULL;
	int rootParam = -1;
	int scaleParam = -1;
	void draw(const DrawArgs& args) override;
};

struct ScaleHit : app::ParamWidget {
	int rootParam = -1;
	int scaleParam = -1;
	void onButton(const ButtonEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override {}
};

// Direction: four segments with arrow glyphs, the active one lit.
struct DirectionSelector : app::ParamWidget {
	const Ctx* ctx = NULL;
	void draw(const DrawArgs& args) override;
	void onButton(const ButtonEvent& e) override;
	void onDoubleClick(const DoubleClickEvent& e) override {}
};

// Playing stage: a flat dot in the theme tones, drawn as a plain fill.
struct PlayLight : app::ModuleLightWidget {
	const Ctx* ctx = NULL;
	PlayLight();
	void draw(const DrawArgs& args) override;
	void drawLayer(const DrawArgs& args, int layer) override;
};

// Every label, area title, the name in the header and the footer words.
struct LabelLayer : widget::TransparentWidget {
	const Ctx* ctx = NULL;
	const lay::Label* table = lay::LABELS;
	int count = lay::LABEL_COUNT;
	void draw(const DrawArgs& args) override;
};

} // namespace xlw
