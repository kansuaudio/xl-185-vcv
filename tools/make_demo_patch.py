#!/usr/bin/env python3
"""Writes patches/XL-185 presets.vcv: every factory preset side by side, one row each, with a voice
per row built from the stock Fundamental and Core modules, one shared clock, a reset button and two
mixers as row faders.

Stock module ports and params are addressed by name through tools/stock_modules.json (ids, names and
ranges read from the modules themselves, panel widths from their SVGs). An unknown name, a value
outside its range, two modules on top of each other or a second cable into one input stop the script.

  python3 tools/make_demo_patch.py               writes patches/XL-185 presets.vcv
  python3 tools/make_demo_patch.py --no-audio X  writes X without the audio module (for headless checks)
"""
import glob, io, json, math, os, re, subprocess, sys, tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NO_AUDIO = '--no-audio' in sys.argv
TABLE = json.load(open(os.path.join(ROOT, 'tools', 'stock_modules.json')))
PLUGIN = json.load(open(os.path.join(ROOT, 'plugin.json')))


def fail(msg):
    raise SystemExit('make_demo_patch: ' + msg)


# ---- the Kansu modules: ports from the enums in src, widths from the panels in res

def enum_ports(src):
    text = re.sub(r'//.*', '', open(os.path.join(ROOT, 'src', src)).read())
    out = {}
    for kind in ('InputId', 'OutputId'):
        body = re.search(r'enum\s+' + kind + r'\s*\{(.*?)\}', text, re.S).group(1)
        names, n = {}, 0
        for a, cnt, b in re.findall(r'ENUMS\((\w+),\s*(\d+)\)|(\w+)', body):
            if a:
                for k in range(int(cnt)):
                    names['%s %d' % (a, k + 1)] = n + k
                n += int(cnt)
            elif not b.endswith('_LEN'):
                names[b] = n
                n += 1
        out[kind] = names
    return out


def svg_hp(path):
    m = re.search(r'<svg[^>]*?\swidth="([\d.]+)(mm|px)?"', open(path).read(), re.S)
    w, unit = float(m.group(1)), m.group(2) or 'px'
    return w / 5.08 if unit == 'mm' else w / 15.0


KANSU = {}
for slug, src in (('XL185', 'XL185.cpp'), ('XL185Mod', 'XL185Mod.cpp')):
    p = enum_ports(src)
    KANSU[slug] = {'hp': svg_hp(os.path.join(ROOT, 'res', slug + '-kansu.svg')),
                   'inputs': p['InputId'], 'outputs': p['OutputId']}


# ---- patch building

modules, cables = [], []
_id = [100]
used_inputs = set()
placed = {}  # row -> [(x0, x1, model)]
OBJ = {}


class M:
    def __init__(self, plugin, model, x, row, params=None, data=None):
        self.plugin, self.model, self.x, self.row = plugin, model, x, row
        if plugin == 'KansuAudio':
            spec = KANSU[model]
            self.ins, self.outs, version = spec['inputs'], spec['outputs'], PLUGIN['version']
            values = params or {}  # whole preset by id, checked by make_presets.py
        else:
            if model not in TABLE[plugin]['models']:
                fail('no module %s %s in the table' % (plugin, model))
            spec = TABLE[plugin]['models'][model]
            self.ins = {p['name']: p['id'] for p in spec['inputs'] if p['name']}
            self.outs = {p['name']: p['id'] for p in spec['outputs'] if p['name']}
            version = TABLE[plugin]['version']
            byname = {p['name']: p for p in spec['params'] if p['name']}
            values = {}
            for name, v in (params or {}).items():
                if name not in byname:
                    fail('%s has no param "%s" (has: %s)' % (model, name, ', '.join(byname)))
                q = byname[name]
                if not (q['min'] - 1e-6 <= v <= q['max'] + 1e-6):
                    fail('%s "%s" = %s outside %s..%s' % (model, name, v, q['min'], q['max']))
                if q.get('snap') and float(v) != round(v):
                    fail('%s "%s" = %s is not whole' % (model, name, v))
                values[q['id']] = float(v)
        self.hp = spec['hp']
        for x0, x1, other in placed.get(row, []):
            if x < x1 - 1e-6 and x0 < x + self.hp - 1e-6:
                fail('row %d: %s at %s overlaps %s at %s..%s' % (row, model, x, other, x0, x1))
        placed.setdefault(row, []).append((x, x + self.hp, model))
        _id[0] += 1
        self.id = _id[0]
        m = {'id': self.id, 'plugin': plugin, 'model': model, 'version': version,
             'params': [{'id': i, 'value': v} for i, v in sorted(values.items())], 'pos': [x, row]}
        if data is not None:
            m['data'] = data
        modules.append(m)
        OBJ[self.id] = self

    @property
    def right(self):
        return self.x + self.hp

    def o(self, name):
        if name not in self.outs:
            fail('%s has no output "%s" (has: %s)' % (self.model, name, ', '.join(self.outs)))
        return (self, self.outs[name])

    def i(self, name):
        if name not in self.ins:
            fail('%s has no input "%s" (has: %s)' % (self.model, name, ', '.join(self.ins)))
        return (self, self.ins[name])


AUDIO, CV, GATE, PITCH, MOD = '#3695ef', '#00b56e', '#f3374b', '#ffb437', '#c9b70e'


def cable(src, dst, color=AUDIO):
    (om, o), (im, i) = src, dst
    if (im.id, i) in used_inputs:
        fail('%s input %d already has a cable' % (im.model, i))
    used_inputs.add((im.id, i))
    _id[0] += 1
    cables.append({'id': _id[0], 'outputModuleId': om.id, 'outputId': o,
                   'inputModuleId': im.id, 'inputId': i, 'color': color})


class Chain:
    """Places modules left to right on a row, one after the other."""
    def __init__(self, x, row):
        self.x, self.row = x, row

    def __call__(self, model, params=None, plugin='Fundamental'):
        m = M(plugin, model, self.x, self.row, params)
        self.x = m.right
        return m


# ---- value helpers (param scales as the modules display them)

def ms(t):        # ADSR times: 1 ms at 0 to 10 s at 1
    return math.log10(t) / 4


def delay_s(t):   # Delay time: 1 ms at 0 to 10 s at 1
    return math.log10(t / 0.001) / 4


def cutoff(hz):   # VCF: 261.63 Hz x 2^(10 v - 5)
    return (math.log2(hz / 261.6256) + 5) / 10


def lfo_hz(hz):   # LFO: 2^v Hz
    return math.log2(hz)


STEP = 0.125      # a 16th at 120 BPM
# The saw of the Fundamental VCO carries about +2 V of DC (its curved analog shape); the wavetable
# VCO at two thirds of its basic table is a saw without DC, so every saw here comes from VCO2.
SAW = 2 / 3
DOTTED_8TH = 3 * STEP


def preset(path):
    p = json.load(open(path))
    return {q['id']: q['value'] for q in p['params']}, p.get('data')


seq = sorted(glob.glob(os.path.join(ROOT, 'presets/XL185/*.vcvm')))
lanes = sorted(glob.glob(os.path.join(ROOT, 'presets/XL185Mod/*.vcvm')))
assert len(seq) == 8 and len(lanes) == 4, (len(seq), len(lanes))

# ---- left column: clock, reset, row faders, audio, notes

clock = M('Fundamental', 'LFO', 0, 0, {'Offset': 0, 'Frequency': lfo_hz(8.0), 'Pulse width': 0.5})  # 16ths at 120 BPM
reset = M('Fundamental', 'Push', clock.right, 0)
MASTER = 0.5
mixA = M('Fundamental', 'VCMixer', 0, 1, {'Mix level': MASTER, 'Channel 1 level': 1.0, 'Channel 2 level': 0.0,
                                         'Channel 3 level': 0.0, 'Channel 4 level': 0.0})
mixB = M('Fundamental', 'VCMixer', 0, 2, {'Mix level': MASTER, 'Channel 1 level': 0.0, 'Channel 2 level': 0.0,
                                         'Channel 3 level': 0.0, 'Channel 4 level': 0.0})
# Both row mixers summed to one mono bus that feeds left and right (an input takes one cable only).
bus = M('Fundamental', 'Mixer', TABLE['Core']['models']['AudioInterface2']['hp'], 3, {'Level': 1.0})
cable(mixA.o('Mix'), bus.i('Channel 1'))
cable(mixB.o('Mix'), bus.i('Channel 2'))
if not NO_AUDIO:
    audio = M('Core', 'AudioInterface2', 0, 3, {'Level': 1.0})
    cable(bus.o('Mix'), audio.i('Audio 1'))
    cable(bus.o('Mix'), audio.i('Audio 2'))
NOTES_TEXT = '\n'.join([
    'XL-185 factory presets, one per row, 01 at the top. One clock (the LFO, 16ths at 120 BPM) runs every '
    'row; the button next to it resets all rows to stage 1.',
    '',
    'The two mixers are the row faders: upper mixer = rows 1 to 4, lower = rows 5 to 8. Only row 1 is open. '
    'The small mixer next to the audio module sums both to left and right.',
    'Rows 1 to 4 carry the four XL-185 MOD presets; lanes A, B, C modulate the voice.',
    '',
    'Worth turning:',
    '1 Rolling Bass - saw + sub square, driven low pass; VCF cutoff and drive. A opens the filter, B drive.',
    '2 Acid Line - resonant saw, VEL = accent; VCF cutoff and resonance. A cutoff, B resonance, C echo.',
    '3 Offbeat Stabs - three saws (root, detuned, fifth); VCF cutoff, Delay feedback. A resonance, B feedback.',
    '4 Five Against Four - two-operator FM; the carrier FM knob is the brightness. A shape, B FM decay.',
    '5 Sparse Dub - PWM pulse into a long echo; Delay feedback and time.',
    '6 Back and Forth Arp - wavetable pluck; wavetable position, ADSR decay.',
    '7 Percussion Triggers - a drum kit on the stage gate jacks: kick (stages 1 3 5 7), snare (3 7), '
    'closed hat (2 4 6), open hat (8). Kick decay on its ADSR, hat filter cutoff.',
    '8 Random Locked Pair - hard sync lead; the sync VCO frequency and its FM amount.',
])
notes = M('Core', 'Notes', 0, 4, data={'text': NOTES_TEXT})

X = 20  # the rows start right of the column
for row_mods in placed.values():
    for x0, x1, model in row_mods:
        if x1 > X + 1e-6:
            fail('column module %s reaches %s, past the rows at %s' % (model, x1, X))


# ---- the voices; each returns the module and output name that goes to the mixer

def rolling_bass(c, xl, lane):
    saw = c('VCO2', {'Frequency': 0.0, 'Wavetable position': SAW})
    sub = c('VCO', {'Frequency': -12.0, 'Pulse width': 0.5})
    mix = c('Mixer', {'Level': 0.5})
    fenv = c('ADSR', {'Attack': ms(1), 'Decay': ms(140), 'Sustain': 0.15, 'Release': ms(60)})
    cvm = c('CVMix', {'Level 1': 0.35, 'Level 2': 0.25})
    vcf = c('VCF', {'Cutoff frequency': cutoff(140), 'Resonance': 0.2, 'Cutoff frequency CV': 1.0,
                    'Drive': 0.25, 'Drive CV': 0.4})
    aenv = c('ADSR', {'Attack': ms(2), 'Decay': ms(200), 'Sustain': 0.7, 'Release': ms(40)})
    vca = c('VCA-1', {'Level': 0.38, 'Response mode': 1})
    for v in (saw, sub):
        cable(xl.o('VOCT_OUTPUT'), v.i('1V/octave pitch'), PITCH)
    cable(saw.o('Wavetable'), mix.i('Channel 1'))
    cable(sub.o('Square'), mix.i('Channel 2'))
    cable(xl.o('GATE_OUTPUT'), fenv.i('Gate'), GATE)
    cable(xl.o('GATE_OUTPUT'), aenv.i('Gate'), GATE)
    cable(fenv.o('Envelope'), cvm.i('CV 1'), CV)
    cable(lane.o('LANE_OUTPUTS 1'), cvm.i('CV 2'), MOD)
    cable(lane.o('LANE_OUTPUTS 2'), vcf.i('Drive'), MOD)
    cable(cvm.o('Mix'), vcf.i('Frequency'), CV)
    cable(mix.o('Mix'), vcf.i('Audio'))
    cable(vcf.o('Lowpass filter'), vca.i('Channel'))
    cable(aenv.o('Envelope'), vca.i('CV'), CV)
    return vca.o('Channel')


def acid_line(c, xl, lane):
    vco = c('VCO2', {'Frequency': 0.0, 'Wavetable position': SAW})
    fenv = c('ADSR', {'Attack': ms(1), 'Decay': ms(180), 'Sustain': 0.0, 'Release': ms(120)})
    cvm = c('CVMix', {'Level 1': 0.3, 'Level 2': 0.12, 'Level 3': 0.1})
    vcf = c('VCF', {'Cutoff frequency': cutoff(180), 'Resonance': 0.72, 'Cutoff frequency CV': 1.0,
                    'Drive': 0.45, 'Resonance CV': 0.15})
    aenv = c('ADSR', {'Attack': ms(1), 'Decay': ms(300), 'Sustain': 0.6, 'Release': ms(40)})
    vca = c('VCA', {'Channel 1 level': 1.0, 'Channel 2 level': 1.0})
    dly = c('Delay', {'Time': delay_s(DOTTED_8TH), 'Feedback': 0.35, 'Tone': 0.5, 'Mix': 0.2, 'Mix CV': 0.15})
    cable(xl.o('VOCT_OUTPUT'), vco.i('1V/octave pitch'), PITCH)
    cable(xl.o('GATE_OUTPUT'), fenv.i('Gate'), GATE)
    cable(xl.o('GATE_OUTPUT'), aenv.i('Gate'), GATE)
    cable(fenv.o('Envelope'), cvm.i('CV 1'), CV)
    cable(xl.o('VEL_OUTPUT'), cvm.i('CV 2'), CV)
    cable(lane.o('LANE_OUTPUTS 1'), cvm.i('CV 3'), MOD)
    cable(lane.o('LANE_OUTPUTS 2'), vcf.i('Resonance'), MOD)
    cable(lane.o('LANE_OUTPUTS 3'), dly.i('Mix'), MOD)
    cable(cvm.o('Mix'), vcf.i('Frequency'), CV)
    cable(vco.o('Wavetable'), vcf.i('Audio'))
    cable(vcf.o('Lowpass filter'), vca.i('Channel 1'))
    cable(aenv.o('Envelope'), vca.i('Channel 1 linear CV'), CV)
    cable(vca.o('Channel 1'), vca.i('Channel 2'))
    cable(xl.o('VEL_OUTPUT'), vca.i('Channel 2 linear CV'), CV)
    cable(vca.o('Channel 2'), dly.i('Audio'))
    return dly.o('Mix')


def offbeat_stabs(c, xl, lane):
    oscs = [c('VCO2', {'Frequency': f, 'Wavetable position': SAW}) for f in (0.0, 0.15, 7.0)]  # root, 15 cents up, a fifth up
    mix = c('Mixer', {'Level': 1.0})
    vcf = c('VCF', {'Cutoff frequency': cutoff(700), 'Resonance': 0.25, 'Cutoff frequency CV': 0.6,
                    'Resonance CV': 0.3})
    env = c('ADSR', {'Attack': ms(2), 'Decay': ms(160), 'Sustain': 0.0, 'Release': ms(120)})
    vca = c('VCA-1', {'Level': 1.0, 'Response mode': 1})
    dly = c('Delay', {'Time': delay_s(DOTTED_8TH), 'Feedback': 0.45, 'Tone': 0.5, 'Mix': 0.3,
                      'Feedback CV': 0.25})
    for k, v in enumerate(oscs):
        cable(xl.o('VOCT_OUTPUT'), v.i('1V/octave pitch'), PITCH)
        cable(v.o('Wavetable'), mix.i('Channel %d' % (k + 1)))
    cable(mix.o('Mix'), vcf.i('Audio'))
    cable(xl.o('GATE_OUTPUT'), env.i('Gate'), GATE)
    cable(env.o('Envelope'), vcf.i('Frequency'), CV)
    cable(lane.o('LANE_OUTPUTS 1'), vcf.i('Resonance'), MOD)
    cable(lane.o('LANE_OUTPUTS 2'), dly.i('Feedback'), MOD)
    cable(vcf.o('Lowpass filter'), vca.i('Channel'))
    cable(env.o('Envelope'), vca.i('CV'), CV)
    cable(vca.o('Channel'), dly.i('Audio'))
    return dly.o('Mix')


def five_against_four(c, xl, lane):
    menv = c('ADSR', {'Attack': ms(1), 'Decay': ms(250), 'Sustain': 0.1, 'Release': ms(150), 'Decay CV': 0.3})
    mod = c('VCO', {'Frequency': 12.0})  # modulator at twice the carrier
    car = c('VCO2', {'Frequency': 0.0, 'Wavetable position': 0.0, 'Frequency modulation': 0.4,
                     'FM mode': 1, 'Wavetable position CV': 0.5})
    aenv = c('ADSR', {'Attack': ms(1), 'Decay': ms(400), 'Sustain': 0.4, 'Release': ms(200)})
    vca = c('VCA', {'Channel 1 level': 1.0, 'Channel 2 level': 0.75})
    dly = c('Delay', {'Time': delay_s(2 * STEP), 'Feedback': 0.3, 'Tone': 0.5, 'Mix': 0.15, 'Mix CV': 0.2})
    cable(xl.o('VOCT_OUTPUT'), mod.i('1V/octave pitch'), PITCH)
    cable(xl.o('VOCT_OUTPUT'), car.i('1V/octave pitch'), PITCH)
    cable(xl.o('GATE_OUTPUT'), menv.i('Gate'), GATE)
    cable(xl.o('GATE_OUTPUT'), aenv.i('Gate'), GATE)
    cable(mod.o('Sine'), vca.i('Channel 1'))
    cable(menv.o('Envelope'), vca.i('Channel 1 linear CV'), CV)
    cable(vca.o('Channel 1'), car.i('Frequency modulation'))
    cable(car.o('Wavetable'), vca.i('Channel 2'))
    cable(aenv.o('Envelope'), vca.i('Channel 2 linear CV'), CV)
    cable(lane.o('LANE_OUTPUTS 1'), car.i('Wavetable position'), MOD)
    cable(lane.o('LANE_OUTPUTS 2'), menv.i('Decay'), MOD)
    cable(lane.o('LANE_OUTPUTS 3'), dly.i('Mix'), MOD)
    cable(vca.o('Channel 2'), dly.i('Audio'))
    return dly.o('Mix')


def sparse_dub(c, xl, lane):
    lfo = c('LFO', {'Frequency': lfo_hz(0.15), 'Offset': 0})
    vco = c('VCO', {'Frequency': 0.0, 'Pulse width': 0.5, 'Pulse width modulation': 0.4})
    vcf = c('VCF', {'Cutoff frequency': cutoff(600), 'Resonance': 0.15, 'Cutoff frequency CV': 0.3})
    env = c('ADSR', {'Attack': ms(5), 'Decay': ms(400), 'Sustain': 0.5, 'Release': ms(300)})
    vca = c('VCA-1', {'Level': 0.85, 'Response mode': 1})
    dly = c('Delay', {'Time': delay_s(DOTTED_8TH), 'Feedback': 0.72, 'Tone': 0.4, 'Mix': 0.45})
    cable(lfo.o('Triangle'), vco.i('Pulse width modulation'), CV)
    cable(xl.o('VOCT_OUTPUT'), vco.i('1V/octave pitch'), PITCH)
    cable(vco.o('Square'), vcf.i('Audio'))
    cable(xl.o('GATE_OUTPUT'), env.i('Gate'), GATE)
    cable(env.o('Envelope'), vcf.i('Frequency'), CV)
    cable(vcf.o('Lowpass filter'), vca.i('Channel'))
    cable(env.o('Envelope'), vca.i('CV'), CV)
    cable(vca.o('Channel'), dly.i('Audio'))
    return dly.o('Mix')


def arp(c, xl, lane):
    vco = c('VCO2', {'Frequency': 0.0, 'Wavetable position': 0.8, 'Wavetable position CV': -0.3})
    env = c('ADSR', {'Attack': ms(1), 'Decay': ms(180), 'Sustain': 0.0, 'Release': ms(180)})
    vcf = c('VCF', {'Cutoff frequency': cutoff(600), 'Resonance': 0.3, 'Cutoff frequency CV': 0.5, 'Drive': 0.3})
    vca = c('VCA-1', {'Level': 0.5, 'Response mode': 1})
    dly = c('Delay', {'Time': delay_s(2 * STEP), 'Feedback': 0.3, 'Tone': 0.5, 'Mix': 0.25})
    cable(xl.o('VOCT_OUTPUT'), vco.i('1V/octave pitch'), PITCH)
    cable(xl.o('GATE_OUTPUT'), env.i('Gate'), GATE)
    cable(env.o('Envelope'), vco.i('Wavetable position'), CV)
    cable(env.o('Envelope'), vcf.i('Frequency'), CV)
    cable(env.o('Envelope'), vca.i('CV'), CV)
    cable(vco.o('Wavetable'), vcf.i('Audio'))
    cable(vcf.o('Lowpass filter'), vca.i('Channel'))
    cable(vca.o('Channel'), dly.i('Audio'))
    return dly.o('Mix')


def drums(c, xl, lane):
    g = lambda k: xl.o('STAGE_GATE_OUTPUTS %d' % k)
    gates = c('Unity', {'Channel 1 mode': 0, 'Channel 2 mode': 0})   # 1: kick gates, 2: snare gates
    hgates = c('Mixer', {'Level': 1.0})                               # closed hat gates
    noise = c('Noise')
    kpitch = c('ADSR', {'Attack': ms(1), 'Decay': ms(20), 'Sustain': 0.0, 'Release': ms(20)})
    kick = c('VCO', {'Frequency': -29.0, 'Frequency modulation': 0.35})  # G1, 49 Hz
    kamp = c('ADSR', {'Attack': ms(1), 'Decay': ms(90), 'Sustain': 0.0, 'Release': ms(90)})
    sf = c('VCF', {'Cutoff frequency': cutoff(400), 'Resonance': 0.45})
    senv = c('ADSR', {'Attack': ms(1), 'Decay': ms(50), 'Sustain': 0.0, 'Release': ms(50)})
    ks = c('VCA', {'Channel 1 level': 1.0, 'Channel 2 level': 0.8})  # 1: kick, 2: snare
    hf = c('VCF', {'Cutoff frequency': cutoff(5000), 'Resonance': 0.3})
    chh = c('ADSR', {'Attack': ms(1), 'Decay': ms(15), 'Sustain': 0.0, 'Release': ms(15)})
    ohh = c('ADSR', {'Attack': ms(1), 'Decay': ms(90), 'Sustain': 0.0, 'Release': ms(90)})
    hats = c('VCA', {'Channel 1 level': 0.6, 'Channel 2 level': 0.5})  # 1: closed, 2: open
    sub = c('Mixer', {'Level': 0.6})
    for k, s in enumerate((1, 3, 5, 7)):
        cable(g(s), gates.i('Channel 1 #%d' % (k + 1)), GATE)
    for k, s in enumerate((3, 7)):
        cable(g(s), gates.i('Channel 2 #%d' % (k + 1)), GATE)
    for k, s in enumerate((2, 4, 6)):
        cable(g(s), hgates.i('Channel %d' % (k + 1)), GATE)
    cable(gates.o('Channel 1 mix'), kpitch.i('Gate'), GATE)
    cable(gates.o('Channel 1 mix'), kamp.i('Gate'), GATE)
    cable(gates.o('Channel 2 mix'), senv.i('Gate'), GATE)
    cable(hgates.o('Mix'), chh.i('Gate'), GATE)
    cable(g(8), ohh.i('Gate'), GATE)
    cable(kpitch.o('Envelope'), kick.i('Frequency modulation'), CV)
    cable(kick.o('Sine'), ks.i('Channel 1'))
    cable(kamp.o('Envelope'), ks.i('Channel 1 linear CV'), CV)
    cable(noise.o('Pink noise'), sf.i('Audio'))
    cable(sf.o('Highpass filter'), ks.i('Channel 2'))
    cable(senv.o('Envelope'), ks.i('Channel 2 linear CV'), CV)
    cable(noise.o('White noise'), hf.i('Audio'))
    cable(hf.o('Highpass filter'), hats.i('Channel 1'))
    cable(hf.o('Highpass filter'), hats.i('Channel 2'))
    cable(chh.o('Envelope'), hats.i('Channel 1 linear CV'), CV)
    cable(ohh.o('Envelope'), hats.i('Channel 2 linear CV'), CV)
    for k, out in enumerate((ks.o('Channel 1'), ks.o('Channel 2'), hats.o('Channel 1'), hats.o('Channel 2'))):
        cable(out, sub.i('Channel %d' % (k + 1)))
    return sub.o('Mix')


def sync_lead(c, xl, lane):
    master = c('VCO', {'Frequency': 0.0})
    slave = c('VCO2', {'Frequency': 7.0, 'Wavetable position': SAW, 'Sync': 0, 'Frequency modulation': 0.3})
    env = c('ADSR', {'Attack': ms(1), 'Decay': ms(260), 'Sustain': 0.3, 'Release': ms(120)})
    vcf = c('VCF', {'Cutoff frequency': cutoff(2500), 'Resonance': 0.1, 'Drive': 0.3})
    vca = c('VCA-1', {'Level': 1.0, 'Response mode': 1})
    dly = c('Delay', {'Time': delay_s(DOTTED_8TH), 'Feedback': 0.35, 'Tone': 0.5, 'Mix': 0.2})
    cable(xl.o('VOCT_OUTPUT'), master.i('1V/octave pitch'), PITCH)
    cable(xl.o('VOCT_OUTPUT'), slave.i('1V/octave pitch'), PITCH)
    cable(master.o('Square'), slave.i('Sync'))
    cable(xl.o('GATE_OUTPUT'), env.i('Gate'), GATE)
    cable(env.o('Envelope'), slave.i('Frequency modulation'), CV)
    cable(slave.o('Wavetable'), vcf.i('Audio'))
    cable(vcf.o('Lowpass filter'), vca.i('Channel'))
    cable(env.o('Envelope'), vca.i('CV'), CV)
    cable(vca.o('Channel'), dly.i('Audio'))
    return dly.o('Mix')


VOICES = {'01': rolling_bass, '02': acid_line, '03': offbeat_stabs, '04': five_against_four,
          '05': sparse_dub, '06': arp, '07': drums, '08': sync_lead}

row_out = []
for r, path in enumerate(seq):
    params, data = preset(path)
    xl = M('KansuAudio', 'XL185', X, r, params, data)
    cable(clock.o('Square'), xl.i('CLOCK_INPUT'), GATE)
    cable(reset.o('Trigger'), xl.i('RESET_INPUT'), GATE)
    lane = None
    if r < len(lanes):
        lp, ld = preset(lanes[r])
        lane = M('KansuAudio', 'XL185Mod', xl.right, r, lp, ld)
    c = Chain(xl.right + KANSU['XL185Mod']['hp'], r)
    out = VOICES[os.path.basename(path)[:2]](c, xl, lane)
    mixer, ch = (mixA, r + 1) if r < 4 else (mixB, r - 3)
    cable(out, mixer.i('Channel %d' % ch))
    row_out.append((os.path.basename(path)[:-5], out))

# Every cable joins two modules of this patch on ports they have.
for cb in cables:
    om, im = OBJ.get(cb['outputModuleId']), OBJ.get(cb['inputModuleId'])
    if om is None or im is None:
        fail('cable %d joins a module that is not in the patch' % cb['id'])
    if cb['outputId'] not in om.outs.values() or cb['inputId'] not in im.ins.values():
        fail('cable %d: %s output %d or %s input %d does not exist' % (cb['id'], om.model, cb['outputId'], im.model, cb['inputId']))
assert len({cb['id'] for cb in cables} | {m['id'] for m in modules}) == len(cables) + len(modules)

patch = {'version': '2.6.4', 'zoom': 0.0, 'modules': modules, 'cables': cables}
out = os.path.join(ROOT, 'patches', 'XL-185 presets.vcv') if not NO_AUDIO else sys.argv[-1]
os.makedirs(os.path.dirname(out), exist_ok=True)
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode='w') as tar:
    raw = json.dumps(patch, indent=1).encode()
    info = tarfile.TarInfo('patch.json'); info.size = len(raw)
    tar.addfile(info, io.BytesIO(raw))
subprocess.run(['zstd', '-q', '-f', '-o', out], input=buf.getvalue(), check=True)

print(out)
print('%d modules, %d cables, no overlaps, every cable on a named port' % (len(modules), len(cables)))
for r in sorted(placed):
    row = sorted(placed[r])
    voice = [m for x0, x1, m in row if x0 >= X and m not in KANSU]
    print('  row %d: %2d voice modules, ends at %5.1f HP: %s' % (r + 1, len(voice), row[-1][1], ' '.join(voice)))
