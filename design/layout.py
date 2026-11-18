#!/usr/bin/env python3
# XL-185 and XL-185 MOD panels: the one table of areas, controls, labels and colours.
# Writes res/XL185-{kansu,ableton}.svg, res/XL185Mod-{kansu,ableton}.svg, src/Layout.hpp, src/LayoutMod.hpp,
# design/preview-{kansu,ableton}.png, preview-mod-*.png, preview-pair-*.png and design/RULER.json.
# Units mm, origin top left. Run from anywhere: python3 design/layout.py

import json
import math
import os
import re
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT_DIR = '/Applications/VCV Rack 2 Pro.app/Contents/Resources/res/fonts/'
LOGO_PNG = os.path.join(ROOT, '..', 'website', 'assets', 'brand', 'kansu-icon-1024.png')
LOGO_TXT = os.path.join(HERE, 'logo_path.txt')

W, H = 182.88, 128.5
PX_MM = 75.0 / 25.4	# Rack px per mm at 100 % zoom
PREVIEW_MM = 4.0	# preview px per mm

# ---------------------------------------------------------------- colours

TOKENS = ['panel', 'area', 'dark', 'text', 'textDark', 'screen', 'screenText',
	'accentLight', 'accentDark', 'onAccent', 'off', 'mid']
THEMES = [
	('Kansu', {
		'panel': '#E8E4DB', 'area': '#DAD5C9', 'dark': '#1B1A18',
		'text': '#1A1917', 'textDark': '#E8E4DB',
		'screen': '#1B1A18', 'screenText': '#E8E4DB',
		'accentLight': '#1A1917', 'accentDark': '#E8E4DB', 'onAccent': '#E8E4DB',
		'off': '#C5BFB1', 'mid': '#8E887C',
	}),
	('Ableton', {
		'panel': '#BEBEBE', 'area': '#D6D6D6', 'dark': '#3A3A3A',
		'text': '#121212', 'textDark': '#E6E6E6',
		'screen': '#EDEDED', 'screenText': '#121212',
		'accentLight': '#FFAD56', 'accentDark': '#FFAD56', 'onAccent': '#121212',
		'off': '#9D9D9D', 'mid': '#626262',
	}),
]
DIM = 0.4	# beyond Length: 40 % toward the area tone

def rgb(h):
	return (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16))

def mix(a, b, t):
	return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))

def lum(c):
	def ch(v):
		v = v / 255.0
		return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
	return 0.2126 * ch(c[0]) + 0.7152 * ch(c[1]) + 0.0722 * ch(c[2])

def contrast(a, b):
	la, lb = lum(a), lum(b)
	return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)

# ---------------------------------------------------------------- fonts

# Labels and screens from Rack's own fonts; names and footer words in the brand's title font, shipped in res/fonts.
FONTS = {'label': 'Nunito-Bold.ttf', 'screen': 'ShareTechMono-Regular.ttf', 'title': 'JetBrainsMono-Bold.ttf'}
FONT_PATHS = {'label': FONT_DIR + FONTS['label'], 'screen': FONT_DIR + FONTS['screen'],
	'title': os.path.join(ROOT, 'res', 'fonts', FONTS['title'])}
LABEL_PX = 11.0	# nanovg font size, Rack px
TITLE_PX = 11.0
SCREEN_PX = 12.0
HEADER_PX = 20.0
NAME_SP = 0.5	# letter spacing of the names, Rack px (nvgTextLetterSpacing, between glyphs)
FOOT_PX = 11.0	# footer words
FOOT_SP = 1.5

def hhea(path):
	d = open(path, 'rb').read()
	n = struct.unpack('>H', d[4:6])[0]
	t = {}
	for i in range(n):
		tag, _, off, _ = struct.unpack('>4sIII', d[12 + 16 * i:28 + 16 * i])
		t[tag] = off
	upem = struct.unpack('>H', d[t[b'head'] + 18:t[b'head'] + 20])[0]
	asc, desc = struct.unpack('>hh', d[t[b'hhea'] + 4:t[b'hhea'] + 8])
	return upem, asc, desc

FONT_METRICS = {k: hhea(v) for k, v in FONT_PATHS.items()}
_font_cache = {}

def pil_font(font, size_px, px_per_mm):
	# nanovg sizes a font by ascender - descender (stbtt_ScaleForPixelHeight); PIL by the em.
	upem, asc, desc = FONT_METRICS[font]
	em = size_px * upem / float(asc - desc) / PX_MM * px_per_mm
	key = (font, round(em * 100))
	if key not in _font_cache:
		_font_cache[key] = ImageFont.truetype(FONT_PATHS[font], max(1, int(round(em))))
	return _font_cache[key], em

def baseline_offset(font, size_px):
	# NVG_ALIGN_MIDDLE puts the baseline this far below y (mm).
	upem, asc, desc = FONT_METRICS[font]
	return size_px * (asc + desc) / (2.0 * (asc - desc)) / PX_MM

def text_box(text, font, size_px, x, y, align, sp=0.0):
	# Ink box (mm) of text drawn at x, y with NVG_ALIGN_MIDDLE and align -1 left, 0 centre, 1 right.
	# sp: letter spacing in Rack px, added between glyphs as nanovg does.
	f, em = pil_font(font, size_px, 40.0)
	scale = em / round(em)
	extra = sp * max(0, len(text) - 1) / PX_MM
	wid = f.getlength(text) * scale / 40.0 + extra
	l, t, r, b = f.getbbox(text, anchor='ls')
	base = y + baseline_offset(font, size_px)
	x0 = x - (wid if align == 1 else wid / 2 if align == 0 else 0)
	return (x0 + l * scale / 40.0, base + t * scale / 40.0, r * scale / 40.0 - l * scale / 40.0 + extra,
		(b - t) * scale / 40.0)

def text_width(text, font, size_px, sp=0.0):
	f, em = pil_font(font, size_px, 40.0)
	return f.getlength(text) * em / round(em) / 40.0 + sp * max(0, len(text) - 1) / PX_MM

# ---------------------------------------------------------------- geometry

M = 3.5	# outer margin
PAD = 1.6	# inner padding of an area
GAP = 2.0	# between areas
R_AREA = 1.5
R_CELL = 0.5
R_SMALL = 0.3	# bars and Len cells
SEG_GAP = 0.6	# between the Direction segments
GLYPH_STROKE = 0.5
CLEAR = 1.2
CLEAR_JACK = 2.0

KNOB_D = 28.34759 / PX_MM	# RoundBlackKnob
SMALL_KNOB_D = 22.67578 / PX_MM	# RoundSmallBlackKnob
JACK_D = 23.7 / PX_MM	# PJ301M
LIGHT_D = 2.0	# SmallSimpleLight
SCREW_D = 15.0 / PX_MM

LEFT_X0, LEFT_X1 = M, 45.5
RIGHT_X0, RIGHT_X1 = LEFT_X1 + GAP, W - M
TOP = 13.0
STAGES_Y1 = 82.0
GATES_Y0, GATES_Y1 = STAGES_Y1 + GAP, 96.0
SETTINGS_Y1 = 66.5
IO_Y0, IO_Y1 = GATES_Y1 + GAP, 122.4
IN_X1 = 86.0

AREAS = [
	# name, title, x0, y0, x1, y1, tone
	('settings', 'SETTINGS', LEFT_X0, TOP, LEFT_X1, SETTINGS_Y1, 'area'),
	('tools', 'TOOLS', LEFT_X0, SETTINGS_Y1 + GAP, LEFT_X1, GATES_Y1, 'area'),
	('stages', 'STAGES', RIGHT_X0, TOP, RIGHT_X1, STAGES_Y1, 'area'),
	('gates', 'GATES', RIGHT_X0, GATES_Y0, RIGHT_X1, GATES_Y1, 'dark'),
	('in', 'IN', LEFT_X0, IO_Y0, IN_X1, IO_Y1, 'area'),
	('out', 'OUT', IN_X1 + GAP, IO_Y0, RIGHT_X1, IO_Y1, 'dark'),
]
AREA = {a[0]: a for a in AREAS}

class Panel:
	# One module face: its areas, controls and labels, read by the ruler, the SVG and the preview.
	def __init__(self, key, w, areas, header_y1, src):
		self.key = key
		self.w = w
		self.areas = areas
		self.area = {a[0]: a for a in areas}
		self.header_y1 = header_y1
		self.src = src
		self.controls = []	# dicts: name, kind, area, box (x, y, w, h), ids, label, extra
		self.labels = []	# dicts: text, x, y, size, font, align, tone, area, box
		self.screws = [(7.62, 2.54), (w - 7.62, 2.54), (7.62, H - 2.54), (w - 7.62, H - 2.54)]

SEQ = Panel('XL185', W, AREAS, TOP - 0.5, 'XL185.cpp')
CUR = SEQ	# the panel add_label and add_control write to
controls = SEQ.controls
labels = SEQ.labels

def add_label(text, x, y, area, align=0, size=LABEL_PX, font='label', tone=None, role='label', sp=0.0):
	if tone is None:
		tone = 'textDark' if area in CUR.area and CUR.area[area][6] == 'dark' else 'text'
	lb = {'text': text, 'x': x, 'y': y, 'size': size, 'font': font, 'align': align, 'tone': tone,
		'area': area, 'role': role, 'sp': sp, 'box': text_box(text, font, size, x, y, align, sp)}
	CUR.labels.append(lb)
	return lb

def add_control(name, kind, area, box, ids, label, **extra):
	c = {'name': name, 'kind': kind, 'area': area, 'box': box, 'ids': ids, 'label': label}
	c.update(extra)
	CUR.controls.append(c)
	return c

def circle_box(cx, cy, d):
	return (cx - d / 2, cy - d / 2, d, d)

def area_title(area):
	a = CUR.area[area]
	return add_label(a[1], a[2] + PAD, a[3] + 3.3, area, align=-1, size=TITLE_PX, role='title')

for a in AREAS:
	area_title(a[0])

# Header: the name where the mark started (the top left screw keeps it off the content edge), shift arrows.
HEADER = 'header'
NAME_X, NAME_Y = 91.44, 6.15	# panel centre
add_label('XL-185', NAME_X, NAME_Y, HEADER, align=0, size=HEADER_PX, font='title', role='name', sp=NAME_SP)
SHIFT_W, SHIFT_H, SHIFT_Y = 6.4, 5.0, 3.7
SHIFT_RX = 169.0	# right edge of the right arrow
add_control('SHIFT_RIGHT', 'push', HEADER, (SHIFT_RX - SHIFT_W, SHIFT_Y, SHIFT_W, SHIFT_H), ['SHIFT_RIGHT_PARAM'],
	'shift', glyph='right')
add_control('SHIFT_LEFT', 'push', HEADER, (SHIFT_RX - 2 * SHIFT_W - CLEAR, SHIFT_Y, SHIFT_W, SHIFT_H),
	['SHIFT_LEFT_PARAM'], 'shift', glyph='left')
add_label('shift', SHIFT_RX - 2 * SHIFT_W - CLEAR - 1.6, SHIFT_Y + SHIFT_H / 2, HEADER, align=1)

# Settings.
sx0 = LEFT_X0 + PAD
sx1 = LEFT_X1 - PAD
sw = sx1 - sx0
SCREEN_H = 5.8
y = 20.7
add_label('scale', sx0, y, 'settings', align=-1)
y += 2.4
ROOT_W = 7.0
add_control('SCALE_SCREEN', 'scalescreen', 'settings', (sx0, y, sw, SCREEN_H), ['ROOT_PARAM', 'SCALE_PARAM'], 'scale',
	root_w=ROOT_W)
y += SCREEN_H + 3.1
add_label('direction', sx0, y, 'settings', align=-1)
y += 2.4
add_control('DIRECTION', 'direction', 'settings', (sx0, y, sw, SCREEN_H), ['DIRECTION_PARAM'], 'direction')
y += SCREEN_H + 3.1
# Three number screens; each as wide as its label so the labels keep clear.
names = [('length', 'LENGTH', 'LENGTH_PARAM'), ('transpose', 'TRANSPOSE', 'TRANSPOSE_PARAM'),
	('restart', 'RESTART', 'RESTART_PARAM')]
widths = [max(text_width(n[0], 'label', LABEL_PX), 9.0) for n in names]
spare = (sw - sum(widths)) / 2.0
x = sx0
for (text, name, pid), wd in zip(names, widths):
	add_label(text, x + wd / 2, y, 'settings')
	add_control(name, 'number', 'settings', (x, y + 2.9, wd, SCREEN_H), [pid], text)
	x += wd + spare
y += 2.9 + SCREEN_H + 1.6
KNOB_Y = y + SMALL_KNOB_D / 2
for k, (text, name, pid) in enumerate([('swing', 'SWING', 'SWING_PARAM'), ('gate', 'GATE', 'GATE_PARAM'),
		('glide', 'GLIDE', 'GLIDE_PARAM')]):
	cx = sx0 + sw * (k + 0.5) / 3.0
	add_control(name, 'smallknob', 'settings', circle_box(cx, KNOB_Y, SMALL_KNOB_D), [pid], text)
	add_label(text, cx, KNOB_Y + SMALL_KNOB_D / 2 + 2.6, 'settings')

# Tools.
t = AREA['tools']
BTN_H = 6.8
ty0 = t[3] + 6.6
add_control('GLOBAL', 'push', 'tools', (sx0, ty0, (sw - CLEAR * 1.5) / 2, BTN_H), ['GLOBAL_PARAM'], 'global',
	text='global')
add_control('MUTATE', 'push', 'tools', (sx0 + (sw + CLEAR * 1.5) / 2, ty0, (sw - CLEAR * 1.5) / 2, BTN_H),
	['MUTATE_PARAM'], 'mutate', text='mutate')
ty1 = ty0 + BTN_H + 2.2
AUTO_W = 10.4
EVERY_W = 13.4
add_control('AUTO', 'cell', 'tools', (sx0, ty1, AUTO_W, BTN_H), ['AUTO_PARAM'], 'auto', text='auto')
add_control('EVERY', 'number', 'tools', (sx0 + AUTO_W + 0.9, ty1, EVERY_W, BTN_H), ['EVERY_PARAM'], 'auto')
reset_x = sx0 + AUTO_W + 0.9 + EVERY_W + CLEAR * 1.5
add_control('RESET', 'push', 'tools', (reset_x, ty1, sx1 - reset_x, BTN_H), ['RESET_TOOL_PARAM'], 'reset',
	text='reset')

# Stages: gutter, then eight columns.
st = AREA['stages']
GUTTER_X = st[2] + PAD
GUTTER_W = 17.0
COL0 = st[2] + GUTTER_W + 0.6
COL_W = (st[4] - 0.6 - COL0) / 8.0
STAGE_X = [COL0 + COL_W * (i + 0.5) for i in range(8)]

LIGHT_Y = 16.3
ON_BOX = (-4.5, 19.4, 9.0, 5.0)	# x relative to the column centre
PITCH_Y = 30.9
BAR_W, BAR_DX = 3.2, 3.3
BAR_Y0, BAR_Y1 = 37.4, 58.0
VEL_BOX = (-BAR_DX - BAR_W / 2, BAR_Y0, BAR_W, BAR_Y1 - BAR_Y0)
PROB_BOX = (BAR_DX - BAR_W / 2, BAR_Y0, BAR_W, BAR_Y1 - BAR_Y0)
LEN_CELL_W, LEN_CELL_H, LEN_GAP = 2.7, 2.6, 0.5
LEN_W = 4 * LEN_CELL_W + 3 * LEN_GAP
LEN_BOX = (-LEN_W / 2, 59.8, LEN_W, 2 * LEN_CELL_H + LEN_GAP)
MODE_BOX = (-6.2, 67.1, 12.4, 5.0)
SL_W, SL_H = 5.2, 4.2
SLIDE_BOX = (-BAR_DX - SL_W / 2, 74.0, SL_W, SL_H)
LOCK_BOX = (BAR_DX - SL_W / 2, 74.0, SL_W, SL_H)
GATE_Y = (GATES_Y0 + GATES_Y1) / 2 + 0.6

def rel(box, cx):
	return (cx + box[0], box[1], box[2], box[3])

for i, cx in enumerate(STAGE_X):
	n = str(i)
	add_control('LIGHT' + n, 'light', 'stages', circle_box(cx, LIGHT_Y, LIGHT_D), [], 'STAGES', light='STAGE_LIGHTS+' + n)
	add_control('ON' + n, 'stagecell', 'stages', rel(ON_BOX, cx), ['ON_PARAMS+' + n], 'on')
	add_control('PITCH' + n, 'knob', 'stages', circle_box(cx, PITCH_Y, KNOB_D), ['PITCH_PARAMS+' + n], 'pitch')
	add_control('VEL' + n, 'bar', 'stages', rel(VEL_BOX, cx), ['VEL_PARAMS+' + n], 'vel / prob')
	add_control('PROB' + n, 'bar', 'stages', rel(PROB_BOX, cx), ['PROB_PARAMS+' + n], 'vel / prob')
	add_control('LEN' + n, 'lencells', 'stages', rel(LEN_BOX, cx), ['LEN_PARAMS+' + n], 'len')
	add_control('MODE' + n, 'mode', 'stages', rel(MODE_BOX, cx), ['MODE_PARAMS+' + n], 'mode')
	add_control('SLIDE' + n, 'cell', 'stages', rel(SLIDE_BOX, cx), ['SLIDE_PARAMS+' + n], 'sl / lk', text='S')
	add_control('LOCK' + n, 'cell', 'stages', rel(LOCK_BOX, cx), ['LOCK_PARAMS+' + n], 'sl / lk', text='L')
	add_control('GATE' + n, 'darkjack', 'gates', circle_box(cx, GATE_Y, JACK_D), ['STAGE_GATE_OUTPUTS+' + n], 'GATES')

add_label('on', GUTTER_X, ON_BOX[1] + ON_BOX[3] / 2, 'stages', align=-1)
add_label('pitch', GUTTER_X, PITCH_Y - 2.4, 'stages', align=-1)
DIE_W, DIE_H = 5.4, 4.6
add_control('PITCH_DIE', 'push', 'stages', (GUTTER_X, PITCH_Y + 0.4, DIE_W, DIE_H), ['PITCH_DIE_PARAM'], 'pitch',
	glyph='die')
add_label('vel / prob', GUTTER_X, BAR_Y0 + 1.4, 'stages', align=-1)
add_control('VEL_DIE', 'push', 'stages', (GUTTER_X, BAR_Y0 + 4.2, DIE_W, DIE_H), ['VEL_DIE_PARAM'], 'vel / prob',
	glyph='die')
add_label('len', GUTTER_X, LEN_BOX[1] + LEN_BOX[3] / 2, 'stages', align=-1)
add_label('mode', GUTTER_X, MODE_BOX[1] + MODE_BOX[3] / 2, 'stages', align=-1)
add_label('sl / lk', GUTTER_X, SLIDE_BOX[1] + SL_H / 2, 'stages', align=-1)

# In and Out: a label row over one row of jacks, spread over the area.
LABEL_ROW_Y = IO_Y0 + 7.0
JACK_ROW_Y = IO_Y0 + 14.8
RUN_W = 6.0

def jack_row(area, items, kind):
	a = CUR.area[area]
	x0, x1 = a[2] + PAD, a[4] - PAD
	widths = []
	for it in items:
		body = JACK_D + (CLEAR_JACK + RUN_W if it[0] == 'run' else 0)
		widths.append(max(body, text_width(it[0], 'label', LABEL_PX)))
	gap = (x1 - x0 - sum(widths)) / (len(items) - 1)
	x = x0
	for it, wd in zip(items, widths):
		text, name, pid = it
		body = JACK_D + (CLEAR_JACK + RUN_W if text == 'run' else 0)
		bx = x + (wd - body) / 2
		add_label(text, x + wd / 2, LABEL_ROW_Y, area)
		add_control(name, kind, area, circle_box(bx + JACK_D / 2, JACK_ROW_Y, JACK_D), [pid], text)
		if text == 'run':
			add_control('RUN', 'cell', area, (bx + JACK_D + CLEAR_JACK, JACK_ROW_Y - RUN_W / 2, RUN_W, RUN_W), ['RUN_PARAM'],
				'run', glyph='play')
		x += wd + gap

jack_row('in', [('clock', 'CLOCK', 'CLOCK_INPUT'), ('reset', 'RESET_IN', 'RESET_INPUT'), ('run', 'RUN_IN', 'RUN_INPUT'),
	('transpose', 'TRANSPOSE_IN', 'TRANSPOSE_INPUT'), ('mutate', 'MUTATE_IN', 'MUTATE_INPUT'),
	('random', 'RANDOM_IN', 'RANDOM_INPUT')], 'jack')
jack_row('out', [('v/oct', 'VOCT', 'VOCT_OUTPUT'), ('gate', 'GATE_OUT', 'GATE_OUTPUT'), ('vel', 'VEL_OUT', 'VEL_OUTPUT'),
	('slide', 'SLIDE_OUT', 'SLIDE_OUTPUT'), ('stage', 'STAGE_OUT', 'STAGE_OUTPUT'), ('eoc', 'EOC', 'EOC_OUTPUT'),
	('stage cv', 'STAGE_CV', 'STAGE_CV_OUTPUT')], 'darkjack')

# ---------------------------------------------------------------- XL-185 MOD (expander)

MW = 16 * 5.08	# 16 HP
LANE_Y = [13.0, 42.0, 71.0]	# lane area tops
LANE_H = 27.0
LANE_X1 = 63.0
MOUT_X0 = LANE_X1 + 2.5
MBOT_Y0 = LANE_Y[2] + LANE_H + GAP
MTOOLS_X1 = 48.0
MOD_AREAS = [
	('lane0', 'LANE A', M, LANE_Y[0], LANE_X1, LANE_Y[0] + LANE_H, 'area'),
	('lane1', 'LANE B', M, LANE_Y[1], LANE_X1, LANE_Y[1] + LANE_H, 'area'),
	('lane2', 'LANE C', M, LANE_Y[2], LANE_X1, LANE_Y[2] + LANE_H, 'area'),
	('out', 'OUT', MOUT_X0, TOP, MW - M, LANE_Y[2] + LANE_H, 'dark'),
	('tools', 'TOOLS', M, MBOT_Y0, MTOOLS_X1, IO_Y1, 'area'),
	('in', 'IN', MTOOLS_X1 + GAP, MBOT_Y0, MW - M, IO_Y1, 'area'),
]
MOD = Panel('XL185Mod', MW, MOD_AREAS, TOP - 0.5, 'XL185Mod.cpp')
CUR = MOD
for a in MOD_AREAS:
	area_title(a[0])

# Columns: eight stage columns over the lane's inner width, one bar each.
MCOL0, MCOL1 = M + PAD, LANE_X1 - PAD
MCOL_W = (MCOL1 - MCOL0) / 8.0
MCOL_X = [MCOL0 + MCOL_W * (i + 0.5) for i in range(8)]

# Header: the name, then the stage numbers over the columns.
MNAME_X = 40.64	# panel centre
add_label('XL-185 MOD', MNAME_X, NAME_Y, HEADER, align=0, size=HEADER_PX, font='title', role='name', sp=NAME_SP)
MNUM_Y = 10.55
for i, cx in enumerate(MCOL_X):
	add_label(str(i + 1), cx, MNUM_Y, HEADER)

# Lane, y relative to the lane top: title row (die and shift arrows at the right), bars, range row.
LBTN_Y, LBTN_H = 0.9, 4.4
MSHIFT_W, MDIE_W = 6.4, 5.4
MSHIFT_R = (MCOL1 - MSHIFT_W, LBTN_Y, MSHIFT_W, LBTN_H)
MSHIFT_L = (MCOL1 - 2 * MSHIFT_W - CLEAR, LBTN_Y, MSHIFT_W, LBTN_H)
MDIE = (MSHIFT_L[0] - 2.4 - MDIE_W, LBTN_Y, MDIE_W, LBTN_H)
MBAR_W = 4.0
MBAR_BOX = (-MBAR_W / 2, LBTN_Y + LBTN_H + CLEAR, MBAR_W, 13.1)	# x relative to the column centre
MRANGE_Y = MBAR_BOX[1] + MBAR_BOX[3] + CLEAR
MRANGE_H = 4.6
# Min, Max, Smooth: label, then its screen, on one row; screens fit "100" and "2000".
mrange = [('min', 'MIN', 'MIN_PARAMS', 8.0), ('max', 'MAX', 'MAX_PARAMS', 8.0), ('smooth', 'SMOOTH', 'SMOOTH_PARAMS', 10.0)]
mused = sum(text_width(r[0], 'label', LABEL_PX) + CLEAR + r[3] for r in mrange)
mgap = (MCOL1 - MCOL0 - mused) / 2.0
MRANGE = {}
x = MCOL0
for text, name, pid, wd in mrange:
	tw = text_width(text, 'label', LABEL_PX)
	MRANGE[name] = (x, tw, (x + tw + CLEAR, MRANGE_Y, wd, MRANGE_H))
	x += tw + CLEAR + wd + mgap
MJACK_DY = 14.3	# output jack centre below the lane top
MOUT_X = (MOUT_X0 + MW - M) / 2

for l in range(3):
	area = 'lane%d' % l
	title = MOD_AREAS[l][1]
	y0 = LANE_Y[l]
	letter = 'ABC'[l]

	def at(b):
		return (b[0], y0 + b[1], b[2], b[3])
	add_control('DIE_' + letter, 'push', area, at(MDIE), ['DIE_PARAMS+%d' % l], title, glyph='die')
	add_control('SHIFT_LEFT_' + letter, 'push', area, at(MSHIFT_L), ['SHIFT_LEFT_PARAMS+%d' % l], title, glyph='left')
	add_control('SHIFT_RIGHT_' + letter, 'push', area, at(MSHIFT_R), ['SHIFT_RIGHT_PARAMS+%d' % l], title, glyph='right')
	for i, cx in enumerate(MCOL_X):
		add_control('VAL_%s%d' % (letter, i), 'lanebar', area, at(rel(MBAR_BOX, cx)), ['VALUE_PARAMS+%d' % (l * 8 + i)], title,
			lane=l, stage=i)
	for text, name, pid, wd in mrange:
		lx, tw, b = MRANGE[name]
		add_label(text, lx, y0 + MRANGE_Y + MRANGE_H / 2, area, align=-1)
		add_control('%s_%s' % (name, letter), 'number', area, at(b), ['%s+%d' % (pid, l)], text, lane=l, what=name)
	jy = y0 + MJACK_DY
	add_label(letter.lower(), MOUT_X, jy - 7.8, 'out')
	add_control('OUT_' + letter, 'darkjack', 'out', circle_box(MOUT_X, jy, JACK_D), ['LANE_OUTPUTS+%d' % l], letter.lower())

# Tools: Random and Mutate over Auto, its interval and Reset, as on the sequencer.
mt = CUR.area['tools']
mtx0, mtx1 = mt[2] + PAD, mt[4] - PAD
mtw = mtx1 - mtx0
mty0 = mt[3] + 5.8
mty1 = mty0 + BTN_H + CLEAR
mhalf = (mtw - CLEAR * 1.5) / 2
add_control('RANDOM', 'push', 'tools', (mtx0, mty0, mhalf, BTN_H), ['RANDOM_PARAM'], 'random', text='random')
add_control('MUTATE', 'push', 'tools', (mtx1 - mhalf, mty0, mhalf, BTN_H), ['MUTATE_PARAM'], 'mutate', text='mutate')
add_control('AUTO', 'cell', 'tools', (mtx0, mty1, AUTO_W, BTN_H), ['AUTO_PARAM'], 'auto', text='auto')
add_control('EVERY', 'number', 'tools', (mtx0 + AUTO_W + 0.9, mty1, EVERY_W, BTN_H), ['EVERY_PARAM'], 'auto')
mreset_x = mtx0 + AUTO_W + 0.9 + EVERY_W + CLEAR * 1.5
add_control('RESET', 'push', 'tools', (mreset_x, mty1, mtx1 - mreset_x, BTN_H), ['RESET_PARAM'], 'reset', text='reset')

# In: Random and Mutate trigger jacks, in the order of their buttons.
mi = CUR.area['in']
MIN_LABEL_Y = mi[3] + 7.0
MIN_JACK_Y = MIN_LABEL_Y + 7.8
ix0, ix1 = mi[2] + PAD, mi[4] - PAD
iws = [text_width(t, 'label', LABEL_PX) for t in ('random', 'mutate')]
igap = (ix1 - ix0 - sum(iws)) / 1.0
x = ix0
for (text, name, pid), wd in zip([('random', 'RANDOM_IN', 'RANDOM_INPUT'), ('mutate', 'MUTATE_IN', 'MUTATE_INPUT')], iws):
	add_label(text, x + wd / 2, MIN_LABEL_Y, 'in')
	add_control(name, 'jack', 'in', circle_box(x + wd / 2, MIN_JACK_Y, JACK_D), [pid], text)
	x += wd + igap

CUR = SEQ
PANELS = [SEQ, MOD]

# ---------------------------------------------------------------- ruler

def enum_ids(src, name):
	m = re.search(r'enum ' + name + r' \{(.*?)\};', src, re.S)
	out = []
	body = re.sub(r'//[^\n]*', '', m.group(1))
	for e in re.finditer(r'ENUMS\((\w+),\s*(\d+)\)|(\w+)', body):
		if e.group(1):
			out += [e.group(1) + '+' + str(i) for i in range(int(e.group(2)))]
		elif not e.group(3).endswith('_LEN'):
			out.append(e.group(3))
	return out

def gap(a, b):
	dx = max(b[0] - (a[0] + a[2]), a[0] - (b[0] + b[2]))
	dy = max(b[1] - (a[1] + a[3]), a[1] - (b[1] + b[3]))
	if dx < 0 and dy < 0:
		return max(dx, dy)	# overlap, negative
	return max(dx, dy)

def inside(box, x0, y0, x1, y1, eps=1e-6):
	return box[0] >= x0 - eps and box[1] >= y0 - eps and box[0] + box[2] <= x1 + eps and box[1] + box[3] <= y1 + eps

def area_rect(pn, name):
	if name == HEADER:
		return (0, 0, pn.w, pn.header_y1)
	if name == FOOTER:
		return (0, IO_Y1, pn.w, H)
	a = pn.area[name]
	return (a[2], a[3], a[4], a[5])

def ruler(pn):
	v = []
	src = open(os.path.join(ROOT, 'src', pn.src)).read()
	want = {'param': enum_ids(src, 'ParamId'), 'input': enum_ids(src, 'InputId'), 'output': enum_ids(src, 'OutputId')}
	controls, labels = pn.controls, pn.labels
	have = {}
	for c in controls:
		for i in c['ids']:
			have[i] = have.get(i, 0) + 1
	counts = {}
	for kind, ids in want.items():
		counts[kind] = len(ids)
		for i in ids:
			if have.get(i, 0) != 1:
				v.append('%s %s on the panel %d times' % (kind, i, have.get(i, 0)))
	known = set(sum(want.values(), []))
	for i in have:
		if i not in known:
			v.append('unknown id ' + i)
	items = [('control', c['name'], c['area'], c['box'], c['kind'] in ('jack', 'darkjack')) for c in controls]
	items += [('label', l['text'], l['area'], l['box'], False) for l in labels]
	items.append(('logo', 'mark', FOOTER, logo_box(pn), False))
	# Footer group: inside the panel, 1.2 from the screws, 0.8 from the panel bottom and the areas above.
	for kind, name, area, box, _ in items:
		if area != FOOTER:
			continue
		if box[1] + box[3] > H - 0.8 + 1e-6:
			v.append('%s %s within 0.8 of the panel bottom' % (kind, name))
		for sx, sy in pn.screws:
			if gap(box, circle_box(sx, sy, SCREW_D)) < 1.2:
				v.append('%s %s within 1.2 of a screw' % (kind, name))
		for a in pn.areas:
			if gap(box, (a[2], a[3], a[4] - a[2], a[5] - a[3])) < 0.8:
				v.append('%s %s within 0.8 of area %s' % (kind, name, a[0]))
	for kind, name, area, box, _ in items:
		if not inside(box, 0, 0, pn.w, H):
			v.append('%s %s outside the panel' % (kind, name))
		r = area_rect(pn, area)
		if not inside(box, r[0] + 0.3, r[1] + 0.3, r[2] - 0.3, r[3] - 0.3):
			v.append('%s %s outside its area %s' % (kind, name, area))
		for sx, sy in pn.screws:
			if gap(box, circle_box(sx, sy, SCREW_D)) < 0.8:
				v.append('%s %s too close to a screw' % (kind, name))
	for sx, sy in pn.screws:
		for a in pn.areas:
			if gap((a[2], a[3], a[4] - a[2], a[5] - a[3]), circle_box(sx, sy, SCREW_D)) < 0.5:
				v.append('screw at %.2f %.2f on area %s' % (sx, sy, a[0]))
	for i in range(len(items)):
		for j in range(i + 1, len(items)):
			a, b = items[i], items[j]
			need = CLEAR_JACK if (a[4] or b[4]) else CLEAR
			if a[1] in ('AUTO', 'EVERY') and b[1] in ('AUTO', 'EVERY'):
				need = 0.8	# one pair: Auto and its interval
			g = gap(a[3], b[3])
			if g < need - 1e-6:
				v.append('%s %s and %s %s: %.2f mm clear, need %.1f' % (a[0], a[1], b[0], b[1], g, need))
	texts = set(l['text'] for l in labels) | set(c['text'] for c in controls if c.get('text'))
	for c in controls:
		if c['label'] not in texts and c['label'] not in [a[1] for a in pn.areas]:
			v.append('control %s has no label "%s"' % (c['name'], c['label']))
	for l in labels:
		if l['size'] < 10.0:
			v.append('label %s below 10 px' % l['text'])
	return {'ok': not v, 'violations': v, 'controls': len(controls), 'labels': len(labels), 'ids': counts}

def ruler_themes():
	# Contrast of every text colour on its background, both themes; shared by both panels.
	v = []
	if min(SCREEN_PX, LABEL_PX) < 10.0:
		v.append('screen or cell text below 10 px')
	for tname, th in THEMES:
		T = {k: rgb(x) for k, x in th.items()}
		pairs = [('text', 'panel'), ('text', 'area'), ('textDark', 'dark'), ('screenText', 'screen'),
			('onAccent', 'accentLight'), ('text', 'off')]
		for fg, bg in pairs:
			c = contrast(T[fg], T[bg])
			if c < 4.5:
				v.append('%s: %s on %s contrast %.2f' % (tname, fg, bg, c))
		for fg, bg in [('screenText', 'screen'), ('onAccent', 'accentLight'), ('text', 'off')]:
			c = contrast(mix(T[fg], T['area'], DIM), mix(T[bg], T['area'], DIM))
			if c < 3.0:
				v.append('%s: dimmed %s on %s contrast %.2f' % (tname, fg, bg, c))
		for fg in ['accentLight', 'mid']:
			c = contrast(T[fg], T['off'])
			if c < 1.45:
				v.append('%s: %s against off only %.2f' % (tname, fg, c))
		# The expander's playing bar: lit fill on a mid track.
		c = contrast(T['accentLight'], T['mid'])
		if c < 1.45:
			v.append('%s: accentLight against mid only %.2f' % (tname, c))
	return v

def ruler_all():
	mods = {pn.key: ruler(pn) for pn in PANELS}
	tv = ruler_themes()
	v = tv + ['%s: %s' % (k, x) for k, r in mods.items() for x in r['violations']]
	return {'ok': not v, 'violations': v, 'modules': mods, 'themes': tv,
		'rules': ['inside panel and area', 'no overlap, 1.2 mm clear, 2 mm around jacks (Auto and its interval 0.8)',
			'clear of the screws', 'every control labelled', 'every param, input, output of each module exactly once',
			'text >= 10 px', 'text contrast >= 4.5 (dimmed >= 3)',
			'footer group inside the panel, 1.2 from the screws, 0.8 from the panel bottom and the areas above'],
		'footer': {'mark': [round(v, 3) for v in LOGO_BOX],
			'text': [round(v, 3) for v in [l for l in SEQ.labels if l['role'] == 'footer'][0]['box']]}}

# ---------------------------------------------------------------- logo

def trace_logo():
	im = Image.open(LOGO_PNG).convert('L')
	w, h = im.size
	data = im.tobytes()
	fg = bytearray(1 if v > 128 else 0 for v in data)

	def on(x, y):
		return 0 <= x < w and 0 <= y < h and fg[y * w + x]

	nxt = {}
	for y in range(h):
		row = y * w
		for x in range(w):
			if not fg[row + x]:
				continue
			if not on(x, y - 1):
				nxt.setdefault((x, y), []).append((x + 1, y))
			if not on(x + 1, y):
				nxt.setdefault((x + 1, y), []).append((x + 1, y + 1))
			if not on(x, y + 1):
				nxt.setdefault((x + 1, y + 1), []).append((x, y + 1))
			if not on(x - 1, y):
				nxt.setdefault((x, y + 1), []).append((x, y))
	loops = []
	while nxt:
		start = next(iter(nxt))
		pts = [start]
		p = start
		while True:
			lst = nxt[p]
			q = lst.pop()
			if not lst:
				del nxt[p]
			if q == start:
				break
			pts.append(q)
			p = q
			if p not in nxt:
				break
		if len(pts) > 8:
			loops.append(pts)

	def rdp(pts, eps):
		if len(pts) < 3:
			return pts
		a, b = pts[0], pts[-1]
		dx, dy = b[0] - a[0], b[1] - a[1]
		n = math.hypot(dx, dy) or 1e-9
		best, idx = 0, 0
		for i in range(1, len(pts) - 1):
			d = abs(dy * (pts[i][0] - a[0]) - dx * (pts[i][1] - a[1])) / n
			if d > best:
				best, idx = d, i
		if best > eps:
			return rdp(pts[:idx + 1], eps)[:-1] + rdp(pts[idx:], eps)
		return [a, b]

	out = []
	for pts in loops:
		area = 0
		for i in range(len(pts)):
			x0, y0 = pts[i]
			x1, y1 = pts[(i + 1) % len(pts)]
			area += x0 * y1 - x1 * y0
		if abs(area) / 2 < 20:
			continue
		far = max(range(len(pts)), key=lambda i: (pts[i][0] - pts[0][0]) ** 2 + (pts[i][1] - pts[0][1]) ** 2)
		a = rdp(pts[:far + 1], 0.9)
		b = rdp(pts[far:] + [pts[0]], 0.9)
		out.append(a[:-1] + b[:-1])
	xs = [p[0] for l in out for p in l]
	ys = [p[1] for l in out for p in l]
	bx, by = min(xs), min(ys)
	gh = max(ys) - by
	gw = max(xs) - bx
	norm = [[((p[0] - bx) / gh, (p[1] - by) / gh) for p in l] for l in out]
	with open(LOGO_TXT, 'w') as f:
		f.write('# Kansu mark traced from kansu-icon-1024.png; units of the glyph height, evenodd\n')
		f.write('%.5f\n' % (gw / gh))
		for l in norm:
			f.write(' '.join('%.5f,%.5f' % p for p in l) + '\n')
	return gw / gh, norm

def load_logo():
	if os.path.exists(LOGO_PNG):
		return trace_logo()
	lines = [l for l in open(LOGO_TXT).read().splitlines() if l and not l.startswith('#')]
	aspect = float(lines[0])
	loops = [[tuple(float(v) for v in p.split(',')) for p in l.split()] for l in lines[1:]]
	return aspect, loops

LOGO_ASPECT, LOGO_LOOPS = load_logo()

# Footer (both panels): the company mark, then "KANSU AUDIO", one group centred on the panel and on the
# band under the areas; mark centre and capital centre on the band centre.
FOOTER = 'footer'
FOOT_TEXT = 'KANSU AUDIO'
FOOT_MARK_H = 3.4
FOOT_GAP = 1.6	# mark to the first capital
FOOT_YC = (IO_Y1 + H) / 2
LOGO_H = FOOT_MARK_H
LOGO_W = LOGO_ASPECT * LOGO_H
_tb = text_box(FOOT_TEXT, 'title', FOOT_PX, 0.0, 0.0, -1, FOOT_SP)	# ink box of the words drawn at 0, 0
FOOT_W = LOGO_W + FOOT_GAP + _tb[2]
LOGO_X = W / 2 - FOOT_W / 2
LOGO_Y = FOOT_YC - LOGO_H / 2
FOOT_X = LOGO_X + LOGO_W + FOOT_GAP - _tb[0]
FOOT_Y = FOOT_YC - (_tb[1] + _tb[3] / 2)
CUR = SEQ
add_label(FOOT_TEXT, FOOT_X, FOOT_Y, FOOTER, align=-1, size=FOOT_PX, font='title', role='footer', sp=FOOT_SP)
LOGO_BOX = (LOGO_X, LOGO_Y, LOGO_W, LOGO_H)

def foot_dx(pn):
	# The same group, centred on each panel.
	return pn.w / 2 - W / 2

CUR = MOD
add_label(FOOT_TEXT, FOOT_X + foot_dx(MOD), FOOT_Y, FOOTER, align=-1, size=FOOT_PX, font='title', role='footer', sp=FOOT_SP)
CUR = SEQ

def logo_box(pn):
	return (LOGO_X + foot_dx(pn), LOGO_Y, LOGO_W, LOGO_H)

def logo_mm(pn):
	return [[(LOGO_X + foot_dx(pn) + x * LOGO_H, LOGO_Y + y * LOGO_H) for x, y in l] for l in LOGO_LOOPS]

# ---------------------------------------------------------------- SVG

def write_svg(path, th, pn):
	o = []
	o.append('<?xml version="1.0" encoding="UTF-8"?>')
	o.append('<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (pn.w, H, pn.w, H))
	o.append('<rect x="0" y="0" width="%g" height="%g" style="fill:%s"/>' % (pn.w, H, th['panel']))
	for a in pn.areas:
		o.append('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="%g" ry="%g" style="fill:%s"/>' % (
			a[2], a[3], a[4] - a[2], a[5] - a[3], R_AREA, R_AREA, th[a[6]]))
	# The company mark in the footer.
	d = ' '.join('M' + ' L'.join('%.3f %.3f' % p for p in l) + ' Z' for l in logo_mm(pn))
	o.append('<path d="%s" style="fill:%s;fill-rule:evenodd"/>' % (d, th['text']))
	o.append('</svg>')
	with open(path, 'w') as f:
		f.write('\n'.join(o) + '\n')

# ---------------------------------------------------------------- Layout.hpp

def cfloat(v):
	return '%.3ff' % v

def cbox(b):
	return '{%s, %s, %s, %s}' % tuple(cfloat(v) for v in b)

FONT_ENUM = {'label': 'FONT_LABEL', 'screen': 'FONT_SCREEN', 'title': 'FONT_TITLE'}

def write_hpp(path):
	o = ['#pragma once', '', '// Generated by design/layout.py. Do not edit; change layout.py and run it.', '',
		'namespace lay {', '']
	o.append('struct Rgb {\n\tunsigned char r, g, b;\n};\n')
	o.append('struct Theme {\n\tconst char* name;\n' + ''.join('\tRgb %s;\n' % t for t in TOKENS) + '};\n')
	o.append('enum { THEME_KANSU, THEME_ABLETON, THEME_COUNT };\n')
	o.append('static const Theme THEMES[THEME_COUNT] = {')
	for name, th in THEMES:
		o.append('\t{"%s", %s},' % (name, ', '.join('{0x%02X, 0x%02X, 0x%02X}' % rgb(th[t]) for t in TOKENS)))
	o.append('};\n')
	o.append('const float DIM = %s;\t// beyond Length: this far toward the area tone\n' % cfloat(DIM))
	o.append('struct Box {\n\tfloat x, y, w, h;\n};\n')
	o.append('struct Pt {\n\tfloat x, y;\n};\n')
	o.append('enum Font { FONT_LABEL, FONT_SCREEN, FONT_TITLE };')
	o.append('enum Tone { TONE_TEXT, TONE_TEXT_DARK };')
	o.append('// Label and screen fonts load from Rack (asset::system), the title font from the plugin (asset::plugin).')
	o.append('static const char* const FONT_FILES[3] = {"res/fonts/%s", "res/fonts/%s", "res/fonts/%s"};' % (
		FONTS['label'], FONTS['screen'], FONTS['title']))
	o.append('static const bool FONT_IN_PLUGIN[3] = {false, false, true};\n')
	o.append('const float LABEL_PX = %s;' % cfloat(LABEL_PX))
	o.append('const float SCREEN_PX = %s;' % cfloat(SCREEN_PX))
	o.append('const float R_CELL = %s;' % cfloat(R_CELL))
	o.append('const float R_SMALL = %s;' % cfloat(R_SMALL))
	o.append('const float SEG_GAP = %s;' % cfloat(SEG_GAP))
	o.append('const float GLYPH_STROKE = %s;\n' % cfloat(GLYPH_STROKE))
	o.append('struct Label {\n\tconst char* text;\n\tfloat x, y, size;\n\tint font, align, tone;\t// align -1 left, 0 centre, 1 right\n'
		'\tfloat spacing;\t// letter spacing, Rack px\n};\n')
	o.append('static const Label LABELS[] = {')
	for l in labels:
		o.append('\t{"%s", %s, %s, %s, %s, %d, %s, %s},' % (l['text'], cfloat(l['x']), cfloat(l['y']),
			cfloat(l['size']), FONT_ENUM[l['font']], l['align'], 'TONE_TEXT_DARK' if l['tone'] == 'textDark' else 'TONE_TEXT',
			cfloat(l['sp'])))
	o.append('};')
	o.append('const int LABEL_COUNT = %d;\n' % len(labels))
	o.append('const float LOGO_X = %s, LOGO_Y = %s, LOGO_H = %s;\t// footer mark, drawn in the panel SVG\n' % (
		cfloat(LOGO_X), cfloat(LOGO_Y), cfloat(LOGO_H)))
	o.append('// Stage columns: centres, then boxes with x relative to the centre.')
	o.append('static const float STAGE_X[8] = {%s};' % ', '.join(cfloat(v) for v in STAGE_X))
	o.append('const float LIGHT_Y = %s;' % cfloat(LIGHT_Y))
	o.append('const float PITCH_Y = %s;' % cfloat(PITCH_Y))
	o.append('const float GATE_Y = %s;' % cfloat(GATE_Y))
	for n, b in [('ON_BOX', ON_BOX), ('VEL_BOX', VEL_BOX), ('PROB_BOX', PROB_BOX), ('LEN_BOX', LEN_BOX),
			('MODE_BOX', MODE_BOX), ('SLIDE_BOX', SLIDE_BOX), ('LOCK_BOX', LOCK_BOX)]:
		o.append('static const Box %s = %s;' % (n, cbox(b)))
	o.append('const float LEN_CELL_W = %s, LEN_CELL_H = %s, LEN_GAP = %s;\n' % (cfloat(LEN_CELL_W), cfloat(LEN_CELL_H),
		cfloat(LEN_GAP)))
	o.append('// Everything else: boxes, or centres for knobs and jacks.')
	for c in controls:
		if c['name'][-1].isdigit() and c['area'] in ('stages', 'gates') and c['kind'] != 'push':
			continue
		b = c['box']
		if c['kind'] in ('jack', 'darkjack', 'smallknob', 'knob'):
			o.append('static const Pt %s = {%s, %s};' % (c['name'], cfloat(b[0] + b[2] / 2), cfloat(b[1] + b[3] / 2)))
		else:
			o.append('static const Box %s = %s;' % (c['name'], cbox(b)))
	ss = [c for c in controls if c['name'] == 'SCALE_SCREEN'][0]
	o.append('const float SCALE_ROOT_W = %s;' % cfloat(ss['root_w']))
	o.append('\n} // namespace lay')
	with open(path, 'w') as f:
		f.write('\n'.join(o) + '\n')

def write_mod_hpp(path):
	o = ['#pragma once', '', '// Generated by design/layout.py. Do not edit; change layout.py and run it.',
		'// The XL-185 MOD face; types, themes and fonts from Layout.hpp.', '', '#include "Layout.hpp"', '',
		'namespace laym {', '']
	o.append('static const lay::Label LABELS[] = {')
	for l in MOD.labels:
		o.append('\t{"%s", %s, %s, %s, lay::%s, %d, %s, %s},' % (l['text'], cfloat(l['x']), cfloat(l['y']),
			cfloat(l['size']), FONT_ENUM[l['font']], l['align'], 'lay::TONE_TEXT_DARK' if l['tone'] == 'textDark' else 'lay::TONE_TEXT',
			cfloat(l['sp'])))
	o.append('};')
	o.append('const int LABEL_COUNT = %d;\n' % len(MOD.labels))
	o.append('// Stage columns and lane tops; lane boxes below have y relative to the lane top,')
	o.append('// the bar also x relative to its column centre.')
	o.append('static const float COL_X[8] = {%s};' % ', '.join(cfloat(v) for v in MCOL_X))
	o.append('static const float LANE_Y[3] = {%s};' % ', '.join(cfloat(v) for v in LANE_Y))
	for n, b in [('BAR_BOX', MBAR_BOX), ('DIE_BOX', MDIE), ('SHIFT_LEFT_BOX', MSHIFT_L), ('SHIFT_RIGHT_BOX', MSHIFT_R),
			('MIN_BOX', MRANGE['MIN'][2]), ('MAX_BOX', MRANGE['MAX'][2]), ('SMOOTH_BOX', MRANGE['SMOOTH'][2])]:
		o.append('static const lay::Box %s = %s;' % (n, cbox(b)))
	o.append('const float OUT_X = %s, OUT_DY = %s;\t// output jack: centre x, y below the lane top\n' % (cfloat(MOUT_X),
		cfloat(MJACK_DY)))
	o.append('// Tools and In.')
	for c in MOD.controls:
		if c['area'] not in ('tools', 'in'):
			continue
		b = c['box']
		if c['kind'] == 'jack':
			o.append('static const lay::Pt %s = {%s, %s};' % (c['name'], cfloat(b[0] + b[2] / 2), cfloat(b[1] + b[3] / 2)))
		else:
			o.append('static const lay::Box %s = %s;' % (c['name'], cbox(b)))
	o.append('\n} // namespace laym')
	with open(path, 'w') as f:
		f.write('\n'.join(o) + '\n')

# ---------------------------------------------------------------- preview

MODES = ['Rest', 'Once', 'Hold', '1/4', '1/3', '1/2', 'All']
DIRS = ['forward', 'backward', 'pingpong', 'random']
NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
EXAMPLE = {
	'length': 6, 'playing': 2, 'direction': 0, 'transpose': 3, 'restart': 0, 'every': 1, 'auto': 1, 'run': 1,
	'root': 2, 'scale': 'Dorian',
	'on': [1, 1, 1, 1, 0, 1, 1, 1],
	'pitch': [0, 7, 12, -5, 3, 10, -12, 5],
	'vel': [120, 96, 127, 64, 100, 80, 110, 90],
	'prob': [100, 100, 75, 50, 100, 90, 100, 60],
	'len': [1, 2, 4, 3, 1, 8, 2, 6],
	'mode': [1, 2, 6, 5, 0, 3, 1, 4],
	'slide': [0, 0, 0, 1, 0, 0, 0, 0],
	'lock': [0, 1, 0, 0, 0, 0, 0, 0],
}

def fires(mode, step):
	# Step 0 based: does a stage in this mode fire on this step?
	if mode == 0:
		return False
	if mode in (1, 2):
		return step == 0
	div = {3: 4, 4: 3, 5: 2, 6: 1}[mode]
	return step % div == 0

# Expander example: the sequencer's Length and playing stage, Auto off to show the dim interval.
MOD_EXAMPLE = {
	'value': [[20, 45, 80, 60, 35, 100, 10, 70], [100, 75, 50, 25, 0, 50, 90, 40], [0, 0, 60, 60, 15, 30, 85, 55]],
	'MIN': [0, 20, 100], 'MAX': [100, 80, 0], 'SMOOTH': [0, 250, 40], 'every': 2, 'auto': 0,
}

def render_preview(path, th, pn):
	S = PREVIEW_MM
	T = {k: rgb(v) for k, v in th.items()}
	img = Image.new('RGB', (int(round(pn.w * S)), int(round(H * S))), T['panel'])
	d = ImageDraw.Draw(img)

	def R(b):
		return [b[0] * S, b[1] * S, (b[0] + b[2]) * S, (b[1] + b[3]) * S]

	def rrect(b, col, r=R_CELL):
		d.rounded_rectangle(R(b), radius=r * S, fill=col)

	def text(s, x, y, col, size, font='label', align=0, sp=0.0):
		f, em = pil_font(font, size, S)
		base = (y + baseline_offset(font, size)) * S
		if not sp:
			anchor = {-1: 'ls', 0: 'ms', 1: 'rs'}[align]
			d.text((x * S, base), s, fill=col, font=f, anchor=anchor)
			return
		# Letter spacing: glyph by glyph, the spacing between glyphs as nanovg adds it.
		k = em / round(em)
		wid = text_width(s, font, size, sp)
		px = (x - (wid if align == 1 else wid / 2 if align == 0 else 0)) * S
		for ch in s:
			d.text((px, base), ch, fill=col, font=f, anchor='ls')
			px += f.getlength(ch) * k + sp / PX_MM * S

	def dimc(c, on):
		return mix(c, T['area'], DIM) if on else c

	for a in pn.areas:
		d.rounded_rectangle([a[2] * S, a[3] * S, a[4] * S, a[5] * S], radius=R_AREA * S, fill=T[a[6]])
	# Logo, even-odd.
	mask = Image.new('1', img.size, 0)
	for l in logo_mm(pn):
		m = Image.new('1', img.size, 0)
		ImageDraw.Draw(m).polygon([(x * S, y * S) for x, y in l], fill=1)
		mask = ImageChops.logical_xor(mask, m)
	img.paste(T['text'], mask=mask)
	for sx, sy in pn.screws:
		d.ellipse([(sx - SCREW_D / 2) * S, (sy - SCREW_D / 2) * S, (sx + SCREW_D / 2) * S, (sy + SCREW_D / 2) * S],
			fill=(196, 196, 196), outline=(150, 150, 150))
		d.line([(sx - 1.5) * S, sy * S, (sx + 1.5) * S, sy * S], fill=(110, 110, 110), width=2)

	E = dict(EXAMPLE)
	if pn is MOD:
		E.update(MOD_EXAMPLE)
	for l in pn.labels:
		text(l['text'], l['x'], l['y'], T[l['tone']], l['size'], l['font'], l['align'], l['sp'])

	def knob(cx, cy, dia, frac):
		r = dia / 2
		d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=(0x1E, 0x1D, 0x1E))
		ang = -0.83 * math.pi + frac * 1.66 * math.pi
		x0, y0 = cx + math.sin(ang) * r * 0.35, cy - math.cos(ang) * r * 0.35
		x1, y1 = cx + math.sin(ang) * r * 0.85, cy - math.cos(ang) * r * 0.85
		d.line([x0 * S, y0 * S, x1 * S, y1 * S], fill=(0xFF, 0xFF, 0xFF), width=max(2, int(S * 0.5)))

	def jack(cx, cy, dark):
		for rr, col in [(11.1, (0xE0, 0xE0, 0xE0)), (8.17, (0x1F, 0x1F, 0x1F)),
				(6.8, (0x33, 0x33, 0x33) if dark else (0xE0, 0xE0, 0xE0)), (4.87, (0, 0, 0))]:
			r = rr / PX_MM
			d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], fill=col)

	def glyph(kind, b, col):
		cx, cy = b[0] + b[2] / 2, b[1] + b[3] / 2
		lw = int(round(GLYPH_STROKE * S))
		if kind == 'die':
			s = 1.0
			for px, py in [(-s, -s), (0, 0), (s, s)]:
				r = 0.42
				d.ellipse([(cx + px - r) * S, (cy + py - r) * S, (cx + px + r) * S, (cy + py + r) * S], fill=col)
		elif kind in ('left', 'right'):
			k = -1 if kind == 'left' else 1
			d.line([(cx - 1.4 * k) * S, cy * S, (cx + 1.3 * k) * S, cy * S], fill=col, width=lw)
			d.polygon([((cx + 1.6 * k) * S, cy * S), ((cx + 0.5 * k) * S, (cy - 1.0) * S),
				((cx + 0.5 * k) * S, (cy + 1.0) * S)], fill=col)
		elif kind == 'play':
			d.polygon([((cx - 0.9) * S, (cy - 1.2) * S), ((cx + 1.2) * S, cy * S), ((cx - 0.9) * S, (cy + 1.2) * S)], fill=col)

	def dir_glyph(k, cx, cy, col):
		lw = int(round(GLYPH_STROKE * S))

		def head(x, y, k2):
			d.polygon([((x) * S, y * S), ((x - 1.0 * k2) * S, (y - 0.85) * S), ((x - 1.0 * k2) * S, (y + 0.85) * S)],
				fill=col)
		if k == 0:
			d.line([(cx - 2.2) * S, cy * S, (cx + 1.0) * S, cy * S], fill=col, width=lw)
			head(cx + 2.2, cy, 1)
		elif k == 1:
			d.line([(cx - 1.0) * S, cy * S, (cx + 2.2) * S, cy * S], fill=col, width=lw)
			head(cx - 2.2, cy, -1)
		elif k == 2:
			d.line([(cx - 1.6) * S, cy * S, (cx + 1.6) * S, cy * S], fill=col, width=lw)
			head(cx + 2.6, cy, 1)
			head(cx - 2.6, cy, -1)
		else:
			d.line([(cx - 2.3) * S, (cy + 1.2) * S, (cx - 0.7) * S, (cy - 1.0) * S, (cx + 0.7) * S, (cy + 1.0) * S,
				(cx + 1.4) * S, (cy + 0.0) * S], fill=col, width=lw, joint='curve')
			d.polygon([((cx + 2.3) * S, (cy - 1.3) * S), ((cx + 0.9) * S, (cy - 0.7) * S), ((cx + 2.0) * S, (cy + 0.4) * S)],
				fill=col)

	for c in pn.controls:
		b = c['box']
		k = c['kind']
		name = c['name']
		i = int(name[-1]) if name[-1].isdigit() else -1
		dimmed = i >= 0 and i >= E['length'] and c['area'] == 'stages'
		if k == 'light':
			on = i == E['playing']
			col = T['accentLight'] if on else T['off']
			d.ellipse(R(b), fill=col)
		elif k == 'stagecell':
			on = E['on'][i]
			bg = T['accentLight'] if on else T['off']
			fg = T['onAccent'] if on else T['text']
			rrect(b, dimc(bg, dimmed))
			text(str(i + 1), b[0] + b[2] / 2, b[1] + b[3] / 2, dimc(fg, dimmed), LABEL_PX)
		elif k == 'knob':
			knob(b[0] + b[2] / 2, b[1] + b[3] / 2, b[2], (E['pitch'][i] + 36) / 72.0)
		elif k == 'smallknob':
			frac = {'SWING': 0.2, 'GATE': 0.75, 'GLIDE': 0.12}[name]
			knob(b[0] + b[2] / 2, b[1] + b[3] / 2, b[2], frac)
		elif k == 'bar':
			frac = E['vel'][i] / 127.0 if name.startswith('VEL') else E['prob'][i] / 100.0
			rrect(b, dimc(T['off'], dimmed), R_SMALL)
			fh = b[3] * frac
			rrect((b[0], b[1] + b[3] - fh, b[2], fh), dimc(T['accentLight'], dimmed), R_SMALL)
		elif k == 'lanebar':
			# Expander value: the playing stage's track in the mid tone, dim beyond Length.
			n = c['stage']
			dm = n >= E['length']
			frac = E['value'][c['lane']][n] / 100.0
			rrect(b, dimc(T['mid'] if n == E['playing'] else T['off'], dm), R_SMALL)
			fh = b[3] * frac
			if fh > 0:
				rrect((b[0], b[1] + b[3] - fh, b[2], fh), dimc(T['accentLight'], dm), R_SMALL)
		elif k == 'lencells':
			for n in range(8):
				cx = b[0] + (n % 4) * (LEN_CELL_W + LEN_GAP)
				cy = b[1] + (n // 4) * (LEN_CELL_H + LEN_GAP)
				if n < E['len'][i]:
					col = T['accentLight'] if fires(E['mode'][i], n) else T['mid']
				else:
					col = T['off']
				rrect((cx, cy, LEN_CELL_W, LEN_CELL_H), dimc(col, dimmed), R_SMALL)
		elif k == 'mode':
			rrect(b, dimc(T['screen'], dimmed))
			text(MODES[E['mode'][i]], b[0] + b[2] / 2, b[1] + b[3] / 2, dimc(T['screenText'], dimmed), SCREEN_PX, 'screen')
		elif k == 'cell':
			if name.startswith('SLIDE'):
				on = E['slide'][i]
			elif name.startswith('LOCK'):
				on = E['lock'][i]
			else:
				on = E[name.lower()]
			bg = T['accentLight'] if on else T['off']
			fg = T['onAccent'] if on else T['text']
			rrect(b, bg)
			if c.get('glyph'):
				glyph(c['glyph'], b, fg)
			else:
				text(c['text'], b[0] + b[2] / 2, b[1] + b[3] / 2, fg, LABEL_PX)
		elif k == 'push':
			pressed = name == 'MUTATE'	# one shown pressed
			bg = T['accentLight'] if pressed else T['off']
			fg = T['onAccent'] if pressed else T['text']
			rrect(b, bg)
			if c.get('glyph'):
				glyph(c['glyph'], b, fg)
			else:
				text(c['text'], b[0] + b[2] / 2, b[1] + b[3] / 2, fg, LABEL_PX)
		elif k == 'number':
			rrect(b, T['screen'])
			if c.get('what'):
				s = str(E[c['what']][c['lane']])
			else:
				s = {'LENGTH': str(E['length']), 'TRANSPOSE': '%+d' % E['transpose'] if E['transpose'] else '0',
					'RESTART': ['Off', '16', '32', '64', '128'][E['restart']],
					'EVERY': ['4 bars', '8 bars', '16 bars'][E['every']]}[name]
			col = T['screenText'] if (name != 'EVERY' or E['auto']) else mix(T['screenText'], T['area'], DIM)
			if name == 'EVERY' and not E['auto']:
				rrect(b, mix(T['screen'], T['area'], DIM))
			text(s, b[0] + b[2] / 2, b[1] + b[3] / 2, col, SCREEN_PX, 'screen')
		elif k == 'scalescreen':
			rrect(b, T['screen'])
			s = NOTES[E['root']] + ' ' + E['scale'] if E['scale'] else 'Off'
			text(s, b[0] + b[2] / 2, b[1] + b[3] / 2, T['screenText'], SCREEN_PX, 'screen')
		elif k == 'direction':
			sg = SEG_GAP
			segw = (b[2] - 3 * sg) / 4
			for n in range(4):
				sbx = (b[0] + n * (segw + sg), b[1], segw, b[3])
				on = n == E['direction']
				rrect(sbx, T['accentLight'] if on else T['off'])
				dir_glyph(n, sbx[0] + segw / 2, b[1] + b[3] / 2, T['onAccent'] if on else T['text'])
		elif k in ('jack', 'darkjack'):
			jack(b[0] + b[2] / 2, b[1] + b[3] / 2, k == 'darkjack')
	if path:
		img.save(path)
	return img

# ---------------------------------------------------------------- main

def main():
	result = ruler_all()
	with open(os.path.join(HERE, 'RULER.json'), 'w') as f:
		json.dump(result, f, indent=1)
	for name, th in THEMES:
		t = name.lower()
		write_svg(os.path.join(ROOT, 'res', 'XL185-%s.svg' % t), th, SEQ)
		write_svg(os.path.join(ROOT, 'res', 'XL185Mod-%s.svg' % t), th, MOD)
		a = render_preview(os.path.join(HERE, 'preview-%s.png' % t), th, SEQ)
		b = render_preview(os.path.join(HERE, 'preview-mod-%s.png' % t), th, MOD)
		# The pair as it sits in the rack: the expander directly right of the sequencer.
		pair = Image.new('RGB', (a.width + b.width, a.height))
		pair.paste(a, (0, 0))
		pair.paste(b, (a.width, 0))
		pair.save(os.path.join(HERE, 'preview-pair-%s.png' % t))
	write_hpp(os.path.join(ROOT, 'src', 'Layout.hpp'))
	write_mod_hpp(os.path.join(ROOT, 'src', 'LayoutMod.hpp'))
	print('ruler ok' if result['ok'] else 'ruler: %d violations' % len(result['violations']))
	for v in result['violations']:
		print('  ' + v)

if __name__ == '__main__':
	main()
