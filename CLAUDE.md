# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Project template for the M5StickS3. For now the firmware only shows the time
since power-on and the battery level on the screen; it is a placeholder that
the project's logic replaces.

Repository: https://github.com/mbogatyr/M5Template

Knowledge about the board that is not needed in every session (audio, IMU,
memory and speed, power, storage, radio, tools) is in
[docs/StickS3.md](docs/StickS3.md). Read the relevant section before working
on that part.

## Language

All documentation and the project itself are kept in English: this file, code
comments, identifiers, strings shown on the display, commit messages and any
other text files. New text is written in English too, even when the
conversation with the user happens in another language.

## New project from the template

1. Copy the folder without `.git/` (the template's own history) and `.pio/`
   (the build cache).
2. `git init`.
3. Change the project name in the first line of `platformio.ini` and in
   `README.md`, rewrite the description at the top of this file, replace the
   `Repository:` line with the project's GitHub link once there is one, and
   delete this section.
4. Delete the `lib/` modules the project does not need, together with their
   `test/test_<module>` folders and their lines in `src/main.cpp`.
5. Fill in `README.md` and, before publishing, `docs/m5burner.md`. `LICENSE`
   is MIT; change it if the project needs another one.

## Commands

PlatformIO is not installed globally but by the official installer into a
venv. The binary lives at `~/.platformio/penv/bin/pio`; it is not on `PATH`,
so it has to be called by its full path. Python tools run with the same
venv's interpreter, `~/.platformio/penv/bin/python`: it has pyserial, but not
numpy or Pillow.

```bash
~/.platformio/penv/bin/pio test -e native                         # host unit tests for the logic
~/.platformio/penv/bin/pio test -e native -f test_display_timeout # a single test suite
~/.platformio/penv/bin/pio run -e sticks3                         # build the firmware
~/.platformio/penv/bin/pio run -e sticks3 -t upload               # flash the board
~/.platformio/penv/bin/pio run -e sticks3 -t merged               # single image for M5Burner
~/.platformio/penv/bin/pio device monitor -e sticks3              # serial monitor, 115200
~/.platformio/penv/bin/python tools/serial_cmd.py snap shot.png   # screenshot from the board
~/.platformio/penv/bin/python tools/serial_cmd.py send st         # a command, see below
```

Only one program can hold the serial port: stop a monitor before flashing.

The xtensa-esp32s3 toolchain is installed into `~/.platformio/packages` once
per machine (about eight minutes) and is shared by all projects. The first
build of a new project downloads M5Unified into `.pio/` and takes about a
minute.

### Build settings that look odd but are needed

- `build_unflags = -std=gnu++11`: the framework appends its own
  `-std=gnu++11` after `build_flags`, and the last one wins. Without the
  unflag the board builds as C++11 while the host tests build as C++17: code
  that passes the tests then fails on the board (brace-initialising a struct
  with default member values, `static constexpr` array members that do not
  link). Checked in the verbose build: only `-std=gnu++17` reaches the
  compiler now.
- `build_unflags = -Os` plus `-O2`: the framework's `-Os` makes per-pixel and
  DSP code about twice as slow. The cost is a slightly larger firmware.
- `upload_port` / `monitor_port = /dev/cu.usbmodem*`: when the board is not on
  USB, PlatformIO otherwise picks `/dev/cu.Bluetooth-Incoming-Port` and fails
  with "No serial data received".
- `m5stack/M5Unified@^0.2.23`: the version that fixed `M5.Power.powerOff()`
  waking the StickS3 straight back up.

## Architecture

The split into `lib/` and `src/` is not cosmetic here, it is load-bearing:

- `lib/` is the logic: plain C++ with no Arduino, no M5Unified and no hardware
  access of any kind.
- `src/` is everything that knows about the board: `Renderer` draws on the
  display, `main.cpp` reads the buttons, the IMU and the battery, and wires
  the logic to them.

The `native` environment builds only `lib/` (PlatformIO's `test_build_src`
defaults to `no`), so the logic is tested on the Mac without the board.
**Do not pull hardware dependencies into `lib/`: that breaks the tests.**
Whatever can be checked without the hardware (a protocol, a state machine, a
filter) goes into `lib/` and is tested there; `src/` stays thin.

### Modules in lib/ from the template

- `DisplayTimeout` turns the display off after 3 minutes without activity.
  What counts as activity is up to `main.cpp`: here a held or pressed key and
  the stick being picked up.
- `Orientation` decides from the accelerometer whether the stick is held
  upside down; `Renderer::setFlipped()` turns the picture. Portrait reads the
  Y axis, landscape the X axis.
- `PickupDetector` wakes a sleeping display when the stick is picked up:
  gravity turning by more than 8° from the pose it was left in, or a jolt. A
  stick lying still at any angle wakes nothing, so the display does go off
  in a pocket.
- `BatteryFilter` steadies the battery percentage, which sags under load.

### Time is passed in as a parameter

The logic in `lib/` does not call `millis()` itself; it receives the current
time as an argument. That way tests can substitute any moment without waiting,
and the logic has no `delay()`: `loop()` just passes `millis()` along.

`millis()` overflows after roughly 49 days. Compute intervals with unsigned
subtraction `now - since`, so the overflow goes unnoticed. To ask whether a
moment is before or after a deadline, use the signed difference
`static_cast<int32_t>(now - deadline) >= 0`: unsigned, a moment slightly
before would read as 49 days after.

Take `now` after any call that blocks (a Bluetooth connect, a flash write),
not before it. Anything that integrates over time (motion, physics) should
clamp the step, for example to 100 ms: after the display sleeps the gap can
be minutes long.

### The loop

`loop()` runs at 50 Hz (`delay(20)`). A project that animates every frame
drops the delay while something moves and lets the drawing pace the loop;
it keeps the 50 Hz delay while idle and while the display is off.

Buttons are read with `wasPressed()` for actions and `isPressed()` for
activity. **A press that wakes the display only wakes it**: the action runs
only if the display was already on (`wasAwake` in `main.cpp`).

### Rendering

`Renderer` composes the whole frame in an `M5Canvas` sprite and pushes it with
a single `pushSprite`. Drawing directly on the screen causes visible flicker.

The sprite (135x240x2 = 65 KB) lives in internal RAM, with PSRAM as the
fallback: code that touches single pixels runs about twice as fast there as
through the PSRAM cache. Large buffers that are read in sequence (sound,
images) belong in PSRAM.

`Renderer::draw()` takes a `Screen` struct with everything the frame shows,
compares it with the previous one and returns if nothing has changed.
`invalidate()` clears that memory; `main.cpp` calls it when the display wakes
up, because the panel's contents are lost during sleep. A project that
animates continuously drops the comparison.

The template is portrait (`setRotation(0)`, KEY1 below the screen;
`setRotation(2)` is the same upside down). For landscape use 1 and 3 and
`Orientation::Layout::Landscape`; the details are in docs/StickS3.md.

M5GFX fonts are ASCII only: anything else is drawn.

## Board specifics

M5StickS3 is an ESP32-S3-PICO-1-N8R8 with 8 MB of flash, 8 MB of octal PSRAM
and an ST7789P3 135x240 display.

- PlatformIO has **no** `m5stack-sticks3` board id. The project uses
  `esp32-s3-devkitc-1` plus `board_build.arduino.memory_type = qio_opi` and
  the `default_8MB.csv` partitions. Do not "fix" this to a non-existent id.
- USB is native, with no CH9102 bridge, so on macOS the port is called
  `/dev/cu.usbmodem*`, not `/dev/cu.usbserial*`. Serial output needs both the
  `-DARDUINO_USB_CDC_ON_BOOT=1` flag and `Serial.begin(115200)` in `setup()`:
  M5Unified's `serial_baudrate` defaults to 0, and without the call the port
  stays silent.
- Buttons: KEY1 on G11 (`M5.BtnA`), the blue one on the front; KEY2 on G12
  (`M5.BtnB`), on the edge. Grove (G9/G10) and HAT2 (G1–G8, G43, G44) are
  free.
- `M5.getBoard()` returns 26 on the StickS3.

### The side button is handled by the PMIC, not the firmware

| Action | Result |
|---|---|
| Single press | Power on / reset |
| Double press | Power off |
| Long hold | Download mode (the internal green LED blinks) |

So the firmware does not need its own power-off button. Powering off from
software works: `M5.Power.powerOff()` on M5Unified 0.2.23 cuts the power, on
USB power too, and the board stays off until the side button is pressed
(checked on 2026-09-28). A firmware that powers itself off must have a
serial command to disable that during development: a board that is off
cannot be flashed.

`M5.Power` does not set `_wakeupPin` for the StickS3, so there is no
ready-made wake-up from deep sleep by button; it would have to be configured
manually with `esp_sleep_enable_ext0_wakeup`.

## Flashing and the serial port

`pio run -t upload` usually needs no button: the auto-reset into the loader
works on a board that is running a firmware, and the firmware starts after
"Hard resetting via RTS pin".

### If flashing fails

`A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.`

The board shows up as `USB JTAG_serial debug unit` (VID 0x303A, PID 0x1001):
that is the built-in USB-Serial-JTAG, not a CDC port (a consequence of
`ARDUINO_USB_MODE=1`). When the auto-reset fails, neither
`--before usb_reset` nor `--before no_reset` helps. The fix is manual: hold
the side button until the green LED blinks, then flash again.

After a flash in that mode the chip stays in the bootloader: the screen stays
dark. One short press of the side button starts the firmware.

If there is no `/dev/cu.usbmodem*` port at all, the board is off (or the
firmware powered it off): press the side button once.

### If the board is stuck in the bootloader

The firmware does not start, and the port shows `boot:0x0 (DOWNLOAD(USB/UART0))`
and `waiting for download`. The USB-Serial-JTAG resets the chip into the
bootloader while RTS is high and DTR is low. macOS raises both lines when a
port opens, and pyserial then lowers DTR before RTS, passing through exactly
that state. The way out is a single short press of the side button.

Scripts talk to the board through `tools/serial_cmd.py`: its `open_port()`
keeps DTR high until RTS is low, and the board does not reset. A plain
`serial.Serial(port)` in a new script will hit this. `pio device monitor` has
not been checked for this effect.

## Self-testing on the board

Claude checks the firmware on the board itself, without asking the user to
look at the screen. `main.cpp` takes line commands over serial, and
`tools/serial_cmd.py` sends them:

| Command | Answer |
|---|---|
| `s` | `SNAP <w> <h>`, then the frame as RGB565, high byte first |
| `k1`, `k2` | a press of KEY1 / KEY2, the same path as the real button |
| `acc` | toggles `ACC ax ay az flipped=` lines at 10 Hz |
| `st` | `ST board= awake= flipped= battery= internal= psram=` |
| anything else | `ERR unknown '<line>'` |

A project adds its own commands to `runCommand()` and the list in
`tools/serial_cmd.py`; typical ones are `perf` (fps, paint and push time),
a command that forces a screen that is otherwise hard to reach, and a demo
mode with made-up data for README screenshots. Toggles stay on after the
script exits.

Periodic lines are printed only while `serialHasRoom()`: when nobody reads
the port the USB buffer fills up and every print would stall the loop.

`snap` writes a PNG at 3x (`--scale` to change). After a change to the
picture, flash and look at a screenshot. Answers can arrive late right after
the port opens: if a reply is missing, ask again. A screenshot blocks the
loop for as long as the 65 KB transfer takes.

## Publishing to M5Burner

`pio run -e sticks3 -t merged` (the extra script `tools/merged_image.py`)
merges the bootloader, the partition table, `boot_app0` and the application
into `.pio/build/sticks3/firmware-merged.bin`, a single image written at
0x0. M5Burner writes the uploaded file starting at address 0x0, so a bare
`firmware.bin` (meant for 0x10000) would overwrite the bootloader. The
script takes the addresses and flash parameters (dio, 80m, 8MB) from
PlatformIO's regular upload settings, so the image matches what `upload`
writes.

The steps, the checks before an upload, the site's rules and the record of
what was uploaded are in [docs/m5burner.md](docs/m5burner.md).

## Tests

Unit tests cover the logic in `lib/`, with one directory
`test/test_<module>/test_main.cpp` per module. Expected values are worked out
by hand in comments next to them. Rendering is checked with `snap` on the
board: do not try to write tests for `Renderer`; that would require mocking
all of LovyanGFX and would prove nothing useful. To see `lib/` output without
the board, a project can build a small host preview that renders frames into
a PNG (see docs/StickS3.md).

- Unity's `TEST_ASSERT_GREATER_THAN` and friends compare integers: a float
  argument is truncated, so `0.3f` becomes 0 and the check quietly passes or
  fails for the wrong reason. Use the `_FLOAT` variants. `_DOUBLE` ones work
  because the native environment defines `UNITY_INCLUDE_DOUBLE`.
- Declare large objects (tables, frame buffers) `static` in tests, not on
  the stack.

`main` in the tests returns the number of failures from `UNITY_END()`, and
PlatformIO reports a non-zero exit code as a signal number. A line like
`Program received signal SIGALRM` with failing tests is a reporting artifact,
not a separate problem; it disappears once the tests pass.

## Conventions

- Commit messages: an imperative subject; the body says why, and what was
  checked on the board and how. Documentation changes go in the same commit
  as the code they describe.
- Facts found on the board are written down with the date and how they were
  found ("checked on 2026-09-28: ..."), so later sessions know what is
  measured and what is assumed. Say "not yet checked on the board" when it is
  not.
- This file describes this project only. Do not refer to sibling projects
  here: the repository is published on its own.
