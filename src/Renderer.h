#pragma once

#include <M5Unified.h>
#include <string.h>

#include "IrSweep.h"

// Draws on the built-in StickS3 display (135x240).
//
// The whole frame is composed in a sprite and pushed in a single call:
// drawing directly on the screen causes noticeable flicker.
//
// Shows the last IR sweep: the distance once the range finder is
// calibrated, otherwise the margin in dB (how much weaker than full power
// the echo is still heard), and a bar per power level with how many of its
// bursts were heard, weakest on the left.
class Renderer {
  public:
    // Everything the screen shows; a frame is redrawn only when it changes.
    struct Screen {
        IrSweep::State state = IrSweep::State::NoEcho;
        int marginDb10 = 0; // tenths of a dB
        uint8_t hits[IrSweep::kLevels] = {};
        uint8_t bursts = 0;
        int cm10 = -1;         // tenths of a cm, or -1 without a distance
        bool extrapolated = false;
        bool calibrated = false;
        bool measured = false; // false until the first sweep is done
        bool paused = false;
        int battery = -1; // percent, or -1 while unknown

        bool operator==(const Screen &other) const {
            return state == other.state && marginDb10 == other.marginDb10 &&
                   memcmp(hits, other.hits, sizeof(hits)) == 0 && bursts == other.bursts &&
                   cm10 == other.cm10 && extrapolated == other.extrapolated &&
                   calibrated == other.calibrated && measured == other.measured &&
                   paused == other.paused &&
                   battery == other.battery;
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
