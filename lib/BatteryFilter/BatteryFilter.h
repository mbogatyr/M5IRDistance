#pragma once

#include <stdint.h>

// Steadies the battery percentage shown on screen.
//
// The StickS3 derives the level from the battery voltage, which sags
// under load: switching Wi-Fi on dropped the reading by 6 % within
// seconds on the board. An exponential average smooths the readings, and
// the shown value only moves once the average has left it by more than
// 3/4 of a percent, so it does not flicker between two neighbours.
class BatteryFilter {
  public:
    // reading: 0..100 from the PMIC, taken every few seconds. Returns the
    // percentage to show.
    uint8_t update(uint8_t reading);

    uint8_t shown() const { return shown_; }

  private:
    // Average in 1/16 of a percent, so integer maths keeps its precision.
    int32_t average16_ = 0;
    uint8_t shown_ = 0;
    bool started_ = false;
};
