#pragma once

#include <stdint.h>

// Decides when to turn the display off after a period of inactivity.
//
// It does not deal with powering off: the StickS3 side button turns the
// board off on a double press by itself, in the PMIC, without the firmware.
//
// Like everything in lib/, it does not touch the hardware: the time and
// whether anything happened go in, a decision comes out.
class DisplayTimeout {
  public:
    static constexpr uint32_t kIdleMs = 180000; // 3 minutes

    explicit DisplayTimeout(uint32_t idleMs = kIdleMs);

    // Sets the point the idle time is counted from. Call once at startup.
    void begin(uint32_t nowMs);

    // activity: whether anything that should keep the display on happened
    // in this tick. What counts is up to the application: a held key, the
    // stick being picked up, a sound, an animation that is still running.
    bool shouldBeOn(uint32_t nowMs, bool activity);

  private:
    uint32_t idleMs_;
    uint32_t lastActivityMs_ = 0;
};
