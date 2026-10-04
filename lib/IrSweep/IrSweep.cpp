#include "IrSweep.h"

#include <math.h>

namespace {

// Roughly 2^(9 i / 23): log-spaced from 1 to 512 out of 1024, kept strictly
// increasing where rounding would repeat a value. That is 50 dB in steps of
// 1 to 2.7 dB.
constexpr uint32_t kDuty[IrSweep::kLevels] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 15, 20, 26, 34, 45, 58, 77, 101, 132, 173, 227, 298, 390, 512,
};

} // namespace

uint32_t IrSweep::duty(int level) {
    if (level < 0) {
        level = 0;
    } else if (level >= kLevels) {
        level = kLevels - 1;
    }
    return kDuty[level];
}

float IrSweep::levelDb(int level) {
    const double d = static_cast<double>(duty(level)) / kFullScale;
    return static_cast<float>(20.0 * log10(sin(M_PI * d)));
}

IrSweep::Result IrSweep::estimate(const uint8_t hits[kLevels], uint8_t bursts,
                                  uint8_t falseHits) {
    Result result = estimateHits(hits, bursts);
    if (falseHits > 0) {
        result.state = State::Noisy;
    }
    return result;
}

IrSweep::Result IrSweep::estimateHits(const uint8_t hits[kLevels], uint8_t bursts) {
    Result result;
    if (bursts == 0) {
        return result;
    }
    // A level counts as heard when at least half of its bursts were.
    const auto heard = [&](int level) { return 2u * hits[level] >= bursts; };

    if (!heard(kLevels - 1)) {
        return result; // NoEcho, 0 dB
    }
    if (heard(0)) {
        result.state = State::Saturated;
        result.marginDb = -levelDb(0);
        result.level = 0;
        return result;
    }

    float sum = 0;
    for (int i = 0; i < kLevels; ++i) {
        const uint8_t h = hits[i] > bursts ? bursts : hits[i];
        sum += static_cast<float>(h) / bursts;
    }
    float x = kLevels - sum - 0.5f;
    if (x < 0) {
        x = 0;
    } else if (x > kLevels - 1) {
        x = kLevels - 1;
    }
    const int below = static_cast<int>(x);
    const int above = below + 1 < kLevels ? below + 1 : below;
    const float t = x - below;
    const float db = levelDb(below) + (levelDb(above) - levelDb(below)) * t;

    result.state = State::Ok;
    result.marginDb = -db;
    result.level = x;
    return result;
}

IrSweep::Window IrSweep::around(const Result &last) {
    Window window;
    int center;
    switch (last.state) {
    case State::Ok:
        center = static_cast<int>(last.level + 0.5f);
        break;
    case State::Saturated:
        center = 0;
        break;
    case State::NoEcho:
        center = kLevels - 1;
        break;
    default:
        return window; // Noisy: start over
    }
    window.first = center - kHalfWindow < 0 ? 0 : center - kHalfWindow;
    window.last = center + kHalfWindow > kLevels - 1 ? kLevels - 1 : center + kHalfWindow;
    // Near an end of the scale the window keeps its width.
    if (window.first == 0) {
        window.last = 2 * kHalfWindow;
    } else if (window.last == kLevels - 1) {
        window.first = kLevels - 1 - 2 * kHalfWindow;
    }
    return window;
}

bool IrSweep::contains(const uint8_t hits[kLevels], uint8_t bursts, Window window) {
    const bool lowestHeard = 2u * hits[window.first] >= bursts;
    const bool highestHeard = 2u * hits[window.last] >= bursts;
    return (window.first == 0 || !lowestHeard) && (window.last == kLevels - 1 || highestHeard);
}

void IrSweep::fill(uint8_t hits[kLevels], uint8_t bursts, Window window) {
    for (int i = 0; i < window.first; ++i) {
        hits[i] = 0;
    }
    for (int i = window.last + 1; i < kLevels; ++i) {
        hits[i] = bursts;
    }
}
