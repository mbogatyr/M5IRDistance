#include "BatteryFilter.h"

uint8_t BatteryFilter::update(uint8_t reading) {
    if (reading > 100) {
        reading = 100;
    }
    const int32_t sample16 = static_cast<int32_t>(reading) * 16;
    if (!started_) {
        average16_ = sample16;
        shown_ = reading;
        started_ = true;
        return shown_;
    }
    // New average = old + (reading - old) / 4.
    average16_ += (sample16 - average16_) / 4;

    const int32_t shown16 = static_cast<int32_t>(shown_) * 16;
    const int32_t diff = average16_ - shown16;
    if (diff > 12 || diff < -12) { // 12/16 = 0.75 %
        shown_ = static_cast<uint8_t>((average16_ + 8) / 16);
    }
    return shown_;
}
