# M5Burner listing

<!-- Template: fill in the table and the two descriptions when the firmware is
uploaded, and keep the rest. Until then the project is not on M5Burner. -->

| | |
|---|---|
| Name | `<Name>` |
| Category | `<Games / Audio & Media / Robotics & Control / ...>` |
| Device | StickS3 |
| Version | v1.0.0 |
| File | `dist/<Name>-v1.0.0.bin`, a full image flashed at 0x0 |
| Cover | `dist/<Name>-cover.png` (1440 x 810) |
| Project link | `https://github.com/mbogatyr/<Name>` |
| Visibility | Public |
| Uploaded | `<date>`, "Pending" review |

## Preparing the files

`dist/` is not in git: a file picker can't easily reach `.pio/`, a hidden
folder, so the upload files are copied there.

```bash
~/.platformio/penv/bin/pio run -e sticks3 -t merged
mkdir -p dist && cp .pio/build/sticks3/firmware-merged.bin dist/<Name>-v1.0.0.bin
```

The cover is 1440 x 810: M5Burner stores covers at 16:9, and its cover editor
frames the image at about that shape (a 4:3 picture needs the Crop slider
raised to fill the frame). Screenshots come from the board
(`tools/serial_cmd.py snap`):

- landscape: one 240 x 135 screen scaled 6x with nearest neighbour is exactly
  1440 x 810;
- portrait: three 135 x 240 screens at 3x (405 x 720) side by side on a
  background in the screen's colour.

A copy of the cover can go to `docs/` for the README.

### Checking the image before the upload

Write the image alone, the way M5Burner does, and see the firmware start:

```bash
PY=~/.platformio/penv/bin/python
ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
$PY $ESPTOOL --chip esp32s3 erase_flash
$PY $ESPTOOL --chip esp32s3 write_flash 0x0 dist/<Name>-v1.0.0.bin
```

A `verify_flash` of the whole image against a board that has run it always
fails, and that is expected: the image has the NVS partition (0x9000–0xE000)
blank, and the firmware writes a page header there on its first boot. Verify
0x0–0x9000 and 0xE000 to the end instead.

The image starts with the bootloader at 0x0 (header `e9 03 02 3f`), the
partition table at 0x8000 (`aa 50`) and the application at 0x10000, byte for
byte `firmware.bin`. It does not contain data partitions after the
application (LittleFS): the firmware formats them on the first start, and a
later flash leaves them alone. NVS (`Preferences`) is erased by every flash
of the image.

How the full-image format is known: of the six StickS3 firmwares on
burner.m5stack.com on 2026-09-27, five, including the official UIFlow2.0,
were full images with the layout above; one was uploaded as a bare
application. The merged image was then tested on a board: flashed alone at
0x0 with esptool.

## Uploading

The form is at burner.m5stack.com/developer/firmware/upload. It needs a
logged-in account and real file attachments, so the upload is done in the
user's own Chrome with Claude in Chrome (the built-in browser pane cannot
attach files). It asks for:

- a name, a category and the supported devices;
- a firmware description and a version description, both in Markdown;
- the version number and a link to the project;
- the `.bin` file and the cover;
- visibility: Public goes to moderation ("Pending").

How M5Burner behaves:

- "Supported devices" takes one device; choosing a second replaces the
  first. One image for two boards means two listings with different names.
- A listing is found by its name. Uploading under a name that already exists
  adds a version to that listing, and the server refuses it if the public
  data differ ("新增版本不能修改固件说明，请使用固件编辑接口": a new version
  cannot change the firmware description, use the firmware edit page).
- While a listing waits for review it offers "Edit firmware public data",
  "Edit version" and "Withdraw", but no "New version".
- Someone else's trademark in the name or picture may stop moderation. Draw
  your own characters.

## Description (as uploaded)

```markdown
**<Name>** <one sentence: what it turns the M5StickS3 into>.

<A paragraph on what it does and what is special about it.>

- <feature>
- <feature>

### Controls

Hold the stick <upright / in landscape>, screen towards you.

| Input | Action |
|---|---|
| KEY1 (the blue button on the front) | <action> |
| KEY2 (the button on the edge) | <action> |

The screen goes dark after 3 minutes without use; press a key or pick the
stick up to wake it. Double-press the side button to switch the stick off.

Source code: https://github.com/mbogatyr/<Name>
```

Version description (v1.0.0):

```markdown
First release.

- <what is in it>
```
