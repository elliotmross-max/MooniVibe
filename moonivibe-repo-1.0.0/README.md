# MooniVibe

A stereo phase shifter for the Ableton Move, built as a [Schwung](https://github.com/charlesvestal/schwung) `audio_fx` module.

Four all-pass stages swept by a lamp, with component values you can swap. The notches come from summing the phase-shifted path with dry.

## How it works

The engine stores component values and derives its coefficients from them:

```
f_pole = 1 / (2 * pi * R * C)
g      = (1 - sin(w)) / cos(w),   w = 2 * pi * f_pole / fs
```

Changing the Capacitor setting moves the stage poles and the notches follow. Nothing is stored as a preset.

Each of the four stages has its own lag state, its own rise and fall rates, and its own resting pole. The stage poles are staggered rather than evenly spaced, so the notches land at different frequencies instead of as a matched pair.

## Controls

| Control | Values | Notes |
|---|---|---|
| **Capacitor** | Stock 220n / Dark 100n / Mid 47n / Bright 22n | Sets where the notches sit |
| **Lamp** | Matched / Stock / Wide / Drift | How closely the four cells are matched |
| **Drive Res** | Clean 100k / Stock 47k / Hot 22k / Slam 10k | Level into the phase network |
| **Diodes** | None / Silicon / Germanium / LED | Clipping on the wet path |
| **Lamp LFO** | 0 — 8 Hz | A second modulation on the sweep depth |
| **Lamp Bias** | 0 — 1 | The lamp's resting brightness |
| **Speed** | 0.02 — 12 Hz | Main LFO rate |
| **Intensity** | 0 — 1 | Sweep depth |
| **Mix** | 0 — 1 | Dry to equal parts, where the comb is deepest |
| **Output** | 0 — 1 | Output level |
| **Mode** | Chorus / Vibrato | Dry summed, or the phase-shifted path alone |
| **Stages** | 4 / 6 | Cascade length |
| **LFO Shape** | Sine / Triangle / Soft Square | |
| **Stereo Spread** | 0 — 1 | Offset between the two channels |
| **Cell Mismatch** | 0 — 1 | Scales the differences between the four cells |

## Layout

**Knob row:** Speed · Intensity · Mix · Output · Lamp LFO · Lamp Bias · Capacitor · Lamp

**Parameter menu:** Mode · Diodes · Drive Res · Stages · LFO Shape · Stereo Spread · Cell Mismatch

## Building

```shell
bash scripts/build.sh      # cross-compile for the Move, needs Docker
bash scripts/install.sh    # copy to the Move over SSH
ssh root@move.local reboot
```

The engine has an off-device test that runs on your Mac with no Docker and no Move:

```shell
bash scripts/test.sh
```

The vendored header at `vendor/schwung/plugin_api_v1.h` is committed, so the repo builds for anyone who clones it. Check it against upstream with `bash scripts/vendor-header.sh check`.

## Note

Audio effects in a Signal Chain export `move_audio_fx_init_v2`, and the binary is named `<id>.so` rather than `dsp.so`. This module exports both the v1 and v2 entry points.

Built with AI coding assistance for design, implementation and debugging. Disclosed per Schwung's CONTRIBUTING.md.
