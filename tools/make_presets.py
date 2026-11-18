#!/usr/bin/env python3
"""Writes the factory presets in presets/XL185 and presets/XL185Mod.

Param ids are read from the ParamId enums in src/XL185.cpp and src/XL185Mod.cpp, and every value is
checked against the range of its configParam / configSwitch / configButton call, so a preset can
never point at the wrong control. The files have the layout Rack 2 writes for a module preset:
plugin, model, version, params (value, id), data.

The "panel" key of the module data is left out on purpose: the module only changes its panel theme
when the key is present, so loading a preset keeps the player's theme.

Run from the repository root:  python3 tools/make_presets.py
"""

import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def read(path):
    with open(os.path.join(ROOT, path)) as f:
        return f.read()


PLUGIN = json.loads(read('plugin.json'))
CONSTANTS = {
    'xl::SCALE_COUNT': int(re.search(r'SCALE_COUNT\s*=\s*(\d+)', read('core/Scales.hpp')).group(1)),
}

# The scale table, for the check that every pitch lies in the preset's scale.
SCALE_NAMES = re.findall(r'\{"([^"]+)",\s*\d+,', read('core/Scales.cpp'))
SCALE_CENTS = [
    [float(c) for c in m.split(',')]
    for m in re.findall(r'\{"[^"]+",\s*\d+,\s*\{([^}]*)\}\}', read('core/Scales.cpp'))
]
NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


# ---- reading the module source

def param_ids(src):
    """Ids of the ParamId enum: name -> (first id, count)."""
    body = re.search(r'enum ParamId \{(.*?)\};', src, re.S).group(1)
    ids = {}
    n = 0
    for line in body.split('\n'):
        line = line.split('//')[0].strip().rstrip(',')
        if not line:
            continue
        m = re.match(r'ENUMS\((\w+),\s*(\d+)\)$', line)
        if m:
            ids[m.group(1)] = (n, int(m.group(2)))
            n += int(m.group(2))
        elif line == 'PARAMS_LEN':
            ids['PARAMS_LEN'] = (n, 0)
        else:
            assert re.match(r'\w+$', line), line
            ids[line] = (n, 1)
            n += 1
    return ids


def number(expr):
    expr = expr.strip().replace('(float)', '').strip()
    if expr in CONSTANTS:
        return float(CONSTANTS[expr])
    return float(expr.rstrip('f'))


def param_ranges(src):
    """name -> (min, max, whole): from the config calls of the constructor."""
    ranges = {}
    for m in re.finditer(r'config(Param|Switch)\((\w+)[^,]*,\s*([^,]+),\s*([^,]+),\s*[^,]+,(.*)', src):
        kind, name, lo, hi, rest = m.groups()
        whole = kind == 'Switch' or 'snapEnabled = true' in rest
        ranges[name] = (number(lo), number(hi), whole)
    for m in re.finditer(r'configButton\((\w+)', src):
        ranges[m.group(1)] = (0.0, 1.0, True)
    return ranges


def data_keys(src):
    """Keys dataToJson writes, and the clamp of each in dataFromJson where it has one."""
    to = re.search(r'json_t\* dataToJson\(\) override \{(.*?)\n\t\}', src, re.S).group(1)
    keys = re.findall(r'json_object_set_new\(rootJ, "(\w+)"', to)
    limits = {}
    for m in re.finditer(r'readInt\(rootJ, "(\w+)", [\w.]+, ([\w:]+), ([\w:\s-]+)\)', src):
        hi = m.group(3).strip()
        limits[m.group(1)] = (m.group(2), hi)
    return keys, limits


class Module:
    def __init__(self, slug, path):
        self.slug = slug
        src = read(path)
        self.ids = param_ids(src)
        self.count = self.ids['PARAMS_LEN'][0]
        self.ranges = param_ranges(src)
        self.data_keys, self.data_limits = data_keys(src)
        # Every param id must be covered by a config call.
        for name, (first, n) in self.ids.items():
            if name != 'PARAMS_LEN':
                assert name in self.ranges, '%s: no config call for %s' % (slug, name)

    def id(self, name, index=0):
        first, n = self.ids[name]
        assert 0 <= index < n, (name, index)
        return first + index

    def name_of(self, pid):
        for name, (first, n) in self.ids.items():
            if first <= pid < first + n:
                return name
        raise KeyError(pid)

    def defaults(self, src_defaults):
        return [src_defaults.get(self.name_of(i), 0.0) for i in range(self.count)]


def config_defaults(path):
    """Default values of the config calls: name -> default."""
    src = read(path)
    out = {}
    for m in re.finditer(r'config(?:Param|Switch)\((\w+)[^,]*,\s*[^,]+,\s*[^,]+,\s*([^,]+),', src):
        out[m.group(1)] = number(m.group(2))
    return out


SEQ = Module('XL185', 'src/XL185.cpp')
MOD = Module('XL185Mod', 'src/XL185Mod.cpp')
SEQ_DEFAULTS = config_defaults('src/XL185.cpp')
MOD_DEFAULTS = config_defaults('src/XL185Mod.cpp')

MODES = {'rest': 0, 'once': 1, 'hold': 2, '1/4': 3, '1/3': 4, '1/2': 5, 'all': 6}
DIRECTIONS = {'forward': 0, 'backward': 1, 'back and forth': 2, 'random': 3}
RESTART = {'off': 0, 16: 1, 32: 2, 64: 3, 128: 4}
EVERY = {4: 0, 8: 1, 16: 2}


def scale_index(name):
    return 0 if name == 'Off' else SCALE_NAMES.index(name) + 1


# ---- the presets

def stage(pitch, mode='once', length=1, vel=120, prob=100, slide=False, on=True, lock=False):
    return dict(pitch=pitch, mode=mode, len=length, vel=vel, prob=prob, slide=slide, on=on, lock=lock)


def sequencer(name, root, scale, stages, length=8, direction='forward', swing=0, gate=100,
              transpose=0, glide=60, restart='off', auto=False, every=4, data=None):
    v = SEQ.defaults(SEQ_DEFAULTS)
    for i, s in enumerate(stages):
        v[SEQ.id('ON_PARAMS', i)] = 1 if s['on'] else 0
        v[SEQ.id('PITCH_PARAMS', i)] = s['pitch']
        v[SEQ.id('VEL_PARAMS', i)] = s['vel']
        v[SEQ.id('MODE_PARAMS', i)] = MODES[s['mode']]
        v[SEQ.id('LEN_PARAMS', i)] = s['len']
        v[SEQ.id('PROB_PARAMS', i)] = s['prob']
        v[SEQ.id('SLIDE_PARAMS', i)] = 1 if s['slide'] else 0
        v[SEQ.id('LOCK_PARAMS', i)] = 1 if s['lock'] else 0
    v[SEQ.id('LENGTH_PARAM')] = length
    v[SEQ.id('DIRECTION_PARAM')] = DIRECTIONS[direction]
    v[SEQ.id('SWING_PARAM')] = swing
    v[SEQ.id('GATE_PARAM')] = gate
    v[SEQ.id('TRANSPOSE_PARAM')] = transpose
    v[SEQ.id('GLIDE_PARAM')] = glide
    v[SEQ.id('ROOT_PARAM')] = NOTES.index(root)
    v[SEQ.id('SCALE_PARAM')] = scale_index(scale)
    v[SEQ.id('RESTART_PARAM')] = RESTART[restart]
    v[SEQ.id('AUTO_PARAM')] = 1 if auto else 0
    v[SEQ.id('EVERY_PARAM')] = EVERY[every]
    d = {'stageGateMode': 0, 'toolParts': 3, 'pitchLow': 36, 'pitchHigh': 60,
         'velocityLow': 1, 'velocityHigh': 127, 'stepsPerBar': 16}
    d.update(data or {})
    return name, v, d


S = stage
XL_PRESETS = [
    # A minor, 16 steps on 7 stages: rolling 16ths around a held D.
    sequencer('01 Rolling Bass', 'A', 'Minor', [
        S(-15, 'all', 4, vel=118), S(-15, '1/2', 2, vel=96), S(-3, 'once', 1, vel=127),
        S(-15, 'all', 3, vel=104), S(-10, 'hold', 3, vel=112), S(-15, 'all', 2, vel=96),
        S(-8, 'once', 1, vel=124), S(-12, 'all', 2, vel=100),
    ], length=7, gate=55),
    # G minor, 12 steps: slides into ties and glides, three against the bar.
    sequencer('02 Acid Line', 'G', 'Minor', [
        S(-17, 'once', 1, vel=120), S(-5, 'once', 1, vel=90, slide=True), S(-14, 'all', 2, vel=104),
        S(-17, 'once', 1, vel=112, slide=True), S(-12, '1/2', 2, vel=110), S(-9, 'once', 1, vel=127, slide=True),
        S(-10, 'once', 1, vel=96), S(-5, 'all', 3, vel=100, prob=75),
    ], gate=50, glide=80),
    # D Dorian, 15 steps: stabs on the off-beats that slip a 16th each bar.
    sequencer('03 Offbeat Stabs', 'D', 'Dorian', [
        S(2, 'rest', 2), S(2, 'once', 2, vel=120), S(5, 'rest', 2), S(-3, 'once', 2, vel=110),
        S(0, 'rest', 2), S(5, 'once', 1, vel=100), S(-5, 'once', 1, vel=84, prob=50), S(2, 'rest', 3),
    ], gate=22, swing=20),
    # E Phrygian, two groups of five steps against the beat, back in line every 16 steps.
    sequencer('04 Five Against Four', 'E', 'Phrygian', [
        S(-8, 'all', 2, vel=120), S(-5, 'once', 1, vel=92), S(-1, '1/2', 2, vel=104),
        S(-7, 'once', 1, vel=110), S(4, 'once', 2, vel=96), S(2, 'all', 2, vel=88),
        S(-8, 'once', 1), S(-8, 'once', 1),
    ], length=6, gate=70, restart=16),
    # C minor, 25 steps of long stages, held notes and chance.
    sequencer('05 Sparse Dub', 'C', 'Minor', [
        S(-12, 'once', 4, vel=112), S(-12, 'rest', 3), S(-5, 'once', 3, vel=96, prob=50),
        S(-9, 'hold', 6, vel=104, prob=70), S(0, 'rest', 2), S(-2, 'once', 5, vel=88, prob=35),
        S(0, 'once', 2, vel=100, prob=60), S(0, 'rest', 1),
    ], length=7, gate=40, swing=30),
    # C Dorian, a rising and falling arpeggio with a doubled step near the top.
    sequencer('06 Back and Forth Arp', 'C', 'Dorian', [
        S(-12, 'once', 1, vel=120), S(-5, 'once', 1, vel=96), S(-2, 'once', 1, vel=104),
        S(2, 'once', 1, vel=110), S(3, 'once', 1, vel=92), S(7, 'all', 2, vel=100),
        S(10, 'once', 1, vel=108), S(14, 'once', 1, vel=124),
    ], direction='back and forth', gate=60),
    # One bar of drums for the stage gate jacks, pitch flat, eight stages of two 16ths each.
    # Kick on 1, 3, 5, 7 (four to the floor), snare stacked on 3 and 7 (beats 2 and 4),
    # closed hat on the off-beats of 2 and 6, a two-16th hat on 4, open hat on 8.
    # The stage gate jacks copy the note gates (stageGateMode 0), so the repeat of stage 4 reaches
    # its jack; Gate 10 % keeps every gate a short trigger.
    sequencer('07 Percussion Triggers', 'C', 'Minor', [
        S(0, 'once', 2, vel=127), S(0, 'once', 2, vel=96), S(0, 'once', 2, vel=120),
        S(0, 'all', 2, vel=88), S(0, 'once', 2, vel=124), S(0, 'once', 2, vel=96),
        S(0, 'once', 2, vel=120), S(0, 'once', 2, vel=104),
    ], gate=10, data={'stageGateMode': 0}),
    # D Phrygian, random stage order; stages 1 and 5 locked against the random tools.
    sequencer('08 Random Locked Pair', 'D', 'Phrygian', [
        S(-10, 'once', 2, vel=124, lock=True), S(-7, 'all', 1, vel=96), S(-3, '1/2', 2, vel=104),
        S(0, 'once', 1, vel=110), S(-10, 'hold', 3, vel=116, lock=True), S(3, 'once', 1, vel=92),
        S(-5, 'all', 2, vel=100, slide=True), S(-2, 'once', 1, vel=108),
    ], direction='random', gate=60),
]


def lanes(name, lane_list):
    """lane_list: three (values, min, max, smooth)."""
    v = MOD.defaults(MOD_DEFAULTS)
    for l, (values, lo, hi, smooth) in enumerate(lane_list):
        assert len(values) == 8
        for i, x in enumerate(values):
            v[MOD.id('VALUE_PARAMS', l * 8 + i)] = x
        v[MOD.id('MIN_PARAMS', l)] = lo
        v[MOD.id('MAX_PARAMS', l)] = hi
        v[MOD.id('SMOOTH_PARAMS', l)] = smooth
    return name, v, {}


MOD_PRESETS = [
    # A rises over the loop, B falls; both glide slowly between stages. C holds the middle.
    lanes('01 Slow Ramp', [
        ([0, 14, 29, 43, 57, 71, 86, 100], 0, 100, 1500),
        ([100, 86, 71, 57, 43, 29, 14, 0], 0, 100, 1500),
        ([50] * 8, 0, 100, 0),
    ]),
    # A on and off per stage, B the same values turned over (Min above Max), C a softer accent.
    lanes('02 Alternating Accents', [
        ([100, 0] * 4, 0, 100, 0),
        ([100, 0] * 4, 100, 0, 0),
        ([100, 30, 60, 30] * 2, 0, 80, 0),
    ]),
    # Fixed random looking steps, each lane with its own glide time.
    lanes('03 Random Steps Smooth', [
        ([62, 13, 88, 41, 7, 73, 29, 95], 0, 100, 120),
        ([35, 91, 18, 66, 52, 4, 79, 47], 0, 100, 250),
        ([84, 26, 58, 11, 97, 39, 70, 21], 0, 100, 400),
    ]),
    # Three lanes over three ranges: 0 to 2 V, 3 to 7 V and 6 to 10 V.
    lanes('04 Three Lanes Three Ranges', [
        ([0, 50, 100, 25, 75, 0, 100, 50], 0, 20, 0),
        ([100, 0, 50, 100, 0, 50, 25, 75], 30, 70, 50),
        ([20, 40, 60, 80, 100, 80, 60, 40], 60, 100, 0),
    ]),
]


# ---- checks and output

def check(module, name, values, data):
    errors = []
    if len(values) != module.count:
        errors.append('%d params, the module has %d' % (len(values), module.count))
    for pid, x in enumerate(values):
        pname = module.name_of(pid)
        lo, hi, whole = module.ranges[pname]
        if not (lo <= x <= hi):
            errors.append('%s (id %d) = %s outside %s..%s' % (pname, pid, x, lo, hi))
        if whole and float(x) != int(x):
            errors.append('%s (id %d) = %s is not whole' % (pname, pid, x))
    for k in data:
        if k not in module.data_keys:
            errors.append('data key %s is not written by dataToJson' % k)
        if k == 'panel':
            errors.append('data key panel would force the theme')
    missing = [k for k in module.data_keys if k not in data and k != 'panel']
    if missing:
        errors.append('data keys missing: %s' % ', '.join(missing))
    for k, x in data.items():
        if k in module.data_limits:
            lo, hi = module.data_limits[k]
            lo = int(lo) if lo.lstrip('-').isdigit() else None
            hi = int(hi) if hi.lstrip('-').isdigit() else None
            if (lo is not None and x < lo) or (hi is not None and x > hi):
                errors.append('data %s = %s outside %s..%s' % (k, x, lo, hi))
    if module is SEQ:
        root = int(values[SEQ.id('ROOT_PARAM')])
        sc = int(values[SEQ.id('SCALE_PARAM')])
        transpose = int(values[SEQ.id('TRANSPOSE_PARAM')])
        if sc == 0:
            errors.append('no scale set')
        else:
            cents = SCALE_CENTS[sc - 1]
            for i in range(8):
                p = int(values[SEQ.id('PITCH_PARAMS', i)])
                rel = ((60 + p + transpose - root) % 12) * 100.0
                if not any(abs(rel - c) < 1e-6 for c in cents):
                    errors.append('stage %d pitch %d is not in %s %s' % (i + 1, p, NOTES[root], SCALE_NAMES[sc - 1]))
    if errors:
        sys.exit('%s / %s:\n  %s' % (module.slug, name, '\n  '.join(errors)))


def as_json(module, values, data):
    params = []
    for pid, x in enumerate(values):
        params.append({'value': float(x), 'id': pid})
    return {
        'plugin': PLUGIN['slug'],
        'model': module.slug,
        'version': PLUGIN['version'],
        'params': params,
        'data': data,
    }


def write(module, presets):
    folder = os.path.join(ROOT, 'presets', module.slug)
    os.makedirs(folder, exist_ok=True)
    for f in os.listdir(folder):
        if f.endswith('.vcvm'):
            os.remove(os.path.join(folder, f))
    names = set()
    for name, values, data in presets:
        assert name not in names, name
        names.add(name)
        check(module, name, values, data)
        path = os.path.join(folder, name + '.vcvm')
        with open(path, 'w') as f:
            json.dump(as_json(module, values, data), f, indent=2)
            f.write('\n')
        # Read back: valid JSON, ids 0..N-1 in order, model and plugin right.
        with open(path) as f:
            j = json.load(f)
        assert j['plugin'] == PLUGIN['slug'] and j['model'] == module.slug
        assert [p['id'] for p in j['params']] == list(range(module.count))
        print('%s/%s.vcvm: %d params, data %s' % (module.slug, name, len(j['params']), sorted(j['data'])))


if __name__ == '__main__':
    assert [m['slug'] for m in PLUGIN['modules']] == [SEQ.slug, MOD.slug]
    write(SEQ, XL_PRESETS)
    write(MOD, MOD_PRESETS)
