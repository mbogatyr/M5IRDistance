#pragma once

#include <stdint.h>

// Turns an IR echo margin (dB) into a distance (cm) from points measured
// with the target at known distances.
//
// The echo weakens with distance roughly as a power law, so the margin is
// close to a straight line in log(distance), but not quite: on the board
// a white sheet gave 15.5 dB from 5 to 10 cm and 9 dB from 10 to 20 cm. So
// the points are joined by straight segments in log(cm) rather than fitted
// with one line, and a margin beyond the first or last point is
// extrapolated along the end segment.
//
// The margin depends on the target's colour and material as much as on
// its distance: the result is only right for the surface the points were
// taken with.
class RangeCalibration {
  public:
    static constexpr int kMaxPoints = 8;

    struct Point {
        float cm;
        float db;
    };

    struct Estimate {
        bool valid = false;
        float cm = 0;
        // Outside the measured points; within half the nearest point's
        // distance and twice the farthest one's.
        bool extrapolated = false;
    };

    // Adds a point, or replaces the one within 0.25 cm of it. Returns false
    // for a distance that is not positive, or when all points are taken.
    bool add(float cm, float db);
    void clear() { count_ = 0; }

    // Points sorted by distance, nearest first.
    int count() const { return count_; }
    const Point &point(int i) const { return points_[i]; }

    // Needs two points or more. A segment along which the margin does not
    // fall with distance cannot be extrapolated.
    Estimate estimate(float db) const;

  private:
    float cmOn(int segment, float db) const;

    Point points_[kMaxPoints] = {};
    int count_ = 0;
};
