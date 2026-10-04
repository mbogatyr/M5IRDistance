#pragma once

#include <M5Unified.h>

// Draws on the built-in StickS3 display (135x240).
//
// The whole frame is composed in a sprite and pushed in a single call:
// drawing directly on the screen causes noticeable flicker.
//
// For now this is a placeholder that shows the time since power-on and the
// battery level. In a new project the Screen fields and the body of paint()
// change; the sprite, skipping identical frames, invalidate(), setFlipped()
// and writeSnapshot() stay.
class Renderer {
  public:
    // Everything the screen shows; a frame is redrawn only when it changes.
    struct Screen {
        uint32_t seconds = 0;
        int battery = -1; // percent, or -1 while unknown

        bool operator==(const Screen &other) const {
            return seconds == other.seconds && battery == other.battery;
        }
    };

    // Call after M5.begin(). Returns false if there is no memory for the
    // frame.
    bool begin();

    // Redraws the screen only when the picture has changed.
    void draw(const Screen &screen);

    // Forgets the last frame. Needed after the display wakes up: its
    // contents are lost, and otherwise the comparison with the previous
    // frame would decide there is nothing to redraw.
    void invalidate();

    // Turns the picture by 180°, for a stick held upside down.
    void setFlipped(bool flipped);

    // The last frame over serial: "SNAP <w> <h>\n", then RGB565, high byte
    // first. tools/serial_cmd.py snap turns it into a PNG.
    void writeSnapshot(Print &out);

  private:
    void paint(const Screen &screen);

    M5Canvas canvas_{&M5.Display};

    bool flipped_ = false;
    bool hasPrevious_ = false;
    Screen previous_;
};
