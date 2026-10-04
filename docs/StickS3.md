# M5StickS3 reference

What earlier StickS3 projects found out about the board, M5Unified and the
tools around them. CLAUDE.md has the rules every session needs; this file is
read by section, when working on that part.

Unless a date is given, a fact was found in one of the projects listed at the
end. The library versions behind all of it: M5Unified 0.2.23, M5GFX 0.2.30,
Arduino core 2.0.17 on ESP-IDF 4.4 (`framework-arduinoespressif32` 4.20017).

## Hardware

- ESP32-S3-PICO-1-N8R8: two cores at 240 MHz, 8 MB flash, 8 MB octal PSRAM,
  about 320 KB of internal RAM for the application.
- ST7789P3 display, 135x240, on a 40 MHz SPI bus. It is physically about
  25x14 mm: draw large, bold shapes, with lines no thinner than 1.5 px.
- BMI270 IMU (accelerometer and gyroscope).
- ES8311 codec with a MEMS microphone, and a small speaker behind an
  amplifier that the PMIC switches.
- 250 mAh battery. No RTC: the clock starts from zero at every power-on.
- Product page: https://docs.m5stack.com/en/core/StickS3

## Buttons

- KEY1 (`M5.BtnA`, G11) is the blue key on the front. In portrait
  (`setRotation(0)`) it is below the screen; in landscape (`setRotation(1)`)
  it is right of the screen, around y = 70 of 135.
- KEY2 (`M5.BtnB`, G12) is on the edge. In landscape it is on the top edge
  above the right third of the screen, x ≈ 142–232 (centre 187): a hint for
  it on screen goes there. Taken from a photo of the board.
- The side button belongs to the PMIC (see CLAUDE.md). Call it "the side
  button" everywhere, in the README too, so it is not confused with KEY2.

M5Unified's click API: `wasPressed()`, `wasReleased()`, `isPressed()`,
`pressedFor(ms)`, `wasReleasedAfterHold()`, and for single/double clicks
`wasDecideClickCount()` with `getClickCount()`. A click is decided once the
double-click window has passed after the release; `setHoldThresh(300)` sets
the hold time to the same 300 ms. A hold produces no click.

## Orientation and the IMU

- M5Unified already turns the BMI270's axes to the board's: X across the
  short side, Y along the long side, Z out of the screen. `M5.Imu.getAccel()`
  reads the sensor itself and returns g.
- Landscape: with `ax > 0` the picture is upright at `setRotation(1)` (KEY1
  to the right of the screen); with `ax < 0` at `setRotation(3)`. Checked on
  the board in two projects.
- Portrait: with `ay > 0` the picture is upright at `setRotation(0)` (KEY1
  below the screen); with `ay < 0` at `setRotation(2)`. Checked on the board
  on 2026-10-04: about +0.9 g held upright, -0.96 g upside down. See
  `Orientation`.
- Reading the IMU about 30 times a second is plenty; reading it on every
  pass of a fast loop wastes time.
- Tilt must not count as activity on its own: a stick in a pocket would keep
  its display on forever. `PickupDetector` (a turn from the pose the stick was
  left in) wakes it; a moving stick in hand can count as activity when the
  application is steered by tilt.
- After the display wakes, re-level anything tilt-based (a neutral pose,
  `Orientation::reset()`): the stick is probably held differently now. Wait
  about 0.7 s after wake-up or power-on before taking a neutral pose, so the
  stick is in the user's hands and no longer being picked up.

## Memory and speed

- Internal RAM vs PSRAM: anything read or written at random, pixel by pixel,
  belongs in internal RAM. Through the PSRAM cache one frame of per-pixel
  drawing took 26 ms instead of 13.5 ms. A 65 KB sprite fits in internal RAM;
  large buffers read in sequence (audio, images) go to PSRAM with
  `heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`. Allocate with a fallback to the
  other kind and report a failure (`ERR ...` over serial, a halt screen).
- A full-frame `pushSprite` takes 13–14.5 ms over SPI. That alone caps a
  full-screen animation at about 70 fps; measured projects got 30–35 fps with
  real drawing. A partial push sends only what changed:
  `M5.Display.setClipRect(x, y, w, h)`, `canvas.pushSprite(0, 0)`,
  `M5.Display.clearClipRect()`.
- The FPU is single precision only. `double` is emulated and many times
  slower (it made one audio loop four times slower): use `float` and `sinf`,
  `sqrtf`, etc. `#pragma GCC diagnostic warning "-Wdouble-promotion"` in a
  hot file catches accidental doubles.
- There is no hardware division or square root that the compiler uses, and
  `floorf`/`ceilf` are library calls. Hot loops benefit from a fast inverse
  square root, multiplying by a reciprocal and integer floor.
- `-O2` instead of the framework's `-Os` is set in `platformio.ini`; a single
  file can also be raised with `#pragma GCC optimize("O2")`.
- The second core: a worker task pinned to core 0 with
  `xTaskCreatePinnedToCore`, driven by `xTaskNotifyGive`/`ulTaskNotifyTake`.
  It must block between jobs: the task watchdog checks core 0's idle task and
  panics after 5 s if it never runs.
- Flash writes stop both cores: 10–20 ms, rarely up to 100 ms. Audio capture
  needs a DMA ring longer than that.
- The ESP32-S3 has a hardware random number generator: `esp_random()`.

## Drawing

- A 16-bit LovyanGFX sprite keeps RGB565 with its bytes swapped (high byte
  first, as the SPI bus wants it). Code that writes the buffer directly swaps
  the colour (`__builtin_bswap16`); the buffer can go over serial as it is.
  Writing the buffer directly is much faster than `drawPixel`.
- A 16-bit sprite has no alpha. Glows and gradients are built from shapes in
  colours precomputed against the known background; anti-aliased shapes are
  drawn into the buffer by hand (LovyanGFX fills ellipses and triangles
  without anti-aliasing).
- Fading by scaling every colour toward black gives grey a slight green tint
  at low levels (RGB565 has 6 bits of green); invisible in a 300 ms fade.
- Fonts: built-in M5GFX fonts are ASCII only. `Font0` is 6 px per character;
  `Font7` (seven-segment) is 48 px tall, too tall for many layouts. Small text
  reads well in `DejaVu9`; bold labels can be drawn twice 1 px apart; big
  numbers in `FreeSansBold18pt7b`. A custom font is rendered on the Mac into
  alpha masks (C arrays) by a Python script with Pillow, so no font file
  reaches the firmware; keep the font's licence (OFL) next to it, and do not
  name derived assets after a Reserved Font Name.
- Images: PNGs in flash as C arrays, drawn with `drawPng`. Decoding is slow,
  so keep the decoded frame in a second sprite and redraw from it.
  `pushRotateZoom` squeezes or scales one sprite into another.
- The look of a screen is easiest to design in an HTML page at the real size
  (135x240 or 240x135) with the same shapes and coordinates as the C++: change
  it there first, then port.

## Audio

The microphone and the speaker share the ES8311's I2S clock lines and cannot
run at the same time.

- Only one of them: `cfg.internal_spk = false` (or `cfg.internal_mic = false`)
  before `M5.begin(cfg)`, and `M5.Speaker.end()` before using the mic.
- Both, taking turns: enable both in the config, then `M5.Mic.end()` before
  `M5.Speaker.begin()` and back, as M5Unified's `examples/Basic/Microphone`
  does.

### Microphone

- `M5.Mic.config()`: `over_sampling = 1` (the ES8311 decimates itself),
  `magnification = 2` (unity gain: M5Unified multiplies by
  `magnification / (2 * over_sampling)`, and the codec already adds +32 dB),
  `noise_filter_level = 0` (it is a first-order low-pass, not a noise gate).
- Keep two `M5.Mic.record()` blocks in flight and poll the mic all the time,
  even when the data is not needed: otherwise the queue runs dry and the next
  start gets stale audio. A DMA ring of 8 x 256 frames gives 256 ms at 16 kHz,
  512 ms at 8 kHz.
- Request an even number of samples: with an odd length M5Unified carries one
  sample of a pair over to the next request, and on the board that left a
  wrong sample at the start of a block every few seconds.
- After `Mic.begin()` the ES8311 returns zeros for about 70 ms; after a cold
  power-up, for up to a second.
- The codec's DC-removal filter barely passes anything below 60–70 Hz.
- In a quiet room there is broadband noise at 5–8 kHz around -64 dBFS (not
  from the display); quiet speech is at -50…-60 dBFS.
- M5Unified's `Mic_FFT` example analyses the start of its ring rather than
  the freshest samples.
- ESP-DSP ships with the framework but only for the ESP32 target; an FFT has
  to be our own (which also keeps it testable on the host).

### Speaker

- M5Unified's default `magnification = 1` plays about 20 dB below what the
  speaker can do. +6 dB per doubling up to 4; 8 clips. Use volume 255 and
  `magnification = 4`.
- `sample_rate = 48000`: M5Unified resamples every sound to it, and 48 kHz
  keeps the interpolation images far above hearing.
- The DMA ring delays the sound by about 40–43 ms after `playRaw()`. Start
  the sound that much earlier to sync it with the picture.
- The speaker plays almost nothing below 200 Hz. Give every sound a few ms of
  attack and a decay so nothing clicks.
- `playRaw()` does not copy: the buffer must live until `isPlaying()` is
  false. `M5.Speaker.end()` after a few seconds of silence also switches the
  amplifier off.

## Power and battery

- The side button and `M5.Power.powerOff()`: see CLAUDE.md. A firmware that
  powers off on idle needs a serial command (e.g. `idle 0`) that disables it,
  sent after every upload during development. As a fallback after
  `powerOff()`: `delay(1000)`, then `M5.Display.sleep()` and
  `esp_deep_sleep_start()` with no wake source.
- The battery level (`M5.Power.getBatteryLevel()`, 0–100) is derived from the
  voltage and sags under load: Wi-Fi going on took 6 % off within seconds.
  Read it every few seconds through `BatteryFilter`.
- `M5.Power.isCharging()` gave inconsistent answers.
- The display backlight is the main consumer: `setBrightness(0)` and then
  `M5.Display.sleep()`.

## Storage

- `default_8MB.csv`: NVS at 0x9000 (20 KB), otadata at 0xE000, two 3.2 MB app
  slots (OTA) from 0x10000, a 1.5 MB `spiffs` data partition at 0x670000 and
  a 64 KB coredump at 0x7F0000.
- A custom table goes in `partitions.csv` with
  `board_build.partitions = partitions.csv`. For example one 2 MB app (no OTA)
  and a 5.9 MB data partition.
- LittleFS looks for a partition labelled `spiffs` by default. LittleFS rather
  than FAT: it survives a power cut in the middle of a write.
  `LittleFS.begin(false)` and formatting on the first start take a while; show
  a screen. Its `usedBytes()` plus a directory walk takes about 0.4 s, so
  cache the free space; rewriting a file header takes 0.1–0.25 s.
- Small settings: `Preferences` (NVS) with a namespace and a key.
- An M5Burner image covers NVS with blank `0xFF`: flashing it erases whatever
  `Preferences` stored. Data partitions after the application are not in the
  image and survive.

## Wi-Fi and Bluetooth

- A soft access point with `WebServer`: `handleClient()` runs inside `loop()`,
  one request at a time, so a long `streamFile` holds the loop for seconds.
- No captive-portal DNS: macOS would open the page in its captive-network
  sheet, which cannot download files.
- Joining the board's access point cuts the Mac (and the Claude Code session)
  off the internet: automate tests over serial instead.
- Switching the AP off logs a couple of harmless `rxcb … failed` netif errors.
- Wi-Fi on raises the microphone's noise floor slightly (-60.2 to -58.8 dBFS).
- BLE central with `h2zero/NimBLE-Arduino@^2.5.1`: the callbacks run in
  NimBLE's host task, so they only store into atomics; connecting and writing
  happen from `loop()`. A connect blocks for up to 5 s: draw the
  "connecting" frame before the call. `setActiveScan(true)` is needed to get
  the scan response; `scan->start(0, false, true)` restarts the scan and
  clears its duplicate filter.

## Tools on the Mac

- Host preview: build `lib/` (and even `src/` drawing code, against a small
  stub of `M5Canvas`) with the system `c++ ... -lz`, render frames into an
  RGB565 buffer and write a PNG contact sheet. Shows the picture without
  flashing, and gives render times on the Mac.
- Audio checks: `say -v Samantha` speaks, `afplay -v` plays a WAV,
  `afconvert -f WAVE -d LEI16@16000 -c 1` converts, and
  `ffmpeg -f avfoundation` records the board's speaker through the Mac's mic.
  The terminal needs microphone access, or ffmpeg hangs waiting for the
  prompt. A 440 Hz tone from the Mac and the firmware's log is an end-to-end
  test of the microphone.
- Headless Chrome for rendering SVG or HTML into images: run it without
  `--user-data-dir`, otherwise on macOS it takes the shot but never exits.
- Generated C++ (images, fonts) is committed, so building the firmware needs
  neither Python nor the generator's dependencies. Never edit it by hand.

## Cardputer ADV

The same image runs on the Cardputer ADV (checked on 2026-09-29):

- `M5.getBoard()` returns 24 there (26 on the StickS3).
- ESP32-S3 with 8 MB flash and no PSRAM: the framework boots without it
  (`CONFIG_SPIRAM_IGNORE_NOTFOUND`), so allocations must fall back to
  internal RAM.
- 240x135 ST7789 on the same bus; `setRotation(1)` is right for both boards.
- The IMU is turned by 90°: `x = ay, y = -ax`.
- Keyboard: a TCA8418 at 0x34 on `m5::In_I2C`. Only G0 maps to `M5.BtnA`.
- Flashing works through the usual auto-reset.
- M5Burner takes one device per listing, so a second board means a second
  listing.

The original Cardputer (GPIO keyboard matrix) and the StickC Plus/Plus2
(classic ESP32) would need separate builds.

## Where to find working code

Projects on GitHub that did these things, to copy from rather than rewrite:

| What | Project |
|---|---|
| Continuous mic capture (`BlockRing`, `AudioCapture`), level meter, recording to LittleFS, Wi-Fi access point with a web page, battery level | [M5VoiceRecorder](https://github.com/mbogatyr/M5VoiceRecorder) |
| Mic and speaker taking turns, voice activity detection, speaker playback (`VoicePlayer`), anti-aliased shapes into the sprite buffer, idle power-off, host render preview | [M5TalkingTom](https://github.com/mbogatyr/M5TalkingTom) |
| FFT, spectrum from the mic, `Orientation` in landscape | [M5SpectrumAnalyzer](https://github.com/mbogatyr/M5SpectrumAnalyzer) |
| PNG frames in flash, art pipeline (SVG → headless Chrome → PNG → C++), CRT effects | [M5TVset](https://github.com/mbogatyr/M5TVset) |
| Tilt steering, a large table in internal RAM, Cardputer ADV keyboard | [M5Mars](https://github.com/mbogatyr/M5Mars) |
| Shake gesture, alpha-mask compositing, sound synthesis, second-core worker, partial push, 1440x810 cover generator | [M5Dice](https://github.com/mbogatyr/M5Dice) |
| Progressive rendering on two cores, intro screen, colour helpers | [M5Fractal](https://github.com/mbogatyr/M5Fractal) |
| BLE central (NimBLE), command queue with retries, click and hold handling, `Preferences`, demo mode for screenshots | [M5LegoDuploTrain](https://github.com/mbogatyr/M5LegoDuploTrain) |
