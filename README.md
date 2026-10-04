# M5IRDistance

An experiment: turning the IR LED and IR receiver built into the
[M5StickS3](https://docs.m5stack.com/en/core/StickS3) into a range finder
that shows the distance to an object in front of the stick, in centimetres.

<p align="center">
  <img src="docs/screenshot.png" width="270" alt="14.1 cm, an echo margin of 18.4 dB, and bars showing which transmit powers were heard">
</p>

The screenshot is taken from the device itself over USB
(`tools/serial_cmd.py snap`), scaled up 3x; this one is a made-up sweep
(`demo 18.4`).

## What it can do

Measured on 2026-10-04 with a sheet of white A4 paper facing the IR end of
the stick:

| Distance | Echo margin | Spread (6 sweeps) |
|---|---|---|
| 5 cm | 38.4 dB | ± 1.0 |
| 10 cm | 22.9 dB | ± 0.3 |
| 20 cm | 13.9 dB | ± 0.2 |
| 40 cm | 0.7 dB | ± 0.4, the limit |
| nothing | no echo | |

So it works from about 5 to 40 cm on white paper. Things to know:

- It measures how bright the echo is, not how long the light takes, so the
  distance is only right for the surface it was calibrated with. A darker
  or smaller object reads as farther away.
- The target must stand still. At 10 cm a 5 mm shift changes the margin by
  about 1 dB, and a sheet held in the hand spread the readings by several
  dB.
- Other IR sources confuse the receiver. A phone close by (its camera or
  face sensors, most likely) produced pulses that passed for echoes; the
  firmware notices them and shows `IR NOISE` instead of a distance.
- The distance shown has not yet been checked against distances outside
  the calibration points. Run `tools/ir_calibrate.py check` (below).

## Features

- The distance in cm once the stick is calibrated, otherwise the echo margin
  in dB. Orange means outside the calibrated distances (extrapolated).
  `NO ECHO` when nothing is heard even at full power, `TOO NEAR` when the
  echo is heard even at the weakest power, `IR NOISE` when the receiver
  hears IR with the LED off.
- Bars at the bottom: one per transmit power, weakest on the left, as high
  as the share of bursts the receiver heard.
- A reading every 0.6 s while the target stands still (a full sweep takes
  1.8 s).
- Calibration points are kept in flash and survive power-off.
- The screen goes dark after 3 minutes without use and wakes up on a key
  press or when the stick is picked up; measuring stops while it is dark.
  Held upside down, the picture turns over.

## Controls

Point the top end of the stick (opposite the USB socket) at the target.

| Button | Where | What it does |
|---|---|---|
| KEY1 | blue, on the front, below the screen | pause / resume measuring |
| KEY2 | on the edge | nothing yet (wakes the screen) |
| Side button | handled by the power chip, not the firmware | press: on / reset, double press: off, hold: download mode |

When the screen has gone dark, the first press only wakes it.

## Calibrating

The board must be connected over USB. The target (a sheet of paper taped
upright to a book or a box) stands on the table at measured distances from
the IR end, with nothing else in the beam: hands, phones and people out of
the way.

```bash
~/.platformio/penv/bin/python tools/ir_calibrate.py measure            # 5, 10, 20, 40 cm
~/.platformio/penv/bin/python tools/ir_calibrate.py check 7 15 30      # compare with the truth
~/.platformio/penv/bin/python tools/ir_calibrate.py list
```

`measure` asks you to place the target, waits 3 s for you to step away,
averages 5 clean sweeps into a point and stores it on the board. `check`
prints the board's estimate and its error at other distances. Both keep a
log in `docs/data/`. `set 5:38.4 10:22.9 ...` stores points measured
earlier; the board currently holds the table above.

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

Time of flight is out of reach: light crosses a centimetre in 67 ps, and the
receiver takes hundreds of microseconds to answer. What is left is how
bright the echo is.

The receiver is not a light sensor, though. It is a 38 kHz remote-control
receiver: it only says whether it hears a 38 kHz carrier, never how strong
it is. So the firmware turns the question around and looks for the weakest
transmission the receiver still hears. It sends bursts of carrier at 24
powers, set by the carrier's duty cycle from 1/1024 to 1/2 (50 dB), from
the weakest up, 4 bursts each, and counts how many of each were heard. The
power at which half of them are heard gives the **echo margin**: how many
dB weaker than full power the LED can get while the echo still comes
through. The closer or brighter the target, the larger the margin.
Calibration points (margin at known distances) then turn the margin into
centimetres, joining them with straight lines in log(distance).

Getting the receiver to give consistent answers took most of the work
(details in [docs/StickS3.md](docs/StickS3.md), "Infrared"):

- Its gain control lowers the gain after every burst it hears and needs
  about 15 ms to recover, so bursts are 0.8 ms of carrier and 15 ms apart.
  With shorter gaps the margin read up to 7 dB low.
- On a strong echo the gain control lifts the output for about 250 µs in
  the middle of the burst, so a burst counts as heard by the total time the
  output is low (at least 300 µs of 1.1 ms), not by its state at fixed
  moments.
- Before each reading the LED stays off for 8 blank bursts; if any of them
  is "heard", the reading is ambient noise.
- After a full sweep, the next one only measures 3 levels either side of
  the last threshold: 0.6 s instead of 1.8 s; if the threshold
  has left that window, the sweep starts over with all levels.

## Serial commands

`tools/serial_cmd.py send <command>` talks to the board; the full list is in
that script and in [CLAUDE.md](CLAUDE.md). The useful ones: `sw` (one full
sweep with the hits per level), `ir` (a line after every reading), `cal`,
`calset`, `callist`, `calclr` (calibration), `wave <level>` (the receiver's
output during one burst), `freq`, `n`, `gap` (sweep parameters), `demo <dB>`
(a made-up reading for screenshots).

`tools/ir_experiment.py` records sweeps at known distances into a CSV file
for studying them; [docs/data/](docs/data) has the measurements behind the
table above.

## Project layout

| Path | What it is |
|---|---|
| `lib/` | Hardware-free logic, unit-tested on the computer |
| `lib/IrSweep` | Transmit powers; turns one sweep into the echo margin |
| `lib/RangeCalibration` | Turns the margin into centimetres from calibration points |
| `lib/DisplayTimeout` | Turns the screen off after 3 minutes without activity |
| `lib/Orientation` | Decides from the accelerometer whether the stick is upside down |
| `lib/PickupDetector` | Notices the stick being picked up while the screen is off |
| `lib/BatteryFilter` | Keeps the battery percentage steady |
| `src/IrProbe` | Drives the IR LED and reads the IR receiver |
| `src/` | Everything else that touches the board: drawing, buttons, wiring |
| `test/` | Unit tests for `lib/` |
| `tools/ir_calibrate.py` | Calibration and checking over USB |
| `tools/ir_experiment.py` | Records raw sweeps at known distances |
| `tools/serial_cmd.py` | Screenshots and test commands over USB |
| `tools/merged_image.py` | The `merged` build target for M5Burner |
| `docs/` | Screenshot, measurements, the board reference and the M5Burner listing |

[CLAUDE.md](CLAUDE.md) has the details: design decisions, board quirks and
measurements; [docs/StickS3.md](docs/StickS3.md) is the board reference.

## License

[MIT](LICENSE)
