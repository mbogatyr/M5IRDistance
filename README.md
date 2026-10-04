# M5Template

<!-- Template: replace the name, the text in angle brackets and the
placeholder sections. Screenshots come from the board:
~/.platformio/penv/bin/python tools/serial_cmd.py snap docs/screenshot.png -->

`<One sentence: what this turns the stick into.>` Firmware for the
[M5StickS3](https://docs.m5stack.com/en/core/StickS3).

<p align="center">
  <img src="docs/screenshot.png" width="270" alt="<what the screen shows>">
</p>

The screenshot is taken from the device itself over USB
(`tools/serial_cmd.py snap`), scaled up 3x.

## Features

- `<feature>`
- The screen goes dark after 3 minutes without use and wakes up on a key
  press or when the stick is picked up. Held upside down, the picture turns
  over.

## Controls

Hold the stick upright, screen towards you.

| Button | Where | What it does |
|---|---|---|
| KEY1 | blue, on the front, below the screen | `<action>` |
| KEY2 | on the edge | `<action>` |
| Side button | handled by the power chip, not the firmware | press: on / reset, double press: off, hold: download mode |

When the screen has gone dark, the first press only wakes it.

## Building and flashing

Needs [PlatformIO](https://platformio.org/). The commands below use the path
of the official installer; use plain `pio` if it is on your `PATH`.

```bash
~/.platformio/penv/bin/pio run -e sticks3 -t upload     # build and flash over USB
~/.platformio/penv/bin/pio device monitor -e sticks3    # serial log, 115200 baud
~/.platformio/penv/bin/pio test -e native               # unit tests on the computer
```

PlatformIO has no board definition for the StickS3, so `platformio.ini` uses
`esp32-s3-devkitc-1` with octal PSRAM (`qio_opi`) and 8 MB partitions.

If the upload cannot connect, hold the side button until the internal green
LED blinks (download mode) and try again. If the screen stays dark
afterwards, press the side button once.

**M5Burner.** `pio run -e sticks3 -t merged` builds
`.pio/build/sticks3/firmware-merged.bin`, a full image to be written at
address 0x0, which is what M5Burner expects.

## How it works

`<A few paragraphs on the interesting parts.>`

## Project layout

| Path | What it is |
|---|---|
| `lib/` | Hardware-free logic, unit-tested on the computer |
| `lib/DisplayTimeout` | Turns the screen off after 3 minutes without activity |
| `lib/Orientation` | Decides from the accelerometer whether the stick is upside down |
| `lib/PickupDetector` | Notices the stick being picked up while the screen is off |
| `lib/BatteryFilter` | Keeps the battery percentage steady |
| `src/` | Everything that touches the board: drawing, buttons, wiring |
| `test/` | Unit tests for `lib/` |
| `tools/serial_cmd.py` | Screenshots and test commands over USB |
| `tools/merged_image.py` | The `merged` build target for M5Burner |
| `docs/` | Screenshots, the board reference and the M5Burner listing |

[CLAUDE.md](CLAUDE.md) has the details: design decisions, board quirks and
measurements; [docs/StickS3.md](docs/StickS3.md) is the board reference.

## License

[MIT](LICENSE)
