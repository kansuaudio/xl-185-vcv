# XL-185 for VCV Rack

## What it is

XL-185 is an eight-stage sequencer. Each stage holds a pitch for 1 to 8 clock steps, and in that time
it rests, plays once, holds, or repeats on every 4th, 3rd, 2nd or single step. Because every stage can
have its own length, the loop rarely lines up with the bar: a five-step figure drifts against a
four-to-the-floor kick until you tell it to restart. It quantizes to 26 scales (8 of them microtonal),
has a gate jack per stage for drum work, and a set of random tools that keep locked stages as they
are. The XL-185 MOD expander adds three modulation lanes that step with the stages.

## Quick start

1. Patch a clock (one pulse per step, usually 16ths) into CLOCK.
2. Patch V/OCT and GATE into a voice. VEL gives velocity as a voltage.
3. Run is on by default (the play button next to the RUN jack is lit). Press it to stop and start.
4. If your clock module has a reset output, patch it into RESET. If it has a run output, patch it into
   RUN: the module reads it as a trigger that toggles Run, which is what clock modules send.

## The stage column

Each of the eight columns, from top to bottom:

- **Light**: lit while the stage plays.
- **On** (the stage number): a stage that is off never plays, but keeps its place and its time in the
  loop, like a rest. Its lane values are still sent.
- **Pitch**: -36 to +36 semitones from C4.
- **Velocity** (left bar): 0 to 127. A stage at 0 never plays.
- **Probability** (right bar): 0 to 100 %. Each time the stage would fire, one roll decides.
- **Length** (the 2 x 4 cells): 1 to 8 steps; click a cell to set it. Cells inside the length are
  filled, and the cells on which the stage fires are lit.
- **Mode**: click for the list, or drag.
  - Rest: silent for its length.
  - Once: one note on its first step.
  - Hold: one note held for the whole stage.
  - 1/4: a note on every 4th step of the stage, starting on its first.
  - 1/3: every 3rd step.
  - 1/2: every 2nd step.
  - All: every step.
- **S** (Slide): the gate stays high until the next note, and the pitch glides to it over the Glide
  time. The same pitch after a slide is tied: no new gate. If no note follows, the slid note ends
  3/4 of a step after its slot.
- **L** (Lock): a locked stage keeps all its values when Global, Mutate, Reset or a die is used, on
  this module and on the MOD expander.
- **Gate jack** (dark strip): the gates of this stage's own notes. See the right-click menu for the
  other two behaviours.

## Settings

- **Scale** screen: shows root and scale ("D Dorian"), or Off. Click it for the scale list and the Root
  submenu. Pitches go to the nearest note of the scale; when two are equally near, the lower wins.
  Scales: Chromatic, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor,
  Melodic Minor, Phrygian Dominant, Hungarian Minor, Major Pentatonic, Minor Pentatonic, Minor Blues,
  Whole Tone, Hirajoshi, In-Sen, and the microtonal Rast, Bayati, Sikah, Just Major, Just Minor,
  Harmonic 8-16, Slendro 5-EDO and 7-EDO (marked ~), which output their exact tunings.
- **Direction**: forward, backward, back and forth (the end steps are not played twice), random
  (whole stages in random order, repeats allowed).
- **Length**: how many stages are in the loop, 1 to 8. Stages beyond it are dimmed.
- **Transpose**: -24 to +24 semitones, added before the scale.
- **Restart**: Off, 16, 32, 64 or 128 steps. Every N steps the sequence starts again from the top, so
  an odd loop lines up with the bar again.
- **Swing**: 0 to 100 %. Every second step after a reset is delayed; at 100 % it lands three quarters of
  the way through the pair.
- **Gate**: note length, 1 to 100 % of a step (of the whole stage for Hold). A note always ends 1 ms
  before the next one, so every note gets its own gate.
- **Glide**: 0 to 500 ms, the time of a slide.

Edits while playing take effect on the next step and never move the playhead.

## Tools

- **Global**: new random values on every unlocked stage, 1 to 8, including stages beyond Length and
  stages that are off. Which values change is set by the ticks in the right-click menu (Pitch and
  Velocity by default). Random pitches stay inside the pitch range and the scale.
- **Mutate**: new random values on one unlocked stage that is on and inside Length.
- **Auto** and its interval: Mutate by itself every 4, 8 or 16 bars, right after the last step of the
  bar, so the next bar starts changed. A bar is 16 steps unless set otherwise in the menu. The count
  starts at reset and at load; stop and start keep it, so Auto stays on the bar.
- **Reset**: every unlocked stage back to its default (on, pitch 0, velocity 120, Once, length 1,
  100 %, no slide). Length and the settings stay.
- **Dice** (left of the pitch and velocity rows): new random pitches, or velocities, on every unlocked
  stage.
- **Shift arrows** (top right): rotate the stages inside Length one place left or right. Lock and On
  move with their stage.

Each button press is one step in Rack's undo history: Cmd+Z on a Mac, Ctrl+Z on Windows and Linux.
Auto and triggers at the MUTATE and RANDOM jacks write no undo steps.

## Inputs and outputs

| Jack | Signal |
|------|--------|
| CLOCK | trigger, one step per rising edge (high above 1 V) |
| RESET | trigger; see the notes on clocks |
| RUN | trigger, toggles Run |
| TRANSPOSE | 1 V/oct, rounded to semitones, added before the scale |
| MUTATE | trigger, same as the Mutate button |
| RANDOM | trigger, same as the Global button |
| V/OCT | 1 V/oct, 0 V = C4; notes are kept inside -5 V to +5.58 V |
| GATE | 0 or 10 V |
| VEL | 0 to 10 V (velocity 127 = 10 V), held until the next note |
| SLIDE | 10 V while a note of a slide stage sounds |
| STAGE | 1 ms trigger at every stage start |
| EOC | 1 ms trigger when the loop comes round, and on a Restart step |
| Gates 1 to 8 | 0 or 10 V, one per stage |
| STAGE CV | polyphonic, 8 channels: the pitch of each stage after Transpose and the scale |

## Right-click menu

- **Panel**: Kansu or Ableton colours, saved with the patch.
- **Stage gate outputs**: Note gates (the stage's own notes), High for the whole stage, or Trigger at
  stage start.
- **Global and Mutate change**: ticks for Pitch, Velocity, Probability, Slide, Mode, Length and Seq
  Length (Global only, picks a new Length).
- **Pitch range low / high**: C1 to C7, for the random pitches (default C2 to C4).
- **Velocity range low / high**: 1 to 127, for the random velocities.
- **Steps per bar (Auto)**: 8, 12, 16, 24 or 32.

## XL-185 MOD

Place it directly to the right of XL-185. It has three lanes, A, B and C, each with a value per stage
(0 to 100 %), Min, Max and Smooth.

- At every stage start, rests, stages that are off and random picks included, each lane moves to that
  stage's value: output = 10 V x (Min + value x (Max - Min)). Min above Max turns the lane upside down.
- **Smooth**: 0 to 2000 ms, a straight ramp to the new value; 0 jumps.
- The screens show bare numbers: Min and Max are in %, Smooth in ms.
- **Random** (all three lanes) and each lane's **die**: new values on every unlocked stage.
- **Mutate**: new values on all three lanes for one unlocked stage inside Length.
- **Auto** and its interval: as on the sequencer, with its own button.
- **Reset**: unlocked values to 0, and every lane's Min 0, Max 100, Smooth 0.
- **Shift arrows** per lane: rotate that lane's values inside Length.
- RANDOM and MUTATE inputs: triggers, same as the buttons.

The tools follow the sequencer's locks and Length and are independent of its tools. Each button press
is one undo step; triggers at the jacks and Auto are not. The expander follows the sequencer's panel colours while attached. On its own it outputs
0 V and dims its bars. It reads the sequencer one sample late, which is how Rack expanders work.

## Notes on clocks

- RUN is a trigger that toggles Run, as clock modules send it. Stopping ends the sounding note at once;
  starting again continues from where it stopped.
- RESET while running plays the first step at once, and clocks in the next 1 ms are ignored, so a clock
  module that sends reset together with its first pulse starts cleanly on stage 1. RESET while stopped
  waits for the next clock.
- Note lengths, swing and slides follow the time between the last two clocks. Until two clocks have
  arrived a step is taken as 125 ms (a 16th at 120 BPM). Swing needs a straight clock: do not swing the
  clock module as well.
- With a clock slower than 2 seconds per step the module keeps its last known step time for gate
  lengths.

## Credits

XL-185 started as a rebuild of ML-185 Sequencer by Nico Starke (2009) for Max for Live.

XL-185 is free software under the GNU General Public License, version 3 or later.
