#include "RangeCalibration.h"

#include <math.h>

bool RangeCalibration::add(float cm, float db) {
    if (!(cm > 0)) {
        return false;
    }
    for (int i = 0; i < count_; ++i) {
        if (fabsf(points_[i].cm - cm) < 0.25f) {
            points_[i] = {cm, db};
            return true;
        }
    }
    if (count_ == kMaxPoints) {
        return false;
    }
    // Insertion keeps the points sorted by distance.
    int at = count_;
    while (at > 0 && points_[at - 1].cm > cm) {
        points_[at] = points_[at - 1];
        --at;
    }
    points_[at] = {cm, db};
    ++count_;
    return true;
}

float RangeCalibration::cmOn(int segment, float db) const {
    const Point &a = points_[segment];
    const Point &b = points_[segment + 1];
    const float xa = log10f(a.cm);
    const float xb = log10f(b.cm);
    if (b.db == a.db) {
        return a.cm;
    }
    return powf(10.0f, xa + (db - a.db) * (xb - xa) / (b.db - a.db));
}

RangeCalibration::Estimate RangeCalibration::estimate(float db) const {
    Estimate result;
    if (count_ < 2) {
        return result;
    }

    // Inside: the first segment, from the near end, whose margins enclose
    // this one.
    for (int i = 0; i + 1 < count_; ++i) {
        const float lo = fminf(points_[i].db, points_[i + 1].db);
        const float hi = fmaxf(points_[i].db, points_[i + 1].db);
        if (db >= lo && db <= hi) {
            result.valid = true;
            result.cm = cmOn(i, db);
            return result;
        }
    }

    // Outside: along the end segment on the side the margin points to.
    const int last = count_ - 1;
    const int segment = db > points_[0].db ? 0 : last - 1;
    if (points_[segment + 1].db >= points_[segment].db) {
        return result; // the margin does not fall with distance there
    }
    float cm = cmOn(segment, db);
    const float nearest = points_[0].cm / 2;
    const float farthest = points_[last].cm * 2;
    cm = cm < nearest ? nearest : cm > farthest ? farthest : cm;
    result.valid = true;
    result.cm = cm;
    result.extrapolated = true;
    return result;
}
