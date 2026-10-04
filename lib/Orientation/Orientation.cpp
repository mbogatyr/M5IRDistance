#include "Orientation.h"

#include <math.h>

bool Orientation::update(uint32_t nowMs, float ax, float ay, float az) {
    const Side side = sideOf(ax, ay, az);
    if (side == Side::Unknown) {
        pending_ = Side::Unknown;
        return flipped_;
    }

    const bool wantFlipped = side == Side::Flipped;
    if (!decided_) {
        flipped_ = wantFlipped;
        decided_ = true;
        pending_ = Side::Unknown;
        return flipped_;
    }

    if (wantFlipped == flipped_) {
        pending_ = Side::Unknown;
    } else if (pending_ != side) {
        pending_ = side;
        pendingSinceMs_ = nowMs;
    } else if (nowMs - pendingSinceMs_ >= kSettleMs) {
        // Unsigned subtraction handles millis() wraparound correctly.
        flipped_ = wantFlipped;
        pending_ = Side::Unknown;
    }
    return flipped_;
}

void Orientation::reset() {
    decided_ = false;
    pending_ = Side::Unknown;
}

Orientation::Side Orientation::sideOf(float ax, float ay, float az) const {
    // Being shaken or carried: the acceleration contains more than gravity.
    const float g2 = ax * ax + ay * ay + az * az;
    const float lo = 1.0f - kShakeG;
    const float hi = 1.0f + kShakeG;
    if (g2 < lo * lo || g2 > hi * hi) {
        return Side::Unknown;
    }

    const bool portrait = layout_ == Layout::Portrait;
    const float along = portrait ? ay : ax;
    const float across = portrait ? ax : ay;
    const float a = fabsf(along);
    if (a < kMinG || a < fabsf(across) || a < fabsf(az)) {
        return Side::Unknown;
    }
    // Checked on the board. Landscape: with ax > 0 the picture is upright at
    // setRotation(1), KEY1 to the right of the screen. Portrait (2026-10-04):
    // with ay > 0 it is upright at setRotation(0), KEY1 below the screen.
    return along > 0 ? Side::Normal : Side::Flipped;
}
