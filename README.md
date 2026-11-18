# XL-185 for VCV Rack

Kansu Audio plugin for VCV Rack 2 with two modules:

- **XL-185**: an eight-stage sequencer. Each stage holds a pitch for 1 to 8 clock steps and rests,
  plays once, holds, or repeats on every 4th, 3rd, 2nd or single step. Scale quantizer, a gate output
  per stage, random tools with locks.
- **XL-185 MOD**: an expander placed to the right of XL-185, with three modulation lanes that take a
  value per stage.

The manual is in `docs/MANUAL.md`. Factory presets are in `presets/`, written by
`tools/make_presets.py`.

## Building

Requires the VCV Rack 2 SDK and the usual Rack plugin toolchain.

    export RACK_DIR=/path/to/Rack-SDK
    make
    make dist      # packs the plugin into dist/
    make install   # copies it into your Rack user folder

The sequencer logic in `core/` has its own tests, built without Rack:

    cd tests
    make test

## Licence

GPL-3.0-or-later. See `LICENSE`.

## Credits

XL-185 started as a rebuild of ML-185 Sequencer by Nico Starke (2009) for Max for Live.
