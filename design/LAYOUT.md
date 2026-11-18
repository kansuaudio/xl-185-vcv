# XL-185 panel layout

Single source for the sequencer panel. `design/layout.py` is this document in executable form: it
holds every coordinate, writes the panel SVGs, `src/Layout.hpp`, a preview PNG per theme and
`design/RULER.json`. Change this file and `layout.py` together, before the widget code.

Direction: light Eurorack panel (grey / off-white, as Impromptu CLOCKED and VCV
VCO); controls that belong together sit on a shared background area in another tone; not everything
is a knob (selectors, sliders, small screens, closer to Ableton Live); two colour variants, "Ableton"
and "Kansu"; Kansu Audio logo on the panel.

## Panel

36 HP = 182.88 x 128.5 mm. Units below are mm, origin top left. These are the final numbers from
`layout.py` (rounded); the ruler is green. Four stock screws at x 7.62 from each side, y 2.54 and
125.96; nothing sits on them.

## Areas

Outer margin 3.5, 2 between areas, corner radius 1.5, inner padding 1.6.

| Area | x | y | Tone | Holds |
|------|---|---|------|-------|
| Header | 0..182.88 | 0..12.5 | panel | "XL-185", shift arrows at the right over the stages |
| Settings | 3.5..45.5 | 13..66.5 | light area | scale screen, direction, length, transpose, restart, swing, gate, glide |
| Tools | 3.5..45.5 | 68.5..96 | light area | Global, Mutate, Auto + interval, Reset |
| Stages | 47.5..179.38 | 13..82 | light area | eight columns, row labels and the two dice in the left gutter |
| Gates | 47.5..179.38 | 84..96 | dark strip | eight stage gate output jacks under their columns |
| In | 3.5..86 | 98..122.4 | light area | Clock, Reset, Run (+ Run button), Transpose, Mutate, Random |
| Out | 88..179.38 | 98..122.4 | dark strip | V/Oct, Gate, Vel, Slide, Stage, EOC, Stage CV |
| Footer | 0..182.88 | 122.4..128.5 | panel | Kansu mark and "KANSU AUDIO", one group centred on the panel |

Every area has a title in its top left corner, upper case in both themes ("SETTINGS", "TOOLS",
"STAGES", "GATES", "IN", "OUT"); labels are lower case in both themes. The stage gate strip is titled
"GATES" because "STAGE GATES" does not fit the 17 mm gutter. Outputs always sit on dark, inputs on light.

Header: "XL-185" alone (title font, 20 px, letter spacing 0.5 px) centred on the panel centre
(x 91.44), y 6.15, over the footer group; shift label and two
arrow buttons 6.4 x 5 at x 155..169, y 3.7.

Footer: the band under the areas (y 122.4..128.5, between the bottom screws) holds the company mark,
not the product: the Kansu mark 3.4 tall (3.3 wide, x 77.1, y 123.75), then "KANSU AUDIO" (title
font, 11 px, letter spacing 1.5 px, ink x 82.0..105.8, capitals y 124.4..126.5) 1.6 to its right.
The group (x 77.1..105.8) is centred on the panel centre x 91.44 and on the band (mark centre and
capital centre at y 125.45, 1.35 clear above and below the mark). Centred, not right aligned: the
bottom right screw sits under the right end of the Out area, so a right aligned group would run into it.

## Stage column (eight, centres x = 72.20 + 14.21 n)

The gutter is 17 wide so "vel / prob" fits on one line at the label size; that leaves 14.21 per column.

| Row | y | Control | Type |
|-----|---|---------|------|
| Playing | 16.3 | light | flat dot 2 mm, plain fill: off tone unlit, accent when the stage plays |
| Number / On | 19.4..24.4 | stage number, lit when on | lit cell button 9 x 5 |
| Pitch | 30.9 | -36..+36 st | stock RoundBlackKnob (9.6 mm), the only big one |
| Velocity, Probability | 37.4..58.0 | two bars side by side at x -3.3 / +3.3 | vertical sliders 3.2 x 20.6 with lit fill |
| Length | 59.8..65.5 | cells 1..4 over 5..8 | 2 x 4 clickable cells 2.7 x 2.6, gap 0.5; cells up to Len in the mid tone, cells on which the stage fires in the accent |
| Mode | 67.1..72.1 | Rest Once Hold 1/4 1/3 1/2 All | screen cell 12.4 x 5, click opens the list, drag steps |
| Slide, Lock | 74.0..78.2 | two at x -3.3 / +3.3, letters S and L | lit buttons 5.2 x 4.2 |
| Gate out | 90.6 | jack | stock dark PJ301M on the dark strip |

Left gutter of the Stages area (x 49.1..65.1): area title, row names (on, pitch, vel / prob, len, mode,
sl / lk) and the Pitch and Velocity dice (5.4 x 4.6, three-pip glyph) under their row names.
Stages beyond Length are drawn dimmed (number cell, bars, cells, mode cell).

## Settings

Label above each control, x 5.1..43.9.

| Control | Box | Type |
|---------|-----|------|
| Root + Scale | 38.8 x 5.8 at y 23.1 | one screen ("D Dorian", "Off"); click opens Root (submenu) and the Scale list; the left 7 mm is the Root param, the rest the Scale param, so both have their own tooltip and right-click menu |
| Direction | 38.8 x 5.8 at y 34.4 | four-segment selector with arrow glyphs (forward, backward, back and forth, random) |
| Length | 9 x 5.8 at y 46.2 | number screen, drag; 1..8 |
| Transpose | 13.6 x 5.8 | number screen, drag; -24..+24 ("+3") |
| Restart | 9.2 x 5.8 | number screen, drag or click for the list; Off 16 32 64 128 |
| Swing, Gate, Glide | centres y 57.4 | stock RoundSmallBlackKnob (7.7 mm), names under them |

The three number screens are as wide as their labels so the labels keep 1.2 clear.

## Tools

Global, Mutate (18.5 x 6.8 at y 75.1), Reset (12.3 x 6.8 at y 84.1): momentary text buttons that
light while held. Auto (10.4 x 6.8): lit toggle; its interval is a number screen beside it (13.4 x 6.8,
"4 bars" / "8 bars" / "16 bars", drag or click for the list), dim while Auto is off.

## In and Out

Label row at y 105, jacks at y 112.8, spread evenly by label width. In: stock PJ301M. Out: stock dark
PJ301M. The Run button (6 x 6, play glyph, lit while running) sits right of the Run jack under one
"run" label.

## Themes

Chosen in the context menu ("Panel"), saved with the patch (key "panel"); default Kansu.

| Token | Ableton | Kansu |
|-------|---------|-------|
| panel | #BEBEBE | #E8E4DB |
| area (light area) | #D6D6D6 | #DAD5C9 |
| dark (dark strip) | #3A3A3A | #1B1A18 |
| text (on panel and areas) | #121212 | #1A1917 |
| textDark (text on dark) | #E6E6E6 | #E8E4DB |
| screen (screen background) | #EDEDED | #1B1A18 |
| screenText | #121212 | #E8E4DB |
| accentLight (bar fills, lit cells, active buttons, playing light, on light parts) | #FFAD56 | #1A1917 |
| accentDark (accent on dark parts) | #FFAD56 | #E8E4DB |
| onAccent (text and glyphs on a lit cell) | #121212 | #E8E4DB |
| off (unlit cells, bar tracks, unlit light) | #9D9D9D | #C5BFB1 |
| mid (Len cells inside Len that do not fire) | #626262 | #8E887C |
| dim (beyond Length, Auto interval off) | 40 % toward the area tone | same |

Kansu has no product accent for XL-185; it stays warm monochrome.
The Ableton off tone is darker than the screens so buttons and screens read apart, and far enough
from the orange in lightness to tell lit from unlit without colour.
No text below 10 px at 100 % zoom; text colour is a solid colour, never an alpha of another.

## Text and logo

- Labels and screen text are drawn in code, so the SVG holds no text. Labels and screens use fonts
  that ship inside Rack (Nunito Bold for labels, ShareTech Mono for screens). The module names
  ("XL-185", "XL-185 MOD") and the footer words use the brand's title font, JetBrains Mono Bold, which ships
  in the plugin (`res/fonts/JetBrainsMono-Bold.ttf`, SIL OFL 1.1 in `res/fonts/JetBrainsMono-LICENSE.txt`).
  Sizes are nanovg font sizes in Rack px: labels, titles and cell text 11, screens 12, names 20,
  footer words 11. Letter spacing (nanovg letter spacing, Rack px, between glyphs): names
  0.5, footer words 1.5, everything else 0. One solid colour, the theme text colour.
- The logo is the Kansu mark (the kanji from `website/assets/brand/kansu-icon-1024.png`) traced to
  one even-odd SVG path (`design/logo_path.txt`), in the theme text colour, in the footer of
  both panels, left of "KANSU AUDIO".

## Ruler

`design/RULER.json` is green when: every control lies inside its area and the panel, no two controls
or labels overlap, neighbours keep 1.2 mm clear (jacks 2 mm; Auto and its interval 0.8 as one pair),
nothing touches a screw, every control has a label or sits in a labelled row, every parameter, input,
output of the module (read from the enums in `src/XL185.cpp`) appears exactly once, no text is below
10 px, and text contrast is at least 4.5 (3 when dimmed) in both themes. The footer group (mark and
words) lies inside the panel, keeps 1.2 mm from the screws and 0.8 mm from the panel bottom and from
the areas above.

# XL-185 MOD panel layout (expander)

Kansu is the default theme. The expander uses the same areas, tones, fonts,
bars, screens and buttons as the sequencer, and follows the sequencer's theme while attached (its own
"Panel" menu choice applies only when it stands alone). `design/layout.py` has a second table and
writes `res/XL185Mod-kansu.svg`, `res/XL185Mod-ableton.svg`, `src/LayoutMod.hpp`, a preview per theme
(`preview-mod-*.png`), the pair as it sits in the rack (`preview-pair-*.png`) and the ruler entries.
Final numbers from `layout.py` (rounded); the ruler is green for both modules.

## Panel

16 HP = 81.28 x 128.5 mm. Sits directly to the right of XL-185. Four stock screws at x 7.62 from each
side, y 2.54 and 125.96.

## Areas

| Area | x | y | Tone | Holds |
|------|---|---|------|-------|
| Header | 0..81.28 | 0..12.5 | panel | "XL-185 MOD" (title font, 20 px, centred on the panel centre x 40.64 as the sequencer's name; the footer holds the same mark and "KANSU AUDIO" group as the sequencer, centred on this panel), stage numbers 1..8 at y 10.55 over the bar columns |
| Lane A | 3.5..63 | 13..40 | light area | see Lane |
| Lane B | 3.5..63 | 42..69 | light area | |
| Lane C | 3.5..63 | 71..98 | light area | |
| Out | 65.5..77.78 | 13..98 | dark strip | A, B, C output jacks, each level with its lane |
| Tools | 3.5..48 | 100..122.4 | light area | Random, Mutate, Auto + interval, Reset |
| In | 50..77.78 | 100..122.4 | light area | Random and Mutate trigger jacks |

The lanes are 27 tall, not 28: the bottom row needs 22.4 for a title, a label and a jack (the
sequencer's In and Out rows are as tall), so the lanes start at 13 and end at 98.

## Lane (three times; y below is from the lane top)

Eight columns over the lane's inner width, centres x = 8.62 + 7.04 n, the same eight stages as the
sequencer's columns.

| Row | y | Control | Type |
|-----|---|---------|------|
| Title row | 0.9..5.3 | lane title ("LANE A") at the left; die 5.4 x 4.4 at x 39.6, shift left and right 6.4 x 4.4 at x 47.4 and 55.0 | area title, momentary buttons with glyphs |
| Values | 6.5..19.6 | eight values 0..100 %, one bar 4.0 x 13.1 per stage | vertical bars as on the sequencer; the playing stage's track is drawn in the mid tone; bars beyond the sequencer's Length are dimmed |
| Range row | 20.8..25.4 | "min" + screen 8.0 at x 11.5, "max" + screen 8.0 at x 29.1, "smooth" + screen 10.0 at x 51.4 | number screens, drag (full range over 200 px); values without units, the tooltip has them |

Bars are 13.1 tall instead of about 15: the 27 mm lane holds the title row, the bars and the range
row with 1.2 clear between them.

Out: jacks at x 71.64, 14.3 below each lane top, labels "a", "b", "c" 7.8 above each jack.

## Tools

Random and Mutate (19.75 x 6.8 at y 105.8), then Auto (10.4 x 6.8, lit toggle), its interval (13.4 x 6.8,
"4 bars" / "8 bars" / "16 bars", dim while Auto is off) and Reset (14.8 x 6.8) at y 113.8. Same sizes as
the sequencer's tools. Independent of the sequencer's tools.

## In

Labels "random" and "mutate" at y 107, jacks at y 114.8 (x 56.9 and 71.2), in the order of their
buttons.

Without a sequencer on its left the expander shows all bars dimmed and outputs 0 V.
