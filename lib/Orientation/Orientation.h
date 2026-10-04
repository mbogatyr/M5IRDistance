#pragma once

#include <stdint.h>

// Decides from the accelerometer whether the device is turned by 180°, so
// the picture can be flipped to stay upright.
//
// Held upright, gravity runs along one board axis: the long Y axis for a
// portrait screen, the short X axis for a landscape one. Its sign tells
// which end faces down. Lying flat on a table (gravity along Z) or standing
// on the other edge, the top cannot be determined, so the orientation stays
// as it was.
//
// Like everything in lib/, it does not touch the hardware: it takes the time
// and acceleration, and returns a decision.
class Orientation {
  public:
    // Portrait: setRotation(0), normal; setRotation(2), flipped.
    // Landscape: setRotation(1), normal; setRotation(3), flipped.
    enum class Layout : uint8_t { Portrait, Landscape };

    static constexpr float kMinG = 0.6f;       // gravity along the axis, g
    static constexpr float kShakeG = 0.25f;    // tolerance of |a| around 1 g
    static constexpr uint32_t kSettleMs = 400; // hold time for a new position

    explicit Orientation(Layout layout = Layout::Portrait) : layout_(layout) {}

    // ax, ay, az: acceleration in g along the board axes (M5.Imu.getAccel).
    // Returns flipped().
    bool update(uint32_t nowMs, float ax, float ay, float az);

    bool flipped() const { return flipped_; }

    // The next clear reading takes effect immediately, without waiting.
    // Needed at startup and after the screen sleeps: the device may have
    // been turned over while the screen was off.
    void reset();

  private:
    enum class Side : uint8_t { Unknown, Normal, Flipped };

    Side sideOf(float ax, float ay, float az) const;

    Layout layout_;
    bool flipped_ = false;
    bool decided_ = false;
    Side pending_ = Side::Unknown;
    uint32_t pendingSinceMs_ = 0;
};
